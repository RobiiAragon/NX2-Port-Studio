// SPDX-License-Identifier: GPL-3.0-or-later
// Authored buffer/VM/lifetime proof only; no game bytes or image aliases.
#define DM_GAME_NVN2 1
#define nvn_game_fps nvn_game_fps_mtl_default
#include "../nvn/nvn_mtl.m"
#undef nvn_game_fps
static int refuse_owned_buffer;
static int fixture_pool_add_owned(const void *memory,uint64_t size,void *owner,void (*release)(void *)) {
    if (refuse_owned_buffer) { release(owner); return 0; }
    return mtl_pool_add_owned(memory,size,owner,release);
}
#define mtl_pool_add_owned fixture_pool_add_owned
#define g_ft g_ft_hle
#include "../nvn/hle_nvn.c"
#undef g_ft
#undef mtl_pool_add_owned
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
enum { PAGE=65536 };
static unsigned checks,failed;
static int check(int value,const char *message) { ++checks; if (!value) { ++failed; fprintf(stderr,"FAIL: %s\n",message); } return value; }
#define CHECK(c,m) check(!!(c),m)
#define REQUIRE(c,m) do { if (!CHECK(c,m)) exit(2); } while (0)
static int resolves(const void *memory,uint64_t size) { void *buffer=NULL; uint64_t offset=0; return mtl_addr((uintptr_t)memory,size,&buffer,&offset); }
static unsigned pins(pool_t *pool) {
    pthread_mutex_lock(&g_vp_mu); nvn2_source *source=nvn2_source_find(pool);
    unsigned result=source?atomic_load(&source->pins):0; pthread_mutex_unlock(&g_vp_mu); return result;
}
static int reserved(pool_t *pool) {
    pthread_mutex_lock(&g_vp_mu); int result=vpool_of(pool)!=NULL; pthread_mutex_unlock(&g_vp_mu); return result;
}
static int pool_init(pool_t *pool,void *memory,uint64_t size,unsigned flags) {
    pool_builder_t builder={.mem=memory,.size=size,.flags=flags};
    return h_nvnMemoryPoolInitialize((NVNmemoryPool *)pool,(NVNmemoryPoolBuilder *)&builder);
}
static void refused(pool_t *pool,map_req request,enum nvn2_alias_result expected) {
    struct vpool *v=vpool_of(pool); REQUIRE(v && !v->nrun && !v->n2,"negative test starts with an empty reservation");
    mtl_res_count before,after; mtl_res_snapshot(&before); uint64_t generation=nvn_sparse_gen();
    CHECK(nvn2_alias_fresh(pool,1,&request)==expected,"invalid mapping returns its explicit status");
    mtl_res_snapshot(&after);
    CHECK(!v->nrun && !v->n2 && !nvn_host_readable((uintptr_t)v->base,v->size),"failed mapping publishes no accessible run");
    CHECK(!resolves(v->base,1) && generation==nvn_sparse_gen(),"failed mapping leaves GPU/page generations unchanged");
    CHECK(before.pools==after.pools && before.pool_adds==after.pool_adds,"failed mapping leaves Metal registry unchanged");
}
static void refuses_live_reinitialize(pool_t *physical,pool_t *virtual_pool) {
    pool_t physical_before=*physical, virtual_before=*virtual_pool;
    nvn2_source *source=nvn2_source_find(physical); struct vpool *view=vpool_of(virtual_pool);
    REQUIRE(source && view && view->n2 && atomic_load(&source->pins)==1,"reinitialization regression starts with a pinned source and mapped view");
    struct vpool view_before=*view;
    uint64_t generation=source->generation, map_generation=nvn_sparse_gen();
    void *buffer=NULL; uint64_t offset=0; uint8_t *alias=virtual_pool->gpu_mem+PAGE;
    REQUIRE(mtl_addr((uintptr_t)alias,PAGE,&buffer,&offset),"original GPU view resolves before duplicate Initialize");
    uint8_t *bytes=malloc(physical->size); REQUIRE(bytes,"physical snapshot allocated"); memcpy(bytes,physical->mem,physical->size);
    int live,used; uint64_t adds; nvn_dirty_slot_stats(&live,&used,&adds);
    mtl_res_count metal_before; mtl_res_snapshot(&metal_before);
    // Deliberately cross the builder policies: identity protection must not
    // depend on the requested new kind or already-mutable guest object bytes.
    pool_builder_t replacement[2]={{.size=PAGE,.flags=0x2a1},{.mem=physical->mem,.size=physical->size,.flags=0x18c}};
    pool_t *target[2]={physical,virtual_pool};
    for (unsigned i=0;i<2;++i) {
        CHECK(!h_nvnMemoryPoolInitialize((NVNmemoryPool *)target[i],(NVNmemoryPoolBuilder *)&replacement[i]),"duplicate Initialize refuses a registered identity");
        CHECK(!memcmp(physical,&physical_before,sizeof *physical) && !memcmp(virtual_pool,&virtual_before,sizeof *virtual_pool),"duplicate Initialize preserves every original pool byte");
        CHECK(nvn2_source_find(physical)==source && source->generation==generation && atomic_load(&source->pins)==1 && atomic_load(&source->aliases)==1,"physical source ledger/generation/pin are unchanged");
        CHECK(vpool_of(virtual_pool)==view && !memcmp(view,&view_before,sizeof *view) && nvn_sparse_gen()==map_generation,"mapped reservation and mapping generation are unchanged");
        void *after_buffer=NULL; uint64_t after_offset=0;
        CHECK(mtl_addr((uintptr_t)alias,PAGE,&after_buffer,&after_offset) && after_buffer==buffer && after_offset==offset,"original GPU buffer and offset survive duplicate Initialize");
        int after_live,after_used; uint64_t after_adds; nvn_dirty_slot_stats(&after_live,&after_used,&after_adds);
        CHECK(after_live==live && after_used==used && after_adds==adds,"duplicate Initialize never unregisters or adds dirty tracking");
        mtl_res_count metal_after; mtl_res_snapshot(&metal_after);
        CHECK(metal_after.pools==metal_before.pools && metal_after.pool_adds==metal_before.pool_adds,"duplicate Initialize leaves Metal registry/allocation counters intact");
        CHECK(!memcmp(bytes,physical->mem,physical->size) && !memcmp(alias,bytes+PAGE,PAGE),"physical bytes and shared alias content remain intact");
    }
    free(bytes);
}
static void gpu_copy_read(pool_t *physical,uint8_t *alias) { @autoreleasepool {
    void *raw=NULL; uint64_t offset=0;
    REQUIRE(mtl_addr((uintptr_t)alias,PAGE,&raw,&offset),"mapped view resolves an actual no-copy buffer");
    id<MTLBuffer> view=(__bridge id<MTLBuffer>)raw;
    id<MTLBuffer> readback=[g_dev newBufferWithLength:PAGE options:MTLResourceStorageModeShared];
    REQUIRE(readback,"independent Metal readback allocated"); res_add(readback); res_commit();
    id<MTLCommandBuffer> command=[g_q commandBuffer]; id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
    [blit copyFromBuffer:view sourceOffset:offset toBuffer:readback destinationOffset:0 size:PAGE];
    [blit endEncoding]; [command commit]; [command waitUntilCompleted];
    REQUIRE(command.status==MTLCommandBufferStatusCompleted && !command.error,"CPU-written alias copied by real GPU without error");
    CHECK(!memcmp(readback.contents,physical->mem+PAGE,PAGE),"CPU physical writes are visible through Metal alias");
    res_remove(readback); res_commit();
} }
static void release_io(void *memory) { munmap(memory,PAGE); }
static void replay_copy(pool_t *physical,uint8_t *alias) {
    uint8_t *io=mmap(NULL,PAGE,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    REQUIRE(io!=MAP_FAILED && mtl_pool_add_owned(io,PAGE,io,release_io),"independent owned replay IO buffer allocated");
    mtl_sub_begin(); mtl_sub_copy((uintptr_t)alias,(uintptr_t)io,PAGE);
    uint64_t fence=mtl_sub_commit();
    REQUIRE(mtl_wait(fence,UINT64_MAX) && !mtl_last_commit_error(),"real Metal 4 replay copy completes successfully");
    CHECK(!memcmp(io,physical->mem+PAGE,PAGE),"shared replay reads physical CPU writes through virtual alias");
    for (unsigned i=0;i<PAGE;++i) io[i]=(uint8_t)(i*13+(i>>7)+31);
    mtl_sub_begin(); mtl_sub_copy((uintptr_t)io,(uintptr_t)alias,PAGE); fence=mtl_sub_commit();
    REQUIRE(mtl_wait(fence,UINT64_MAX) && !mtl_last_commit_error(),"real Metal 4 replay writes through virtual alias");
    CHECK(!memcmp(io,physical->mem+PAGE,PAGE),"shared replay GPU writes reach physical CPU storage");
    CHECK(nvn_sparse_since(physical,(uintptr_t)alias,PAGE,nvn_sparse_gen(),nvn_dirty_now()),"GPU-writable alias cannot produce a false clean dirty result");
    mtl_pool_del_owned(io);
}
static _Atomic int finalize_started,finalize_done;
static void *finish_virtual(void *pointer) { @autoreleasepool {
    atomic_store(&finalize_started,1);
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)pointer);
    atomic_store(&finalize_done,1); return NULL;
} }
static void lifetime_gpu(pool_t *physical,pool_t *virtual_pool,nvn2_alias_lease *borrow) { @autoreleasepool {
    uint8_t *alias=virtual_pool->gpu_mem+PAGE; void *raw=NULL; uint64_t offset=0;
    REQUIRE(mtl_addr((uintptr_t)alias,PAGE,&raw,&offset),"lifetime view resolves before Finalize");
    id<MTLBuffer> view=(__bridge id<MTLBuffer>)raw;
    id<MTLSharedEvent> ready=[g_dev newSharedEvent];
    id<MTLCommandBuffer> command=[g_q commandBuffer]; [command encodeWaitForEvent:ready value:1];
    id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
    [blit fillBuffer:view range:NSMakeRange(offset,PAGE) value:0xa6]; [blit endEncoding]; [command commit];
    pthread_t thread; REQUIRE(!pthread_create(&thread,NULL,finish_virtual,virtual_pool),"Finalize thread started");
    uint64_t until=bp_now()+5000000000ull;
    while ((!atomic_load(&finalize_started) || reserved(virtual_pool)) && bp_now()<until) sched_yield();
    REQUIRE(!reserved(virtual_pool),"Finalize closes registry before waiting for blocked GPU");
    CHECK(!atomic_load(&finalize_done),"Finalize waits for actual GPU completion");
    CHECK(pins(physical)==1 && nvn2_source_close(physical,0)==NVN2_ALIAS_PINNED,"in-flight alias pins physical ownership");
    CHECK(nvn2_alias_flush_lease(borrow,9,7),"borrowed flush may complete after public view closes");
    CHECK(nvn_flushed(physical->mem+PAGE+9,7) && !nvn_flushed(alias+9,7),"late flush publishes physical bytes without stale virtual span");
    CHECK(nvn2_readable_extent(alias)>=PAGE,"borrow keeps entire old VA alive while Finalize waits");
    ready.signaledValue=1; REQUIRE(!pthread_join(thread,NULL),"Finalize completes after GPU release");
    [command waitUntilCompleted]; REQUIRE(command.status==MTLCommandBufferStatusCompleted && !command.error,"GPU write completed before destruction");
    CHECK(atomic_load(&finalize_done) && !resolves(alias,1),"Finalize removes Metal lookup state after completion");
    int correct=1; for (unsigned i=0;i<PAGE;++i) correct&=physical->mem[PAGE+i]==0xa6;
    CHECK(correct,"real GPU alias write is visible in original physical CPU storage");
    CHECK(nvn2_readable_extent(alias)>=PAGE && pins(physical)==1,"CPU borrow preserves retired mapping and pin after GPU completion");
} }
static void consumed(void *pointer) { ++*(unsigned *)pointer; }
static void retained_buffer_lifetime(pool_t *physical,pool_t *virtual_pool) { @autoreleasepool {
    map_req request={.phys=(NVNmemoryPool *)physical,.poff=PAGE,.voff=PAGE,.size=PAGE,.storage_class=0x4004};
    REQUIRE(nvn2_alias_fresh(virtual_pool,1,&request)==NVN2_ALIAS_OK,"fresh view is possible after previous alias fully retires");
    uint8_t *alias=virtual_pool->gpu_mem+PAGE; void *raw=NULL; uint64_t offset=0;
    REQUIRE(mtl_addr((uintptr_t)alias,PAGE,&raw,&offset),"retained-only Metal view resolves");
    id<MTLBuffer> retained=(__bridge id<MTLBuffer>)raw;
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)virtual_pool);
    CHECK(retained.contents==alias && nvn2_readable_extent(alias)>=PAGE,"retained Metal object keeps VA alive independently of CPU/registry leases");
    CHECK(nvn2_source_close(physical,0)==NVN2_ALIAS_PINNED,"retained Metal object alone pins physical source");
} }
int main(int argc,char **argv) { @autoreleasepool {
    struct rlimit no_core={0,0}; REQUIRE(!setrlimit(RLIMIT_CORE,&no_core),"core dumps disabled");
    // A fresh process tests the fatal wrapper before Metal initializes; never
    // fork an initialized Metal runtime for a death test.
    if (argc==2 && !strcmp(argv[1],"--unsupported-count")) {
        h_nvnMemoryPoolMapVirtual(NULL,2,NULL); return 3;
    }
    crash_install(); mtl_init();
    CHECK(nvn2_common_abi_verified("nvnMemoryPoolMapVirtual"),"MapVirtual fresh-buffer adapter is admitted after independent primitive proof");
    uint8_t *memory=mmap(NULL,2*PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    REQUIRE(memory!=MAP_FAILED,"synthetic physical memory allocated");
    for (unsigned i=0;i<2*PAGE;++i) memory[i]=(uint8_t)(i*29+17+(i>>8));
    pool_t physical,virtual_pool,second;
    REQUIRE(pool_init(&physical,memory,2*PAGE,0x18c),"true GPU_NO_ACCESS physical source initialized");
    REQUIRE(pool_init(&virtual_pool,NULL,3*PAGE,0x2a1),"small inaccessible virtual range initialized");
    REQUIRE(pool_init(&second,NULL,3*PAGE,0x2a1),"second empty virtual range initialized");
    uint8_t *base=virtual_pool.gpu_mem,*alias=base+PAGE;
    map_req request={.phys=(NVNmemoryPool *)&physical,.poff=PAGE,.voff=PAGE,.size=PAGE,.storage_class=0x4004};
    map_req bad=request; bad.poff=-PAGE; refused(&virtual_pool,bad,NVN2_ALIAS_INVALID);
    bad=request; bad.voff=3*PAGE; refused(&virtual_pool,bad,NVN2_ALIAS_INVALID);
    bad=request; bad.size=2*PAGE; refused(&virtual_pool,bad,NVN2_ALIAS_INVALID);
    bad=request; bad.poff=4096; refused(&virtual_pool,bad,NVN2_ALIAS_INVALID);
    bad=request; bad.storage_class=0x4321; refused(&virtual_pool,bad,NVN2_ALIAS_UNSUPPORTED);
    bad=request; bad.phys=NULL; refused(&virtual_pool,bad,NVN2_ALIAS_UNSUPPORTED);
    bad=request; bad.size=0; refused(&virtual_pool,bad,NVN2_ALIAS_UNSUPPORTED);
    ++physical.reserved1; refused(&virtual_pool,request,NVN2_ALIAS_INVALID); --physical.reserved1;
    CHECK(nvn2_alias_fresh(&virtual_pool,2,&request)==NVN2_ALIAS_UNSUPPORTED,"array mappings remain closed");
    refuse_owned_buffer=1; refused(&virtual_pool,request,NVN2_ALIAS_OS_ERROR); refuse_owned_buffer=0;
    CHECK(!pins(&physical),"Metal-refusal rollback releases physical pin");
    // An actual missing VM source must fail before publishing any metadata.
    uint8_t *missing=mmap(NULL,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0); pool_t absent;
    REQUIRE(missing!=MAP_FAILED && pool_init(&absent,missing,PAGE,0x18c),"temporary physical object registered");
    REQUIRE(!munmap(missing,PAGE),"source VA removed to exercise real vm_remap failure");
    bad=request; bad.phys=(NVNmemoryPool *)&absent; bad.poff=0;
    refused(&virtual_pool,bad,NVN2_ALIAS_OS_ERROR);
    CHECK(!pins(&absent),"OS-failure rollback releases source pin");
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&absent);
    unsigned releases=0; CHECK(!mtl_pool_add_owned(memory+1,PAGE,&releases,consumed) && releases==1,"invalid no-copy view consumes its owner exactly once");
    // Flush before Map is canonical, so the eventual alias must see it.
    h_nvnMemoryPoolFlushMappedRange((NVNmemoryPool *)&physical,PAGE+32,16);
    REQUIRE(h_nvnMemoryPoolMapVirtual((NVNmemoryPool *)&virtual_pool,1,(NVNmappingRequest *)&request),"real API creates actual OS alias and no-copy Metal buffer");
    CHECK(!physical.gpu_mem && !resolves(memory,4),"physical source never gains a direct GPU view");
    CHECK(nvn_host_readable((uintptr_t)alias,PAGE) && nvn_sparse_mapped(&virtual_pool,(uintptr_t)alias,PAGE),"entire mapped run is available");
    CHECK(!nvn_sparse_mapped(&second,(uintptr_t)alias,PAGE),"range availability requires the owning virtual pool identity");
    CHECK(!nvn_host_readable((uintptr_t)base,PAGE) && !resolves(base,PAGE),"leading hole remains inaccessible");
    CHECK(!nvn_host_readable((uintptr_t)(base+2*PAGE),PAGE) && !resolves(base+2*PAGE,PAGE),"trailing hole remains inaccessible");
    CHECK(!resolves(alias-16,32) && !resolves(alias+PAGE-16,32),"Metal cannot resolve across either hole");
    CHECK(!nvn_host_readable((uintptr_t)base-16,32),"host read starting outside reservation cannot cross into a hole");
    CHECK(!resolves(alias,UINT64_MAX) && !nvn_host_readable((uintptr_t)alias,UINT64_MAX),"overflowing ranges fail closed");
    CHECK(!memcmp(alias,memory+PAGE,PAGE),"VM alias preserves complete source content");
    refuses_live_reinitialize(&physical,&virtual_pool);
    CHECK(nvn_flushed(alias+32,16) && !nvn_flushed(alias+64,16),"pre-map physical flush is visible at exact alias offset");
    h_nvnMemoryPoolFlushMappedRange((NVNmemoryPool *)&physical,PAGE-8,16);
    CHECK(nvn_flushed(alias,8) && !nvn_flushed(alias+8,8),"later flush intersects mapped range without expanding its extent");
    CHECK(!nvn_flushed(base+PAGE-8,8),"physical flush never publishes a virtual hole");
    CHECK(nvn2_alias_fresh(&virtual_pool,1,&request)==NVN2_ALIAS_UNSUPPORTED,"remapping an occupied reservation remains closed");
    CHECK(nvn2_alias_fresh(&second,1,&request)==NVN2_ALIAS_UNSUPPORTED,"duplicate physical alias remains closed");
    uint64_t stamp=nvn_dirty_now(); nvn_sparse_protect(&virtual_pool,(uintptr_t)alias,PAGE);
    memory[PAGE+61]^=0x49;
    CHECK(alias[61]==memory[PAGE+61] && nvn_sparse_since(&virtual_pool,(uintptr_t)alias,PAGE,nvn_sparse_gen(),stamp),"physical CPU write is coherent and canonical dirty check cannot miss it");
    gpu_copy_read(&physical,alias);
    // TSan lease lane still runs real classic Metal reads/writes and the
    // concurrent destructor, but excludes inherited replay lazy initialization.
    // The complete regular and ASan lanes always exercise Metal 4 replay too.
    if (!(argc==2 && !strcmp(argv[1],"--leases-only"))) replay_copy(&physical,alias);
    CHECK(nvn2_source_close(&physical,0)==NVN2_ALIAS_PINNED,"source Finalize is refused while live alias owns backing");
    nvn2_alias_lease *borrow=NULL; const uint8_t *canonical=NULL;
    REQUIRE(nvn2_alias_acquire((uintptr_t)alias,PAGE,&borrow,&canonical)==NVN2_ALIAS_OK && canonical==memory+PAGE,"CPU borrower acquires canonical source lease");
    CHECK(!nvn2_alias_flush_lease(borrow,PAGE-1,2),"borrowed flush rejects an overflowing range");
    lifetime_gpu(&physical,&virtual_pool,borrow);
    CHECK(nvn2_source_close(&physical,0)==NVN2_ALIAS_PINNED,"physical ownership remains pinned until final CPU borrower releases");
    nvn2_alias_release(borrow);
    CHECK(!pins(&physical) && nvn2_source_close(&physical,0)==NVN2_ALIAS_OK,"all buffer/CPU leases release physical pin without a future submit");
    CHECK(nvn2_readable_extent(alias)==0 && !resolves(alias,4),"actual no-copy destruction unmaps retired reservation");
    uint8_t *retired=second.gpu_mem+PAGE;
    retained_buffer_lifetime(&physical,&second);
    CHECK(!pins(&physical) && !nvn2_readable_extent(retired),"last Metal reference releases physical pin and VA without another submit");
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&physical);
    CHECK(!nvn_dirty_tracked(memory,2*PAGE),"physical Finalize removes dirty ownership after aliases die");
    munmap(memory,2*PAGE);
    fprintf(stderr,"NVN2 alias: %u checks, %u failed; real GPU buffer copies/writes, no images\n",checks,failed);
    return failed?1:0;
} }
