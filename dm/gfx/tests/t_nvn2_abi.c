// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only fixtures for audited NVN2 object bytes. No game or SDK is required.
#ifndef DM_GAME_NVN2
#define DM_GAME_NVN2 1
#endif
#include "../nvn/hle_nvn.c"
uint64_t g_t0_ns;
_Atomic uint64_t dm_present_ns;
module_t g_mod[MAX_MOD]; int g_nmod;
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
void svc_thread_dump(void) {}
int dm_guest_as_contains(uintptr_t p) { (void)p; return 0; }
uint64_t now_ns(void) { return 0; }

static uint32_t u32(const uint8_t *p) { uint32_t x; memcpy(&x,p,4); return x; }
static uint64_t u64(const uint8_t *p) { uint64_t x; memcpy(&x,p,8); return x; }
static float f32(const uint8_t *p) { float x; memcpy(&x,p,4); return x; }
static void canary(const uint8_t *p, size_t end) { for(size_t i=end;i<end+16;i++) assert(p[i]==0xa5); }

struct ctl_fixture { unsigned calls; _Alignas(8) uint8_t memory[272]; };
static void control_callback(NVNcommandBuffer *c, NVNcommandBufferMemoryEvent event, size_t min, void *data) {
    struct ctl_fixture *fixture=data; assert(event==NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_CONTROL_MEMORY && min==256);
    ++fixture->calls; h_nvnCommandBufferAddControlMemory(c,fixture->memory,256);
}

