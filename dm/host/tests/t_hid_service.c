// Asset-free native HID client fixtures. Exercise actual SVC objects and HIPC
// encoding, including ownership after the sender closes its transfer handle.
#include "../hle_svc.c"
#include "../hle_sf.c"
#include "../hle_hid_service.c"
#include <assert.h>

module_t g_mod[MAX_MOD]; int g_nmod; struct dm_flags g_flag;
void hlecount_svc(int n) { (void)n; }
int svc_thread(int n, uint64_t *r) { (void)n; (void)r; return 0; }
module_t *module_of(const void *p) { (void)p; return NULL; }
void dm_backtrace(uint64_t p) { (void)p; }
void guest_clock_read_wait(void) {}
uint64_t guest_ns(void) { return 0; }
uint64_t dm_entropy64(void) { return 0; }
int thr_limit_used(void) { return 0; }
int dm_ue_heap_owns(uint64_t a, uint64_t n) { (void)a; (void)n; return 0; }
void dm_guest_write_prepare(void *p, size_t n) { (void)p; (void)n; }

void dm_hid_write_shared(void *memory, uint64_t sequence) {
    __atomic_store_n((uint64_t *)memory, sequence, __ATOMIC_RELEASE);
}

static uint32_t transfer(uintptr_t address, uint64_t size, unsigned permission, uint32_t *out) {
    uint64_t r[31] = {0, address, size, permission};
    svc_dispatch(0x15, r, 0); *out = (uint32_t)r[1]; return (uint32_t)r[0];
}
static uint32_t close_memory(uint32_t handle) {
    uint64_t r[31] = {handle}; svc_dispatch(0x16, r, 0); return (uint32_t)r[0];
}
static uint32_t resize_memory(size_t size) {
    uint64_t r[31] = {0, size}; svc_dispatch(1, r, 0); return (uint32_t)r[0];
}
static uint32_t reply_handle, reply_kind;
static uint32_t request(uint32_t session, unsigned command, uint64_t aruid, uint64_t value, uint32_t handle) {
    _Alignas(16) uint32_t m[64] = {6, 0x8000000c, 1};
    unsigned at = 0x20;
    if (handle) { m[1] = 0x8000000c; m[2] = 3; m[5] = handle; at = 0x20; }
    uint32_t *p = (uint32_t *)((uint8_t *)m + at);
    p[0] = 0x49434653; p[1] = 1; p[2] = command;
    memcpy(p + 4, &aruid, 8); memcpy(p + 6, &value, 8);
    // data_words counts from the end of the special header, including alignment.
    unsigned data = handle ? 24 : 20;
    m[1] = 0x80000000 | ((at + 32 - data) / 4);
    if (g_sf[sf_slot(session)].object) {
        // IAppletResource::GetSharedMemoryHandle has no PID or input data.
        memset(m, 0, sizeof m); m[0] = 6; m[1] = 6;
        m[4] = 0x49434653; m[5] = 1; m[6] = command;
    }
    assert(h_sf_Send(session, m, sizeof m) == 0);
    assert(m[4] == 0x4f434653 && m[5] == 1);
    reply_handle = m[1] >> 31 ? m[3] : 0; reply_kind = m[1] >> 31 ? m[2] : 0;
    return m[6];
}
static uint32_t shared_map(uint32_t handle, uintptr_t address, uint64_t size, unsigned permission, int unmap) {
    uint64_t r[31] = {handle, address, size, permission};
    svc_dispatch(unmap ? 0x14 : 0x13, r, 0); return (uint32_t)r[0];
}
int main(void) {
    mem_init(); assert(!resize_memory(0x20000));
    uintptr_t source = (uintptr_t)g_heap + 0x1000; // A real 4 KiB boundary inside an Apple page.
    uint32_t handle;
    assert(transfer(source + 1, 0x4000, 1, &handle) == 0xcc01 && !handle);
    assert(transfer(source, 0, 1, &handle) == 0xca01 && !handle);
    assert(transfer(source, 0x4001, 1, &handle) == 0xca01 && !handle);
    assert(transfer(UINT64_MAX - 0xfff, 0x4000, 1, &handle) == 0xd401 && !handle);
    assert(transfer(source, 0x4000, 2, &handle) == 0xd801 && !handle);
    assert(transfer((uintptr_t)g_heap + 0x20000, 0x4000, 1, &handle) == 0xd401 && !handle);
    memset((void *)source, 0xa5, 0x4000);
    assert(!transfer(source, 0x4000, 1, &handle) && handle >= MEMORY_HANDLE_BASE);
    uint32_t other;
    assert(transfer(source + 0x1000, 0x1000, 1, &other) == 0xd401 && !other);
    assert(!dm_memory_retain_transfer(handle, 0x1000, 1));
    assert(!dm_memory_retain_transfer(handle, 0x4000, 3));
    uint64_t mi[5], q[31] = {(uintptr_t)mi, 0, source};
    svc_dispatch(6, q, 0);
    assert(mi[0] == source && mi[1] == 0x4000 && ((uint32_t *)mi)[5] == 1 && ((uint32_t *)mi)[6] == 1);
    uint32_t session;
    assert(!h_sf_Connect(&session, "hid"));
    assert(request(session, 3110, 128, 0x4000, 0x1234) != 0);
    assert(!request(session, 3110, 128, 0x4000, handle));
    assert(request(session, 3110, 128, 0x4000, handle) != 0);
    for (unsigned k = 0; k < 16; ++k) {
        uint64_t *ring = (uint64_t *)(source + k * 0x2c8);
        assert(ring[1] == 17 && ring[2] == 0 && ring[3] == 0); // Actual absence, no optical measurements.
    }
    assert(*(uint8_t *)(source - 1) == 0 && *(uint8_t *)(source + 0x4000) == 0);
    assert(!close_memory(handle) && close_memory(handle) == 0xe401);
    assert(!dm_memory_retain_transfer(handle, 0x4000, 1));
    assert(resize_memory(0) != 0); // The HID service owns its independent pin.
    assert(!request(session, 3112, 128, 0x107, 0));
    assert(request(session, 3112, 128, 0x108, 0) != 0);
    assert(request(session, 3112, 129, 0, 0) != 0);
    assert(request(session, 9999, 128, 0, 0) != 0);
    assert(!request(session, 3113, 128, 0x107, 0));
    assert(!request(session, 3111, 128, 0, 0));
    assert(!resize_memory(0)); // Finalization releases the service's ownership.
    assert(!resize_memory(0x20000));
    assert(!transfer(source, 0x4000, 1, &other) && other != handle);
    assert(!dm_memory_retain_transfer(handle, 0x4000, 1));
    assert(!close_memory(other) && !resize_memory(0));
    assert(!request(session, 0, 128, 0, 0) && reply_handle && reply_kind == 0x20);
    uint32_t applet = reply_handle;
    assert(!request(applet, 0, 0, 0, 0) && reply_handle && reply_kind == 2);
    uint32_t shared = reply_handle;
    assert(!request(applet, 0, 0, 0, 0) && reply_handle != shared);
    uint32_t shared_copy = reply_handle;
    uintptr_t first_view = (uintptr_t)g_stack, second_view = first_view + 0x40000;
    assert(shared_map(handle, first_view, 0x40000, 1, 0) == 0xe401);
    assert(shared_map(shared, first_view + 1, 0x40000, 1, 0) == 0xcc01);
    if (getpagesize() > 0x1000) assert(shared_map(shared, first_view + 0x1000, 0x40000, 1, 0) == 0xd401);
    assert(shared_map(shared, first_view, 0x1000, 1, 0) == 0xca01);
    assert(shared_map(shared, first_view, 0x40000, 3, 0) == 0xd801);
    assert(shared_map(shared, (uintptr_t)g_heap, 0x40000, 1, 0) == 0xd401); // Even an unopened heap interval is reserved for heap growth.
    assert(!shared_map(shared, first_view, 0x40000, 1, 0));
    assert(shared_map(shared, first_view, 0x40000, 1, 0) == 0xd401);
    assert(!shared_map(shared, second_view, 0x40000, 1, 0));
    uint64_t shared_query[31] = {(uintptr_t)mi, 0, first_view}; svc_dispatch(6, shared_query, 0);
    assert(((uint32_t *)mi)[4] == 6 && ((uint32_t *)mi)[6] == 1);
    vm_region_basic_info_data_64_t region_info;
    mach_msg_type_number_t region_count = VM_REGION_BASIC_INFO_COUNT_64;
    vm_address_t region_address = first_view; vm_size_t region_size; mach_port_t region_object;
    assert(vm_region_64(mach_task_self(), &region_address, &region_size, VM_REGION_BASIC_INFO_64,
                       (vm_region_info_t)&region_info, &region_count, &region_object) == KERN_SUCCESS);
    assert(region_info.protection == VM_PROT_READ); // Kernel-enforced, not just QueryMemory metadata.
    if (region_object) mach_port_deallocate(mach_task_self(), region_object);
    pthread_mutex_lock(&g_hid_service_lock);
    uint64_t *producer = dm_memory_data(g_hid_resources[0].memory);
    __atomic_store_n(producer, 0x12345678, __ATOMIC_RELEASE);
    assert(__atomic_load_n((uint64_t *)first_view, __ATOMIC_ACQUIRE) == 0x12345678 &&
           __atomic_load_n((uint64_t *)second_view, __ATOMIC_ACQUIRE) == 0x12345678); // Aliases, not snapshots.
    pthread_mutex_unlock(&g_hid_service_lock);
    assert(!close_memory(shared) && close_memory(shared) == 0xe401);
    assert(!shared_map(shared_copy, first_view, 0x40000, 0, 1));
    assert(!shared_map(shared_copy, second_view, 0x40000, 0, 1));
    assert(shared_map(shared_copy, second_view, 0x40000, 0, 1) == 0xd401);
    assert(!close_memory(shared_copy));
    uint32_t malformed[16] = {0xfff00006, 16};
    assert(h_sf_Send(session, malformed, sizeof malformed) == 0xe401); // Descriptors exceed the actual message.
    memset(malformed, 0, sizeof malformed); malformed[0] = 6; malformed[1] = 0x80000010; malformed[2] = 0x3ff;
    assert(h_sf_Send(session, malformed, sizeof malformed) == 0xe401); // Oversized special header.
    uint32_t domain = 0xdead;
    assert(h_sf_ToDomain(&domain, session) != 0 && domain == 0xdead);
    uint32_t unsupported;
    assert(h_sf_Connect(&unsupported, "unknown") != 0);
    h_sf_Close(applet);
    h_sf_Close(session);
    uint32_t backup;
    assert(!h_sf_Connect(&backup, "olsc:u") && !request(backup, 0, 128, 0, 0));
    h_sf_Close(backup); // Existing backup-off policy remains separate from strict HID.
    assert(!g_nmap);
    assert(!munmap(g_as, RESERVE)); free(g_alias_bits); free(g_alias_exposed);
    puts("HID IPC and transfer-memory ownership passed");
    return 0;
}
