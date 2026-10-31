// SPDX-License-Identifier: GPL-3.0-or-later
// Current NVN2 reservation boundary only. No game, SDK or display surface.
// MapVirtual is not called here: its fresh buffer subset has a separate fixture.
// This test proves reservation boundaries, not GPU/texture alias coherence.
#define DM_GAME_NVN2 1
#define nvn_game_fps nvn_game_fps_mtl_default
#include "../nvn/nvn_mtl.m"
#undef nvn_game_fps
#define g_ft g_ft_hle
#include "../nvn/hle_nvn.c"
#undef g_ft
#include <sys/resource.h>

uint64_t g_t0_ns;
_Atomic uint64_t dm_present_ns;
module_t g_mod[MAX_MOD]; int g_nmod;
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
void svc_thread_dump(void) {}
int dm_guest_as_contains(uintptr_t p) { (void)p; return 0; }
uint64_t now_ns(void) { return bp_now(); }
void crash_install(void);

enum { PAGE = 65536 };
static unsigned checks, failed;
static int check(int yes, const char *message) {
    ++checks;
    if (!yes) { ++failed; fprintf(stderr, "FAIL: %s\n", message); }
    return yes;
}
#define CHECK(c, message) check(!!(c), message)
#define REQUIRE(c, message) do { if (!CHECK(c, message)) exit(2); } while (0)
static int resolves(const void *address, size_t size) {
    void *buffer = NULL; uint64_t offset = 0;
    return mtl_addr((uintptr_t)address, size, &buffer, &offset);
}
static vm_prot_t protection(const void *pointer) {
    vm_address_t address = (uintptr_t)pointer; vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t result = vm_region_64(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &object);
    if (object) mach_port_deallocate(mach_task_self(), object);
    REQUIRE(result == KERN_SUCCESS && (uintptr_t)pointer >= address && (uintptr_t)pointer - address < size,
            "VM query covers tested address");
    return info.protection;
}
static unsigned reservations(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < NVPOOL; ++i) count += g_vp[i].base != NULL;
    return count;
}
static void rejected(pool_builder_t builder) {
    pool_t pool; memset(&pool, 0xa5, sizeof pool);
    unsigned count = reservations();
    mtl_res_count before, after; mtl_res_snapshot(&before);
    CHECK(!h_nvnMemoryPoolInitialize((NVNmemoryPool *)&pool, (NVNmemoryPoolBuilder *)&builder), "unsupported reservation is refused");
    mtl_res_snapshot(&after);
    CHECK(!pool.magic && !pool.mem && !pool.gpu_mem && !pool.size, "failed reservation publishes no usable object");
    CHECK(reservations() == count && !vpool_of(&pool), "failed reservation leaves no partial VM registry entry");
    CHECK(before.pools == after.pools && before.pool_adds == after.pool_adds, "failed reservation creates no Metal allocation");
}
int main(void) { @autoreleasepool {
    struct rlimit no_core = {0, 0}; REQUIRE(!setrlimit(RLIMIT_CORE, &no_core), "core dumps disabled");
    crash_install(); mtl_init();
    CHECK(nvn2_common_abi_verified("nvnMemoryPoolMapVirtual"), "MapVirtual fresh-buffer adapter is audited");
    nvn_init_slots();
    unsigned slot = 0;
    while (slot < NVN_NFUNC && strcmp(nvn_names[slot], "nvnMemoryPoolMapVirtual")) ++slot;
    REQUIRE(slot < NVN_NFUNC, "MapVirtual has a resolver entry");
    CHECK(nvn_slot[slot].fn == (void *)h_nvnMemoryPoolMapVirtual ||
          (nvn_slot[slot].fn == (void *)nvn_big_entry && nvn_big_real[slot] == (void *)h_nvnMemoryPoolMapVirtual),
          "resolver selects the audited fresh-buffer adapter on its host stack");
    uint8_t *memory = mmap(NULL, 2 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    REQUIRE(memory != MAP_FAILED, "synthetic physical backing allocated");
    for (unsigned i = 0; i < 2 * PAGE; ++i) memory[i] = (uint8_t)(17 + i * 29 + (i >> 8));
    pool_builder_t physical_builder = {.mem = memory, .size = 2 * PAGE, .flags = 0x18c};
    pool_t physical;
    mtl_res_count before, after; mtl_res_snapshot(&before);
    REQUIRE(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&physical, (NVNmemoryPoolBuilder *)&physical_builder), "physical source initialized");
    mtl_res_snapshot(&after);
    CHECK(before.pool_adds == after.pool_adds && before.pools == after.pools, "GPU_NO_ACCESS physical source creates no Metal buffer");
    CHECK(h_nvnMemoryPoolMap((NVNmemoryPool *)&physical) == memory, "physical CPU map is the actual backing");
    CHECK(!physical.gpu_mem && !h_nvnMemoryPoolGetBufferAddress((NVNmemoryPool *)&physical), "physical source has no GPU address");
    CHECK(!resolves(memory, 4), "physical CPU storage cannot resolve to a Metal buffer");
    CHECK(nvn_dirty_tracked(memory, 2 * PAGE), "physical source remains tracked for CPU writes");
    buf_builder_t buffer_builder = {.pool = &physical, .off = PAGE, .size = 256}; buf_t buffer;
    REQUIRE(h_nvnBufferInitialize((NVNbuffer *)&buffer, (NVNbufferBuilder *)&buffer_builder), "CPU-only buffer descriptor initialized");
    CHECK(h_nvnBufferMap((NVNbuffer *)&buffer) == memory + PAGE && !h_nvnBufferGetAddress((NVNbuffer *)&buffer), "physical buffer preserves CPU access without manufacturing a GPU view");
    h_nvnBufferFinalize((NVNbuffer *)&buffer);
    pool_builder_t virtual_builder = {.size = 3 * PAGE, .flags = 0x2a1}; pool_t virtual_pool;
    mtl_res_snapshot(&before);
    REQUIRE(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&virtual_pool, (NVNmemoryPoolBuilder *)&virtual_builder), "small virtual reservation initialized");
    mtl_res_snapshot(&after);
    uint8_t *base = virtual_pool.gpu_mem;
    REQUIRE(base && base != memory && !((uintptr_t)base & (PAGE - 1)), "virtual reservation owns a distinct 64 KiB-aligned address range");
    struct vpool *view = vpool_of(&virtual_pool);
    CHECK(view && view->len == 3 * PAGE && view->size == 3 * PAGE && !view->nrun, "entire small reservation is tracked without mapped runs");
    CHECK(before.pool_adds == after.pool_adds && before.pools == after.pools, "inaccessible reservation creates no Metal buffer");
    CHECK(!h_nvnMemoryPoolMap((NVNmemoryPool *)&virtual_pool), "CPU_NO_ACCESS reservation has no public CPU mapping");
    for (unsigned page = 0; page < 3; ++page) {
        CHECK(protection(base + page * PAGE) == VM_PROT_NONE, "reserved page remains physically inaccessible");
        CHECK(!nvn_host_readable((uintptr_t)(base + page * PAGE), PAGE), "unmapped page is not host-readable");
        CHECK(!resolves(base + page * PAGE, PAGE), "unmapped page cannot resolve to Metal storage");
    }
    CHECK(!resolves(base + PAGE - 16, 32), "lookup crossing a hole boundary remains unavailable");
    rejected((pool_builder_t){.size = 0, .flags = 0x2a1});
    rejected((pool_builder_t){.size = PAGE + 4096, .flags = 0x2a1});
    rejected((pool_builder_t){.mem = memory, .size = PAGE, .flags = 0x2a1});
    rejected((pool_builder_t){.size = PAGE, .flags = 0x2a0}); // CPU policy missing.
    rejected((pool_builder_t){.size = PAGE, .flags = 0x281}); // GPU policy missing.
    rejected((pool_builder_t){.size = PAGE, .flags = 0x3a1}); // PHYSICAL|VIRTUAL conflict.
    rejected((pool_builder_t){.size = (64ull << 30) + PAGE, .flags = 0x2a1});
    CHECK(view == vpool_of(&virtual_pool) && !view->nrun, "invalid reservations never modify the live reservation");
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&virtual_pool);
    CHECK(!vpool_of(&virtual_pool) && !resolves(base, PAGE), "reservation teardown removes VM and Metal lookup state");
    CHECK(h_nvnMemoryPoolMap((NVNmemoryPool *)&physical) == memory && memory[PAGE + 37] == (uint8_t)(17 + (PAGE + 37) * 29 + ((PAGE + 37) >> 8)), "unmapped virtual teardown leaves physical bytes intact");
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&physical);
    CHECK(!nvn_dirty_tracked(memory, 2 * PAGE) && !resolves(memory, 4), "physical finalization removes tracking without a GPU allocation");
    REQUIRE(!munmap(memory, 2 * PAGE), "synthetic backing released");
    printf("t_nvn2_vpool: %u checks, %u failed; reservation/GPU_NO_ACCESS guards only; no MapVirtual call\n", checks, failed);
    return failed != 0;
} }