// Optional synthetic Metal round trip: no game data, shaders or display surface.
// Copy/readback are called directly to exercise the shared backend; their guest
// NVN2 entry points remain unadmitted until separately audited.
static void texture_gpu_fixture(void) {
    dev_builder_t db={0}; dev_t_ device;
    h_nvnDeviceBuilderSetDefaults((NVNdeviceBuilder *)&db);
    assert(h_nvnDeviceInitialize((NVNdevice *)&device,(const NVNdeviceBuilder *)&db));
    void *memory=NULL; assert(!posix_memalign(&memory,16384,65536)); memset(memory,0,65536);
    pool_builder_t pb={0}; pool_t pool;
    h_nvnMemoryPoolBuilderSetDevice((NVNmemoryPoolBuilder *)&pb,(NVNdevice *)&device);
    h_nvnMemoryPoolBuilderSetDefaults((NVNmemoryPoolBuilder *)&pb);
    h_nvnMemoryPoolBuilderSetStorage((NVNmemoryPoolBuilder *)&pb,memory,65536);
    h_nvnMemoryPoolBuilderSetFlags((NVNmemoryPoolBuilder *)&pb,0x22);
    assert(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&pool,(const NVNmemoryPoolBuilder *)&pb));
    nvn2_tex_builder builder={0}; tex_t texture;
    n2_nvnTextureBuilderSetDevice((NVNtextureBuilder *)&builder,(NVNdevice *)&device);
    n2_nvnTextureBuilderSetDefaults((NVNtextureBuilder *)&builder);
    n2_nvnTextureBuilderSetSize2D((NVNtextureBuilder *)&builder,8,4);
    n2_nvnTextureBuilderSetFormat((NVNtextureBuilder *)&builder,NVN_FORMAT_RGBA8);
    n2_nvnTextureBuilderSetStorage((NVNtextureBuilder *)&builder,(NVNmemoryPool *)&pool,0);
    memset(&texture,0,sizeof texture);
    assert(n2_nvnTextureInitialize((NVNtexture *)&texture,(const NVNtextureBuilder *)&builder));
    cmd_t command; _Alignas(8) uint8_t control[256];
    h_nvnCommandBufferInitialize((NVNcommandBuffer *)&command,(NVNdevice *)&device);
    h_nvnCommandBufferAddCommandMemory((NVNcommandBuffer *)&command,(const NVNmemoryPool *)&pool,16384,16384);
    h_nvnCommandBufferAddControlMemory((NVNcommandBuffer *)&command,control,sizeof control);
    uint8_t *pixels=(uint8_t *)memory+32768;
    for(unsigned i=0;i<128;i++) pixels[i]=(uint8_t)(i*17+3);
    NVNcopyRegion region={0,0,0,8,4,1};
    h_nvnCommandBufferBeginRecording((NVNcommandBuffer *)&command);
    pool_builder_t empty_pb={.flags=0x21}; pool_t empty_pool;
    mtl_res_count before,after; mtl_res_snapshot(&before);
    assert(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&empty_pool,(const NVNmemoryPoolBuilder *)&empty_pb));
    mtl_res_snapshot(&after); assert(before.pool_adds==after.pool_adds);
    h_nvnCommandBufferSetShaderScratchMemory((NVNcommandBuffer *)&command,(NVNmemoryPool *)&pool,49152,16384);
    h_nvnCommandBufferSetShaderScratchMemory((NVNcommandBuffer *)&command,(NVNmemoryPool *)&empty_pool,0,0);
    h_nvnCommandBufferSetShaderScratchMemory((NVNcommandBuffer *)&command,(NVNmemoryPool *)&pool,49152,16384);
    h_nvnCommandBufferSetShaderScratchMemory((NVNcommandBuffer *)&command,NULL,0,0);
    h_nvnCommandBufferSetCopyRowStride((NVNcommandBuffer *)&command,0);
    h_nvnCommandBufferCopyBufferToTexture((NVNcommandBuffer *)&command,(uintptr_t)pixels,(NVNtexture *)&texture,NULL,&region,0);
    NVNcommandHandle handle=h_nvnCommandBufferEndRecording((NVNcommandBuffer *)&command);
    queue_builder_t qb={0}; queue_t queue;
    h_nvnQueueBuilderSetDevice((NVNqueueBuilder *)&qb,(NVNdevice *)&device);
    h_nvnQueueBuilderSetDefaults((NVNqueueBuilder *)&qb);
    assert(h_nvnQueueInitialize((NVNqueue *)&queue,(const NVNqueueBuilder *)&qb));
    h_nvnQueueSubmitCommands((NVNqueue *)&queue,1,&handle); h_nvnQueueFinish((NVNqueue *)&queue);
    uint64_t scratch_address,scratch_size,scratch_records;
    mtl_nvn2_scratch_state(&scratch_address,&scratch_size,&scratch_records);
    assert(!scratch_address && !scratch_size && scratch_records==4);
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&empty_pool);
    uint8_t got[128]; memset(got,0,sizeof got);
    mtl_read_texels(&texture,0,0,&region,got,32,128);
    assert(!memcmp(got,pixels,sizeof got));
    // Both advertised guest row minima preserve bytes through the real Metal
    // staging/direct-view selection; no Metal alignment is silently clamped.
    for (int linear_rt=0;linear_rt<2;linear_rt++) {
        nvn2_tex_builder linear_builder=builder; tex_t linear={0};
        linear_builder.flags=linear_rt?0x40:0x10; linear_builder.stride=linear_rt?128:32;
        linear_builder.off=4096+linear_rt*4096;
        assert(n2_nvnTextureInitialize((NVNtexture *)&linear,(NVNtextureBuilder *)&linear_builder));
        int advertised=0; assert(nvn2_protocol_integer(linear_rt?0x39:0x38,&advertised));
        assert(linear.L.align==(unsigned)advertised && linear.L.layer_size==linear_builder.stride*4);
        h_nvnCommandBufferBeginRecording((NVNcommandBuffer *)&command);
        h_nvnCommandBufferCopyBufferToTexture((NVNcommandBuffer *)&command,(uintptr_t)pixels,(NVNtexture *)&linear,NULL,&region,0);
        handle=h_nvnCommandBufferEndRecording((NVNcommandBuffer *)&command);
        h_nvnQueueSubmitCommands((NVNqueue *)&queue,1,&handle); h_nvnQueueFinish((NVNqueue *)&queue);
        assert(!mtl_last_commit_error()); memset(got,0,sizeof got); mtl_read_texels(&linear,0,0,&region,got,32,128);
        assert(!memcmp(got,pixels,sizeof got)); h_nvnTextureFinalize((NVNtexture *)&linear);
    }
    // Real physical/CPU-only backing must not manufacture a directly GPU-
    // addressable Metal buffer. Only a later audited alias may expose it.
    void *physical_memory=NULL; assert(!posix_memalign(&physical_memory,16384,16384)); memset(physical_memory,0,16384);
    pool_builder_t physical_builder={.mem=physical_memory,.size=16384,.flags=0x18c}; pool_t physical_pool;
    mtl_res_snapshot(&before); assert(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&physical_pool,(NVNmemoryPoolBuilder *)&physical_builder));
    mtl_res_snapshot(&after); assert(after.pool_adds==before.pool_adds);
    assert(!h_nvnMemoryPoolGetBufferAddress((NVNmemoryPool *)&physical_pool) && h_nvnMemoryPoolMap((NVNmemoryPool *)&physical_pool)==physical_memory);
    void *physical_metal=NULL; uint64_t physical_offset=0;
    assert(!mtl_addr((uintptr_t)physical_memory,4,&physical_metal,&physical_offset));
    buf_builder_t physical_buffer_builder={.pool=&physical_pool,.off=4,.size=16}; buf_t physical_buffer;
    assert(h_nvnBufferInitialize((NVNbuffer *)&physical_buffer,(NVNbufferBuilder *)&physical_buffer_builder));
    assert(!h_nvnBufferGetAddress((NVNbuffer *)&physical_buffer) && h_nvnBufferMap((NVNbuffer *)&physical_buffer)==(uint8_t *)physical_memory+4);
    h_nvnBufferFinalize((NVNbuffer *)&physical_buffer); h_nvnMemoryPoolFinalize((NVNmemoryPool *)&physical_pool); free(physical_memory);
    // GPU-only COMPRESSIBLE color and promoted D24S8 attachment: the NVIDIA
    // compression/tag bytes are never read; Metal owns the attachment contents.
    void *rt_memory=NULL; assert(!posix_memalign(&rt_memory,16384,65536)); memset(rt_memory,0,65536);
    pool_builder_t rt_pb={.flags=0xa1,.mem=rt_memory,.size=65536}; pool_t rt_pool;
    assert(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&rt_pool,(const NVNmemoryPoolBuilder *)&rt_pb));
    nvn2_tex_builder rt_builder=builder; tex_t color={0},depth={0}; rt_builder.flags=8; rt_builder.pool=&rt_pool;
    assert(n2_nvnTextureInitialize((NVNtexture *)&color,(const NVNtextureBuilder *)&rt_builder));
    rt_builder.format=0x35; rt_builder.off=4096;
    assert(n2_nvnTextureInitialize((NVNtexture *)&depth,(const NVNtextureBuilder *)&rt_builder));
    h_nvnCommandBufferBeginRecording((NVNcommandBuffer *)&command);
    const NVNtexture *colors[]={(const NVNtexture *)&color};
    h_nvnCommandBufferSetRenderTargets((NVNcommandBuffer *)&command,1,colors,NULL,(NVNtexture *)&depth,NULL);
    const float green[]={0,1,0,1};
    h_nvnCommandBufferClearColor((NVNcommandBuffer *)&command,0,green,15);
    h_nvnCommandBufferClearDepthStencil((NVNcommandBuffer *)&command,.5f,1,0x5a,255);
    handle=h_nvnCommandBufferEndRecording((NVNcommandBuffer *)&command);
    h_nvnQueueSubmitCommands((NVNqueue *)&queue,1,&handle); h_nvnQueueFinish((NVNqueue *)&queue);
    assert(!mtl_last_commit_error()); mtl_read_texels(&color,0,0,&region,got,32,128);
    for(unsigned i=0;i<32;i++) assert(got[4*i]==0 && got[4*i+1]==255 && got[4*i+2]==0 && got[4*i+3]==255);
    h_nvnTextureFinalize((NVNtexture *)&color); h_nvnTextureFinalize((NVNtexture *)&depth);
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&rt_pool); free(rt_memory);
    h_nvnTextureFinalize((NVNtexture *)&texture); h_nvnCommandBufferFinalize((NVNcommandBuffer *)&command);
    h_nvnQueueFinalize((NVNqueue *)&queue); h_nvnMemoryPoolFinalize((NVNmemoryPool *)&pool);
    h_nvnDeviceFinalize((NVNdevice *)&device); free(memory);
    puts("t_nvn2_abi --metal: native builder -> Metal GPU copy -> readback: 128 bytes matched");
}

