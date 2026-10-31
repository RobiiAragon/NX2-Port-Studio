// Included by hle_svc.c: g_map_lock covers objects, handles and source pins.
// Guest pages are 4 KiB even on a 16 KiB host. As for borrowed stack sources,
// QueryMemory reports the logical permission; mprotect cannot restrict one
// guest page without changing the permissions of unrelated native-page siblings.
#define MEMORY_HANDLE_BASE 0xe0000u
#define MEMORY_HANDLE_END  0xf0000u
#define MEMORY_OBJECT_MAX 256
struct dm_memory_object { uint64_t address, size; unsigned permission, refs, shared; size_t allocation_size; };
static dm_memory_object *g_memory_objects[MEMORY_OBJECT_MAX];
static struct { uint32_t handle; dm_memory_object *object; } g_memory_handles[MEMORY_OBJECT_MAX];
static uint32_t g_memory_next_handle = MEMORY_HANDLE_BASE;
static struct { dm_memory_object *object; uint64_t address, size; unsigned permission; } g_shared_maps[64];

static int memory_transfer_busy(uint64_t address, uint64_t size) {
    for (unsigned i = 0; i < MEMORY_OBJECT_MAX; ++i) {
        dm_memory_object *o = g_memory_objects[i];
        if (o && !o->shared && address < o->address + o->size && o->address < address + size) return 1;
    }
    return 0;
}
static void memory_transfer_query(uint64_t address, uint64_t *base, uint64_t *size, uint32_t *attr, uint64_t *permission) {
    uint64_t lo = *base, end = lo + *size;
    for (unsigned i = 0; i < MEMORY_OBJECT_MAX; ++i) {
        dm_memory_object *o = g_memory_objects[i]; if (!o || o->shared) continue;
        uint64_t hi = o->address + o->size;
        if (address >= o->address && address < hi) {
            if (o->address > lo) lo = o->address;
            if (hi < end) end = hi;
            *attr |= 1; *permission = o->permission;
        } else if (hi <= address && hi > lo) lo = hi;
        else if (o->address > address && o->address < end) end = o->address;
    }
    *base = lo; *size = end - lo;
}
static void memory_release_locked(dm_memory_object *o) {
    if (--o->refs) return;
    for (unsigned i = 0; i < MEMORY_OBJECT_MAX; ++i) if (g_memory_objects[i] == o) { g_memory_objects[i] = NULL; break; }
    if (o->shared && munmap((void *)(uintptr_t)o->address, o->allocation_size)) die("shared memory backing munmap");
    free(o);
}
static uint32_t memory_transfer_create(uint64_t address, uint64_t size, unsigned permission, uint32_t *out) {
    if (address & 0xfff) return 0xcc01;
    if (!size || size & 0xfff) return 0xca01;
    if (size > UINT64_MAX - address) return 0xd401;
    if (permission != 0 && permission != 1 && permission != 3) return 0xd801;
    pthread_mutex_lock(&g_map_lock);
    uint32_t result = 0;
    unsigned oi = 0, hi = 0;
    while (oi < MEMORY_OBJECT_MAX && g_memory_objects[oi]) ++oi;
    while (hi < MEMORY_OBJECT_MAX && g_memory_handles[hi].object) ++hi;
    // A host malloc-zone allocation is not a kernel-owned lifetime. Unlike a
    // borrowed thread stack, a service can outlive the allocating guest call.
    if ((!dm_guest_as_contains(address) && !module_of((void *)(uintptr_t)address)) ||
        !map_source_backed(address, size) || map_source_busy(address, size)) result = 0xd401;
    else if (oi == MEMORY_OBJECT_MAX || hi == MEMORY_OBJECT_MAX || g_memory_next_handle == MEMORY_HANDLE_END) result = 0xce01;
    dm_memory_object *o = result ? NULL : calloc(1, sizeof *o);
    if (!result && !o) result = 0xce01;
    if (!result) {
        *o = (dm_memory_object){.address = address, .size = size, .permission = permission, .refs = 1};
        uint32_t h = g_memory_next_handle++; // No stale handle can acquire a later object's lifetime.
        g_memory_objects[oi] = o; g_memory_handles[hi].handle = h; g_memory_handles[hi].object = o;
        *out = h;
    }
    pthread_mutex_unlock(&g_map_lock);
    return result;
}
static uint32_t memory_handle_close(uint32_t handle) {
    pthread_mutex_lock(&g_map_lock);
    uint32_t result = 0xe401;
    for (unsigned i = 0; i < MEMORY_OBJECT_MAX; ++i) if (g_memory_handles[i].object && g_memory_handles[i].handle == handle) {
        dm_memory_object *o = g_memory_handles[i].object;
        g_memory_handles[i].object = NULL; memory_release_locked(o); result = 0; break;
    }
    pthread_mutex_unlock(&g_map_lock);
    return result;
}
dm_memory_object *dm_memory_retain_transfer(uint32_t handle, size_t size, unsigned permission) {
    pthread_mutex_lock(&g_map_lock);
    dm_memory_object *object = NULL;
    for (unsigned i = 0; i < MEMORY_OBJECT_MAX; ++i) if (g_memory_handles[i].object && g_memory_handles[i].handle == handle) {
        dm_memory_object *o = g_memory_handles[i].object;
        if (!o->shared && o->size == size && o->permission == permission) { object = o; ++o->refs; }
        break;
    }
    pthread_mutex_unlock(&g_map_lock);
    return object;
}
void dm_memory_release(dm_memory_object *object) {
    if (!object) return;
    pthread_mutex_lock(&g_map_lock); memory_release_locked(object); pthread_mutex_unlock(&g_map_lock);
}
void *dm_memory_data(dm_memory_object *object) { return (void *)(uintptr_t)object->address; }
size_t dm_memory_size(dm_memory_object *object) { return object->size; }
dm_memory_object *dm_memory_create_shared(size_t size) {
    size_t page = getpagesize();
    if (!size || (size & 0xfff) || size > SIZE_MAX - page) return NULL;
    pthread_mutex_lock(&g_map_lock);
    unsigned i = 0; while (i < MEMORY_OBJECT_MAX && g_memory_objects[i]) ++i;
    dm_memory_object *o = i == MEMORY_OBJECT_MAX ? NULL : calloc(1, sizeof *o);
    if (o) {
        size_t allocation = (size + page - 1) & ~(page - 1);
        void *data = mmap(NULL, allocation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (data == MAP_FAILED) { free(o); o = NULL; }
        else {
            *o = (dm_memory_object){.address = (uintptr_t)data, .size = size, .permission = 1, .refs = 1, .shared = 1, .allocation_size = allocation};
            g_memory_objects[i] = o;
        }
    }
    pthread_mutex_unlock(&g_map_lock);
    return o;
}
uint32_t dm_memory_copy_handle(dm_memory_object *object) {
    pthread_mutex_lock(&g_map_lock);
    unsigned i = 0; while (i < MEMORY_OBJECT_MAX && g_memory_handles[i].object) ++i;
    uint32_t handle = 0;
    if (i < MEMORY_OBJECT_MAX && g_memory_next_handle < MEMORY_HANDLE_END) {
        handle = g_memory_next_handle++;
        g_memory_handles[i].handle = handle; g_memory_handles[i].object = object; ++object->refs;
    }
    pthread_mutex_unlock(&g_map_lock);
    return handle;
}
static unsigned memory_mapping_permission(uint64_t address) {
    for (unsigned i = 0; i < 64; ++i) if (g_shared_maps[i].object && address >= g_shared_maps[i].address && address - g_shared_maps[i].address < g_shared_maps[i].size)
        return g_shared_maps[i].permission;
    return 0;
}
static int memory_shared_destination_free(uint64_t address, uint64_t size) {
    if (!memory_contains((uintptr_t)g_as, RESERVE, address, size) || map_find(address, size)) return 0;
    uint64_t end = address + size, heap = (uintptr_t)g_heap;
    if (address < heap + g_heap_cap && heap < end) return 0;
    uint64_t alias = (uintptr_t)g_alias, low = address > alias ? address : alias;
    uint64_t high = end < alias + g_alias_cap ? end : alias + g_alias_cap;
    return high <= low || alias_unmapped(low - alias, high - low) * 4096 == high - low;
}
static uint32_t memory_shared_map(uint32_t handle, uint64_t address, uint64_t size, unsigned permission, int unmap) {
    size_t page = getpagesize();
    if (address & 0xfff) return 0xcc01;
    if (address & (page - 1)) {
        // Open platform limit: fixed native execution cannot alias a 4 KiB
        // address inside an Apple page without replacing neighboring mappings.
        LOG("[RED] shared memory: guest address %#llx needs a native-page-aligned alias", (unsigned long long)address);
        return 0xd401;
    }
    if (!size || size & 0xfff) return 0xca01;
    if (size > UINT64_MAX - address || !memory_contains((uintptr_t)g_as, RESERVE, address, size)) return 0xdc01;
    if (!unmap && permission != 1) return 0xd801; // The HID service gives clients read access only.
    pthread_mutex_lock(&g_map_lock);
    dm_memory_object *o = NULL;
    for (unsigned i = 0; i < MEMORY_OBJECT_MAX; ++i) if (g_memory_handles[i].object && g_memory_handles[i].handle == handle) { o = g_memory_handles[i].object; break; }
    uint32_t result = !o || !o->shared ? 0xe401 : o->size != size ? 0xca01 : 0;
    unsigned slot = 0;
    if (!result && unmap) {
        while (slot < 64 && !(g_shared_maps[slot].object == o && g_shared_maps[slot].address == address && g_shared_maps[slot].size == size)) ++slot;
        if (slot == 64) result = 0xd401;
        else {
            if (mmap((void *)(uintptr_t)address, o->allocation_size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) die("UnmapSharedMemory reservation restore");
            int i = map_find(address, size); if (!i) die("shared mapping registry mismatch"); map_del(i - 1);
            memset(&g_shared_maps[slot], 0, sizeof g_shared_maps[slot]); memory_release_locked(o);
        }
    } else if (!result) {
        while (slot < 64 && g_shared_maps[slot].object) ++slot;
        if (slot == 64 || g_nmap == MAX_MAP) result = 0xce01;
        else if (!memory_shared_destination_free(address, o->allocation_size)) result = 0xd401;
        else {
            vm_address_t target = address;
            vm_prot_t current, maximum;
            kern_return_t kr = vm_remap(mach_task_self(), &target, o->allocation_size, 0,
                VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(), o->address, FALSE, &current, &maximum, VM_INHERIT_NONE);
            if (kr != KERN_SUCCESS || target != address) result = 0xce01;
            else {
                if (vm_protect(mach_task_self(), address, o->allocation_size, FALSE, VM_PROT_READ) != KERN_SUCCESS) die("MapSharedMemory protection");
                g_shared_maps[slot].object = o; g_shared_maps[slot].address = address; g_shared_maps[slot].size = size;
                g_shared_maps[slot].permission = permission; ++o->refs;
                map_add(address, size, 0, 6 /*Shared*/);
            }
        }
    }
    pthread_mutex_unlock(&g_map_lock);
    return result;
}