int main(int argc, char **argv) {
    _Alignas(16) uint8_t db[40], pb[72], sb[64], sampler[80], qb[88], queue[48];
    memset(db,0xa5,sizeof db);
    h_nvnDeviceBuilderSetDefaults((NVNdeviceBuilder *)db);
    for(int i=0;i<24;i++) assert(db[i]==0);
    h_nvnDeviceBuilderSetFlags((NVNdeviceBuilder *)db,0x12345678);
    assert(u32(db)==0 && u32(db+4)==0x12345678);
    assert(h_nvnDeviceBuilderGetFlags((const NVNdeviceBuilder *)db)==0x12345678);
    canary(db,24);

    memset(pb,0xa5,sizeof pb);
    h_nvnMemoryPoolBuilderSetDevice((NVNmemoryPoolBuilder *)pb,(NVNdevice *)(uintptr_t)0x12345000);
    h_nvnMemoryPoolBuilderSetDefaults((NVNmemoryPoolBuilder *)pb);
    assert(u64(pb+8)==0x12345000 && u32(pb+4)==0x21);
    assert(u64(pb+16)==0 && u64(pb+24)==0 && u32(pb+32)==0);
    assert(u64(pb+40)==0 && u64(pb+48)==0);
    h_nvnMemoryPoolBuilderSetStorage((NVNmemoryPoolBuilder *)pb,(void *)(uintptr_t)0x24680000,65536);
    h_nvnMemoryPoolBuilderSetFlags((NVNmemoryPoolBuilder *)pb,0x22);
    assert(u64(pb+16)==0x24680000 && u64(pb+24)==65536 && u32(pb+4)==0x22);
    canary(pb,56);

    memset(sb,0xa5,sizeof sb); memset(sampler,0xa5,sizeof sampler);
    h_nvnSamplerBuilderSetDevice((NVNsamplerBuilder *)sb,(NVNdevice *)(uintptr_t)0x12345000);
    h_nvnSamplerBuilderSetDefaults((NVNsamplerBuilder *)sb);
    assert(u64(sb)==0x12345000 && f32(sb+8)==0 && f32(sb+12)==1000 && f32(sb+20)==1);
    assert(u32(sb+44)==0x08110111); // wrap repeat, linear min/mag, compare LESS.
    h_nvnSamplerBuilderSetLodClamp((NVNsamplerBuilder *)sb,-2.5f,4.25f);
    h_nvnSamplerBuilderSetMinMagFilter((NVNsamplerBuilder *)sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
    h_nvnSamplerBuilderSetWrapMode((NVNsamplerBuilder *)sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_REPEAT,NVN_WRAP_MODE_REPEAT);
    assert(f32(sb+8)==-2.5f && f32(sb+12)==4.25f);
    assert((u32(sb+44)&0x00ff0000)==0);
    assert((u32(sb+44)&0xfff)==0x117);
    canary(sb,48);
    assert(h_nvnSamplerInitialize((NVNsampler *)sampler,(const NVNsamplerBuilder *)sb));
    assert(!memcmp(sampler+8,sb,48));
    assert(u64(sampler+56)==0); canary(sampler,64);
    NVNminFilter mi; NVNmagFilter ma; float lo,hi;
    h_nvnSamplerGetMinMagFilter((const NVNsampler *)sampler,&mi,&ma);
    h_nvnSamplerGetLodClamp((const NVNsampler *)sampler,&lo,&hi);
    assert(mi==NVN_MIN_FILTER_NEAREST && ma==NVN_MAG_FILTER_NEAREST && lo==-2.5f && hi==4.25f);

    memset(qb,0xa5,sizeof qb); memset(queue,0xa5,sizeof queue);
    h_nvnQueueBuilderSetDevice((NVNqueueBuilder *)qb,(NVNdevice *)(uintptr_t)0x12345000);
    h_nvnQueueBuilderSetDefaults((NVNqueueBuilder *)qb);
    assert(u64(qb+8)==0x12345000 && u32(qb+32)==0 && u32(qb+68)==100);
    assert(qb[36]==0 && qb[37]==0 && u32(qb+44)==0);
    assert(h_nvnQueueBuilderGetQueueMemorySize((const NVNqueueBuilder *)qb)==344064);
    h_nvnQueueBuilderSetCommandMemorySize((NVNqueueBuilder *)qb,65536);
    h_nvnQueueBuilderSetComputeMemorySize((NVNqueueBuilder *)qb,0);
    assert(qb[36]==1 && qb[37]==1 && u32(qb+44)==65536 && u32(qb+48)==0);
    assert(h_nvnQueueBuilderGetQueueMemorySize((const NVNqueueBuilder *)qb)==81920);
    h_nvnQueueBuilderSetQueueMemory((NVNqueueBuilder *)qb,(void *)(uintptr_t)0x24680000,81920);
    assert(u64(qb+16)==0x24680000 && u32(qb+60)==81920); canary(qb,72);
    assert(h_nvnQueueInitialize((NVNqueue *)queue,(const NVNqueueBuilder *)qb));
    assert(u64(queue+8)==0x12345000 && u64(queue+16)==0); canary(queue,sizeof(queue_t));
    // Texture ABI boundaries use guarded raw native extents rather than the NVN 1 typedef sizes.
    _Alignas(16) uint8_t tb[120], texture[144], clone[144], packaged[20];
    memset(tb,0xa5,sizeof tb); memset(texture,0xa5,sizeof texture); memset(clone,0xa5,sizeof clone);
    n2_nvnTextureBuilderSetDevice((NVNtextureBuilder *)tb,(NVNdevice *)(uintptr_t)0x12345000);
    n2_nvnTextureBuilderSetDefaults((NVNtextureBuilder *)tb);
    assert(u64(tb)==0xa5a5a5a5a5a5a5a5ULL && u64(tb+8)==0x12345000);
    assert(u32(tb+36)==1 && u32(tb+40)==0 && u32(tb+44)==0 && u32(tb+48)==0);
    assert(u32(tb+52)==1 && u32(tb+64)==2 && u32(tb+76)==5 && tb[86]==0xa5 && tb[87]==0xa5);
    canary(tb,104);
    n2_nvnTextureBuilderSetSize2D((NVNtextureBuilder *)tb,8,8);
    n2_nvnTextureBuilderSetFormat((NVNtextureBuilder *)tb,NVN_FORMAT_RGBA8);
    assert(n2_nvnTextureBuilderGetStorageSize((const NVNtextureBuilder *)tb)==512);
    assert(n2_nvnTextureBuilderGetStorageAlignment((const NVNtextureBuilder *)tb)==512);
    memset(packaged,0xa5,sizeof packaged);
    uint32_t layout=0x321; memcpy(packaged,&layout,4);
    n2_nvnTextureBuilderSetPackagedTextureLayout((NVNtextureBuilder *)tb,(const NVNpackagedTextureLayout *)packaged);
    assert(tb[84]==0x11 && tb[85]==0x32);
    assert(n2_nvnTextureBuilderGetPackagedTextureLayout((const NVNtextureBuilder *)tb,(NVNpackagedTextureLayout *)packaged));
    assert(u32(packaged)==0x321); canary(packaged,4);
    tex_builder_t decoded;
    assert(!nvn2_texture_decode((const nvn2_tex_builder *)tb,&decoded)); // block width/depth not admitted
    ((nvn2_tex_builder *)tb)->packaged=0;
    n2_nvnTextureBuilderSetFlags((NVNtextureBuilder *)tb,NVN_TEXTURE_FLAGS_SPARSE);
    assert(!nvn2_texture_decode((const nvn2_tex_builder *)tb,&decoded));
    n2_nvnTextureBuilderSetFlags((NVNtextureBuilder *)tb,0);
    n2_nvnTextureBuilderSetSamples((NVNtextureBuilder *)tb,4);
    assert(!nvn2_texture_decode((const nvn2_tex_builder *)tb,&decoded));
    n2_nvnTextureBuilderSetSamples((NVNtextureBuilder *)tb,0);
    n2_nvnTextureBuilderSetFormat((NVNtextureBuilder *)tb,NVN_FORMAT_RGB8);
    assert(!nvn2_texture_decode((const nvn2_tex_builder *)tb,&decoded)); // expansion is not admitted
    n2_nvnTextureBuilderSetFormat((NVNtextureBuilder *)tb,NVN_FORMAT_RGBA8);
    n2_nvnTextureBuilderSetFlags((NVNtextureBuilder *)tb,8);
    ((nvn2_tex_builder *)tb)->data=(void *)(uintptr_t)0x1234;
    assert(!nvn2_texture_decode((const nvn2_tex_builder *)tb,&decoded));
    ((nvn2_tex_builder *)tb)->data=NULL;
    n2_nvnTextureBuilderSetFormat((NVNtextureBuilder *)tb,0x35);
    assert(nvn2_texture_decode((const nvn2_tex_builder *)tb,&decoded)); // documented GPU-only depth promotion
    n2_nvnTextureBuilderSetFlags((NVNtextureBuilder *)tb,0);
    assert(!nvn2_texture_decode((const nvn2_tex_builder *)tb,&decoded)); // raw D24S8 remains gated
    n2_nvnTextureBuilderSetFormat((NVNtextureBuilder *)tb,NVN_FORMAT_RGBA8);
    uint8_t storage[1024]={0}; pool_t texture_pool={.magic=MAGIC('P','O','O','L'),.mem=storage,.gpu_mem=storage,.size=sizeof storage};
    n2_nvnTextureBuilderSetStorage((NVNtextureBuilder *)tb,(NVNmemoryPool *)&texture_pool,512);
    assert(n2_nvnTextureInitialize((NVNtexture *)texture,(const NVNtextureBuilder *)tb));
    canary(texture,128); memcpy(clone,texture,128);
    assert(h_nvnTextureGetWidth((const NVNtexture *)clone)==8 && h_nvnTextureGetMemoryOffset((const NVNtexture *)clone)==512);
    assert(h_nvnTextureGetTextureAddress((const NVNtexture *)clone)==(uintptr_t)(storage+512));
    assert(((const tex_t *)clone)->serial==((const tex_t *)texture)->serial);
    h_nvnTextureFinalize((NVNtexture *)texture); canary(texture,128); canary(clone,128);
    assert(nvn2_texture_impl("nvnTextureBuilderSetDefaults") && nvn2_texture_impl("nvnTextureGetStorageClass"));
    _Alignas(16) uint8_t wb[112], sync[48], buffer_builder[56], buffer[64], command[192], control[32];
    memset(wb,0xa5,sizeof wb); memset(sync,0xa5,sizeof sync);
    n2_nvnWindowBuilderSetDevice((NVNwindowBuilder *)wb,(NVNdevice *)(uintptr_t)0x12345000);
    n2_nvnWindowBuilderSetDefaults((NVNwindowBuilder *)wb);
    assert(u64(wb)==0xa5a5a5a5a5a5a5a5ULL && u64(wb+8)==0x12345000);
    assert(u32(wb+16)==0 && f32(wb+48)==.64f && f32(wb+92)==60.f); canary(wb,96);
    NVNtexture *window_textures[2]={(NVNtexture *)texture,(NVNtexture *)clone};
    n2_nvnWindowBuilderSetTextures((NVNwindowBuilder *)wb,2,window_textures);
    n2_nvnWindowBuilderSetPresentInterval((NVNwindowBuilder *)wb,2);
    assert(u32(wb+16)==2 && u64(wb+40)==(uintptr_t)window_textures && u32(wb+32)==1);
    assert(n2_nvnWindowBuilderGetTexture((const NVNwindowBuilder *)wb,1)==(NVNtexture *)clone);
    assert(n2_nvnWindowBuilderGetPresentInterval((const NVNwindowBuilder *)wb)==2);
    assert(h_nvnSyncInitialize((NVNsync *)sync,NULL)); canary(sync,32);
    assert(h_nvnSyncWait((const NVNsync *)sync,0)==NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED);
    h_nvnSyncFinalize((NVNsync *)sync); canary(sync,32);

    memset(buffer_builder,0xa5,sizeof buffer_builder); memset(buffer,0xa5,sizeof buffer);
    h_nvnBufferBuilderSetDevice((NVNbufferBuilder *)buffer_builder,(NVNdevice *)(uintptr_t)0x12345000);
    h_nvnBufferBuilderSetDefaults((NVNbufferBuilder *)buffer_builder);
    assert(u64(buffer_builder)==0xa5a5a5a5a5a5a5a5ULL && u64(buffer_builder+8)==0x12345000); canary(buffer_builder,40);
    h_nvnBufferBuilderSetStorage((NVNbufferBuilder *)buffer_builder,(NVNmemoryPool *)&texture_pool,16,64);
    assert(h_nvnBufferInitialize((NVNbuffer *)buffer,(const NVNbufferBuilder *)buffer_builder));
    assert(u64(buffer+8)==0x12345000 && u64(buffer+16)==(uintptr_t)&texture_pool && u64(buffer+24)==16 && u64(buffer+32)==64);
    assert(h_nvnBufferMap((const NVNbuffer *)buffer)==storage+16); texture_pool.flags=1;
    assert(h_nvnBufferMap((const NVNbuffer *)buffer)==NULL); texture_pool.flags=0; canary(buffer,48);
    h_nvnBufferFinalize((NVNbuffer *)buffer); canary(buffer,48);

    memset(command,0xa5,sizeof command); memset(control,0xa5,sizeof control);
    assert(h_nvnCommandBufferInitialize((NVNcommandBuffer *)command,NULL)); canary(command,176);
    h_nvnCommandBufferAddCommandMemory((NVNcommandBuffer *)command,(const NVNmemoryPool *)&texture_pool,0,128);
    h_nvnCommandBufferAddControlMemory((NVNcommandBuffer *)command,control,16);
    h_nvnCommandBufferBeginRecording((NVNcommandBuffer *)command);
    NVNcommandHandle handle=h_nvnCommandBufferEndRecording((NVNcommandBuffer *)command);
    assert(handle==(uintptr_t)control && u32(control)==OP_JUMP && u64(control+8)==(uintptr_t)storage);
    assert(u32(storage)==OP_END && h_nvnCommandBufferGetCommandMemoryUsed((const NVNcommandBuffer *)command)==8);
    assert(h_nvnCommandBufferGetControlMemoryUsed((const NVNcommandBuffer *)command)==16); canary(control,16);
    struct ctl_fixture ctl_fixture={0}; memset(ctl_fixture.memory,0xa5,sizeof ctl_fixture.memory);
    h_nvnCommandBufferSetMemoryCallback((NVNcommandBuffer *)command,control_callback);
    h_nvnCommandBufferSetMemoryCallbackData((NVNcommandBuffer *)command,&ctl_fixture);
    h_nvnCommandBufferBeginRecording((NVNcommandBuffer *)command);
    handle=h_nvnCommandBufferEndRecording((NVNcommandBuffer *)command);
    assert(ctl_fixture.calls==1 && handle==(uintptr_t)ctl_fixture.memory);
    assert(h_nvnCommandBufferGetControlMemoryUsed((const NVNcommandBuffer *)command)==16);
    assert(u64(ctl_fixture.memory+8)==(uintptr_t)(storage+8)); canary(ctl_fixture.memory,256); canary(command,176);
    pool_t scratch_pool={.magic=MAGIC('P','O','O','L'),.gpu_mem=(uint8_t *)(uintptr_t)0x10000000,.size=65536};
    cmd_t *command_record=(cmd_t *)command;
    assert(nvn2_command_scratch_reserve(command_record,&scratch_pool,4096,16384)==NVN2_SCRATCH_VALID);
    nvn2_scratch_reservation prior=command_record->scratch;
    assert(nvn2_command_scratch_reserve(command_record,&scratch_pool,1,16384)==NVN2_SCRATCH_ALIGNMENT);
    assert(!memcmp(&prior,&command_record->scratch,sizeof prior));
    assert(nvn2_command_scratch_reserve(command_record,&scratch_pool,-1,16384)==NVN2_SCRATCH_RANGE);
    assert(nvn2_command_scratch_reserve(command_record,&scratch_pool,65536,16384)==NVN2_SCRATCH_RANGE);
    assert(nvn2_command_scratch_reserve(command_record,NULL,0,0)==NVN2_SCRATCH_VALID);
    assert(nvn2_common_abi_verified("nvnCommandBufferSetShaderScratchMemory"));
    pool_builder_t empty_builder={.mem=(void *)(uintptr_t)0x1234,.size=0,.flags=0x21}; pool_t empty_pool;
    assert(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&empty_pool,(const NVNmemoryPoolBuilder *)&empty_builder));
    assert(!empty_pool.mem && !empty_pool.gpu_mem && !h_nvnMemoryPoolGetSize((NVNmemoryPool *)&empty_pool));
    assert(!h_nvnMemoryPoolMap((NVNmemoryPool *)&empty_pool) && !h_nvnMemoryPoolGetBufferAddress((NVNmemoryPool *)&empty_pool));
    assert(nvn2_command_scratch_reserve(command_record,&empty_pool,0,0)==NVN2_SCRATCH_VALID);
    assert(nvn2_command_scratch_reserve(command_record,&empty_pool,0,16384)==NVN2_SCRATCH_RANGE);
    empty_builder.size=16384; empty_builder.mem=NULL;
    pool_t invalid_pool; assert(!h_nvnMemoryPoolInitialize((NVNmemoryPool *)&invalid_pool,(const NVNmemoryPoolBuilder *)&empty_builder));
    assert(!invalid_pool.magic && !invalid_pool.mem && !invalid_pool.gpu_mem);
    buf_builder_t empty_buffer_builder={.pool=&empty_pool}; buf_t empty_buffer;
    assert(h_nvnBufferInitialize((NVNbuffer *)&empty_buffer,(const NVNbufferBuilder *)&empty_buffer_builder));
    assert(!h_nvnBufferGetAddress((NVNbuffer *)&empty_buffer) && !h_nvnBufferMap((NVNbuffer *)&empty_buffer));
    h_nvnBufferFinalize((NVNbuffer *)&empty_buffer); h_nvnMemoryPoolFinalize((NVNmemoryPool *)&empty_pool);
    int scratch_scale=-1;
    assert(nvn2_protocol_integer(0x17,&scratch_scale) && scratch_scale==0);
    assert(nvn2_protocol_integer(0x18,&scratch_scale) && scratch_scale==0);
    int value=123;
    assert(!nvn2_protocol_integer(0xffff,&value) && value==123);
    assert(nvn2_common_abi_verified("nvnProgramInitializeFromBuilder"));
    assert(!nvn2_common_abi_verified("nvnProgramSetSubroutineLinkage"));
    _Alignas(16) uint8_t program_builder[120]; memset(program_builder,0xa5,sizeof program_builder);
    n2_nvnProgramBuilderSetDevice(program_builder,(NVNdevice *)(uintptr_t)0x12345000);
    n2_nvnProgramBuilderSetDefaults(program_builder);
    assert(u64(program_builder)==0xa5a5a5a5a5a5a5a5ULL && u64(program_builder+8)==0x12345000);
    assert(u64(program_builder+16)==0xa5a5a5a5a5a5a5a5ULL && u32(program_builder+40)==0 && u64(program_builder+48)==0);
    assert(u32(program_builder+88)==2 && u32(program_builder+92)==2 && u32(program_builder+96)==0);
    assert(program_builder[44]==0xa5 && program_builder[60]==0xa5 && program_builder[76]==0xa5 && program_builder[100]==0xa5);
    canary(program_builder,104);
    NVNshaderData shader_records[2]={{0}};
    n2_nvnProgramBuilderSetShaders(program_builder,2,shader_records);
    assert(n2_nvnProgramBuilderGetShaderCount(program_builder)==2 && n2_nvnProgramBuilderGetShaders(program_builder)==shader_records);
    assert(nvn2_readable_extent(program_builder)>=sizeof program_builder && !nvn2_readable_extent(NULL) && !nvn2_readable_extent((void *)(uintptr_t)1));
    prog_t original_program={.magic=MAGIC('P','R','O','G'),.nstage=1,.serial=0x12345678};
    prog_keys_t *program_keys=pk_get(&original_program,1); program_keys->ok=1; program_keys->stage[0]=1; memset(program_keys->key[0],0x5a,8);
    prog_t copied_program=original_program; uint8_t copied_key[8];
    assert(prog_key(&copied_program,0,copied_key) && copied_key[0]==0x5a && nvn2_program_stage(&copied_program,0)==1);
    pk_drop(&original_program);
    _Alignas(16) uint8_t empty_program[176]; memset(empty_program,0xa5,sizeof empty_program);
    assert(h_nvnProgramInitialize((NVNprogram *)empty_program,NULL)); canary(empty_program,160);
    h_nvnProgramFinalize((NVNprogram *)empty_program); canary(empty_program,160);
    _Alignas(8) uint8_t native_attribute[20],native_stream[24];
    memset(native_attribute,0xa5,sizeof native_attribute); memset(native_stream,0xa5,sizeof native_stream);
    n2_nvnVertexAttribStateSetDefaults((NVNvertexAttribState *)native_attribute);
    n2_nvnVertexAttribStateSetStreamIndex((NVNvertexAttribState *)native_attribute,7);
    n2_nvnVertexAttribStateSetFormat((NVNvertexAttribState *)native_attribute,(NVNformat)0x30,0x1234);
    assert(u32(native_attribute)==(0x30u<<21|0x1234u<<7|64|7)); canary(native_attribute,4);
    NVNformat vertex_format; ptrdiff_t vertex_offset;
    n2_nvnVertexAttribStateGetFormat((NVNvertexAttribState *)native_attribute,&vertex_format,&vertex_offset);
    va_st decoded_attribute;
    assert(vertex_format==0x30 && vertex_offset==0x1234 && nvn2_vertex_decode(u32(native_attribute),&decoded_attribute));
    assert(decoded_attribute.fmt==0x30 && decoded_attribute.offset==0x1234 && decoded_attribute.stream==7);
    assert(!nvn2_vertex_decode(64|(0x31u<<21),&decoded_attribute));
    assert(!nvn2_vertex_decode(64|(1u<<21)|16,&decoded_attribute));
    h_nvnVertexStreamStateSetDefaults((NVNvertexStreamState *)native_stream);
    n2_nvnVertexStreamStateSetStride((NVNvertexStreamState *)native_stream,64);
    n2_nvnVertexStreamStateSetDivisor((NVNvertexStreamState *)native_stream,3);
    assert(u32(native_stream)==64 && u32(native_stream+4)==3); canary(native_stream,8);
    nvn2_tex_builder class_builder={.target=1,.width=64,.height=64,.depth=1,.levels=1,.format=1,.swz={2,3,4,5}};
    uint8_t seen_classes[65536]={0}; unsigned class_count=0;
    const int storage_samples[]={0,2,4,8};
    for (unsigned mode=0;mode<4;mode++) for (int format=1;format<=0x9c;format++) for (int policy=0;policy<3;policy++) {
        class_builder.samples=storage_samples[mode]; class_builder.target=mode?5:1;
        class_builder.format=format; class_builder.flags=policy==1?0x10:policy==2?8:0;
        class_builder.stride=policy==1?1024:0;
        tex_builder_t compatible;
        if (!nvn2_texture_layout_decode(&class_builder,&compatible)) continue;
        unsigned storage_class=n2_nvnTextureBuilderGetStorageClass((NVNtextureBuilder *)&class_builder);
        assert(storage_class<65535 && storage_class && nvn2_storage_class(&compatible.d)==storage_class);
        if (!seen_classes[storage_class]) { ++class_count; seen_classes[storage_class]=1; }
    }
    class_builder.target=1; class_builder.samples=0;
    class_builder.format=NVN_FORMAT_RGB565; class_builder.flags=0; class_builder.stride=0;
    tex_builder_t packed_storage;
    assert(nvn2_texture_layout_decode(&class_builder,&packed_storage));
    assert(!nvn2_texture_decode(&class_builder,&packed_storage)); // Querying storage never admits packed Metal creation.
    assert(n2_nvnTextureBuilderGetStorageSize((NVNtextureBuilder *)&class_builder)>0);
    class_builder.target=5; class_builder.format=NVN_FORMAT_R8; class_builder.samples=2;
    assert(nvn2_texture_layout_decode(&class_builder,&packed_storage) && !nvn2_texture_decode(&class_builder,&packed_storage));
    unsigned sample2_class=n2_nvnTextureBuilderGetStorageClass((NVNtextureBuilder *)&class_builder);
    class_builder.samples=4; assert(sample2_class!=n2_nvnTextureBuilderGetStorageClass((NVNtextureBuilder *)&class_builder));
    class_builder.samples=16; assert(!nvn2_texture_layout_decode(&class_builder,&packed_storage));
    assert(class_count && class_count<256); // Audited caller's dedup table is 256 uint16 cells.
    nvn_texdesc color_class={.format=NVN_FORMAT_RGBA8},same_geometry={.format=NVN_FORMAT_RGBA8UI};
    assert(nvn2_storage_class(&color_class)==nvn2_storage_class(&same_geometry));
    same_geometry.flags=0x10; assert(nvn2_storage_class(&color_class)!=nvn2_storage_class(&same_geometry));
    unsigned linear_class=nvn2_storage_class(&same_geometry);
    same_geometry.flags=0x48; assert(nvn2_storage_class(&same_geometry)==linear_class);
    same_geometry.flags=8; assert(nvn2_storage_class(&color_class)!=nvn2_storage_class(&same_geometry));
    same_geometry.flags=0; same_geometry.target=10;
    assert(nvn2_storage_class(&color_class)!=nvn2_storage_class(&same_geometry));
    same_geometry.target=1;
    same_geometry.flags=0; same_geometry.format=NVN_FORMAT_DEPTH32F;
    assert(nvn2_storage_class(&color_class)!=nvn2_storage_class(&same_geometry));
    same_geometry.format=NVN_FORMAT_RGB_DXT1;
    assert(nvn2_storage_class(&color_class)!=nvn2_storage_class(&same_geometry));
    printf("t_nvn2_abi: %u distinct audited storage classes fit the 16-bit opaque contract\n",class_count);
    _Alignas(16) uint8_t event_builder_bytes[32],event_bytes[56],event_copy[56];
    _Alignas(4) uint32_t event_storage[4]={5,0,0,0};
    pool_t event_pool={.magic=MAGIC('P','O','O','L'),.mem=(uint8_t *)event_storage,.gpu_mem=(uint8_t *)event_storage,.size=sizeof event_storage};
    memset(event_builder_bytes,0xa5,sizeof event_builder_bytes); memset(event_bytes,0xa5,sizeof event_bytes);
    h_nvnEventBuilderSetDefaults((NVNeventBuilder *)event_builder_bytes); canary(event_builder_bytes,16);
    h_nvnEventBuilderSetStorage((NVNeventBuilder *)event_builder_bytes,(NVNmemoryPool *)&event_pool,0);
    int64_t event_offset=-1;
    assert(h_nvnEventBuilderGetStorage((NVNeventBuilder *)event_builder_bytes,&event_offset)==(NVNmemoryPool *)&event_pool && !event_offset);
    assert(h_nvnEventInitialize((NVNevent *)event_bytes,(NVNeventBuilder *)event_builder_bytes)); canary(event_bytes,40);
    assert(u64(event_bytes)==(uintptr_t)event_storage && u64(event_bytes+8)==(uintptr_t)event_storage && u64(event_bytes+16)==(uintptr_t)&event_pool);
    memcpy(event_copy,event_bytes,sizeof event_copy);
    assert(h_nvnEventGetValue((NVNevent *)event_copy)==5);
    h_nvnEventSignal((NVNevent *)event_copy,NVN_EVENT_SIGNAL_MODE_ADD,7); assert(event_storage[0]==12);
    h_nvnEventSignal((NVNevent *)event_bytes,NVN_EVENT_SIGNAL_MODE_WRITE,UINT32_MAX);
    h_nvnEventSignal((NVNevent *)event_copy,NVN_EVENT_SIGNAL_MODE_ADD,2); assert(h_nvnEventGetValue((NVNevent *)event_bytes)==1);
    assert(h_nvnEventGetMemoryPool((NVNevent *)event_copy)==(NVNmemoryPool *)&event_pool && !h_nvnEventGetMemoryOffset((NVNevent *)event_copy));
    h_nvnEventFinalize((NVNevent *)event_bytes); canary(event_bytes,40);
    h_nvnEventBuilderSetStorage((NVNeventBuilder *)event_builder_bytes,(NVNmemoryPool *)&event_pool,sizeof event_storage);
    assert(!h_nvnEventInitialize((NVNevent *)event_bytes,(NVNeventBuilder *)event_builder_bytes)); canary(event_bytes,40);
    h_nvnEventBuilderSetStorage((NVNeventBuilder *)event_builder_bytes,(NVNmemoryPool *)&event_pool,1);
    assert(!h_nvnEventInitialize((NVNevent *)event_bytes,(NVNeventBuilder *)event_builder_bytes));
    event_pool.flags=1;
    h_nvnEventBuilderSetStorage((NVNeventBuilder *)event_builder_bytes,(NVNmemoryPool *)&event_pool,4);
    assert(h_nvnEventInitialize((NVNevent *)event_bytes,(NVNeventBuilder *)event_builder_bytes) && !u64(event_bytes+8));
    h_nvnEventFinalize((NVNevent *)event_bytes);
    queue_t primary_queue={.magic=MAGIC('Q','U','E','U'),.dev=(NVNdevice *)(uintptr_t)0x1234},secondary_queue;
    queue_builder_t secondary_builder={.dev=primary_queue.dev};
    h_nvnQueueBuilderSetPrimaryQueue((NVNqueueBuilder *)&secondary_builder,(NVNqueue *)&primary_queue);
    assert(secondary_builder.primary==(NVNqueue *)&primary_queue && nvn2_queue_primary_valid(&secondary_queue,&primary_queue,primary_queue.dev));
    assert(h_nvnQueueInitialize((NVNqueue *)&secondary_queue,(NVNqueueBuilder *)&secondary_builder) && secondary_queue.primary==&primary_queue);
    primary_queue.magic=0; assert(!nvn2_queue_primary_valid(&secondary_queue,&primary_queue,primary_queue.dev));
    assert(!h_nvnQueueInitialize((NVNqueue *)&secondary_queue,(NVNqueueBuilder *)&secondary_builder));
    primary_queue.magic=MAGIC('Q','U','E','U'); secondary_builder.dev=NULL;
    assert(!h_nvnQueueInitialize((NVNqueue *)&secondary_queue,(NVNqueueBuilder *)&secondary_builder));
    _Alignas(8) uint8_t native_blend[24]; memset(native_blend,0xa5,sizeof native_blend);
    n2_nvnBlendStateSetDefaults((NVNblendState *)native_blend);
    n2_nvnBlendStateSetBlendTarget((NVNblendState *)native_blend,3); canary(native_blend,8);
    assert(n2_nvnBlendStateGetBlendTarget((NVNblendState *)native_blend)==3);
    uint8_t state_memory[512]={0}; pool_t state_pool={.magic=MAGIC('P','O','O','L'),.mem=state_memory,.gpu_mem=state_memory,.size=sizeof state_memory};
    cmd_t state_command; h_nvnCommandBufferInitialize((NVNcommandBuffer *)&state_command,NULL);
    h_nvnCommandBufferAddCommandMemory((NVNcommandBuffer *)&state_command,(NVNmemoryPool *)&state_pool,0,sizeof state_memory);
    h_nvnCommandBufferBeginRecording((NVNcommandBuffer *)&state_command);
    n2_nvnCommandBufferBindBlendState((NVNcommandBuffer *)&state_command,(NVNblendState *)native_blend);
    assert(((op_hdr *)state_memory)->op==OP_BIND_STATE);
    p_bind_state *bound_blend=(p_bind_state *)(state_memory+8); blend_st canonical_blend;
    memcpy(&canonical_blend,bound_blend->bytes,sizeof canonical_blend);
    assert(bound_blend->kind==0 && canonical_blend.target==3 && canonical_blend.src_rgb==NVN_BLEND_FUNC_ONE);
    n2_nvnBlendStateSetBlendTarget((NVNblendState *)native_blend,5);
    memcpy(&canonical_blend,bound_blend->bytes,sizeof canonical_blend); assert(canonical_blend.target==3);
    assert(nvn2_state_impl("nvnMultisampleStateGetSampleLocationsGrid") && nvn2_common_abi_verified("nvnCommandBufferBindDepthStencilState"));
    h_nvnCommandBufferFinalize((NVNcommandBuffer *)&state_command);
    pool_builder_t virtual_builder={.flags=0x2a1,.size=65536}; pool_t virtual_pool;
    assert(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&virtual_pool,(NVNmemoryPoolBuilder *)&virtual_builder));
    uintptr_t virtual_address=h_nvnMemoryPoolGetBufferAddress((NVNmemoryPool *)&virtual_pool);
    assert(virtual_address && !(virtual_address&65535) && h_nvnMemoryPoolGetSize((NVNmemoryPool *)&virtual_pool)==65536);
    assert(!h_nvnMemoryPoolMap((NVNmemoryPool *)&virtual_pool));
    assert(!nvn2_readable_extent((void *)virtual_address) && !nvn_host_readable(virtual_address,4));
    void *unmapped_metal=NULL; uint64_t unmapped_offset=0;
    assert(!mtl_addr(virtual_address,4,&unmapped_metal,&unmapped_offset));
    assert(!nvn2_common_abi_verified("nvnMemoryPoolMapVirtual"));
    h_nvnMemoryPoolFinalize((NVNmemoryPool *)&virtual_pool);
    assert(!vpool_of(&virtual_pool));
    virtual_builder.size=65537;
    assert(!h_nvnMemoryPoolInitialize((NVNmemoryPool *)&virtual_pool,(NVNmemoryPoolBuilder *)&virtual_builder));
    virtual_builder.size=65536; virtual_builder.mem=(void *)(uintptr_t)0x1234;
    assert(!h_nvnMemoryPoolInitialize((NVNmemoryPool *)&virtual_pool,(NVNmemoryPoolBuilder *)&virtual_builder));
    const float unavailable_clear[4]={0,0,0,1};
    assert(!h_nvnDeviceRegisterFastClearColor(NULL,unavailable_clear,NVN_FORMAT_RGBA16F));
    puts("t_nvn2_abi: object extents, texture value copies, serialized fields and unsupported calls passed");
    if (argc==2 && !strcmp(argv[1],"--metal")) texture_gpu_fixture();
    return 0;
}
