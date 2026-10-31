// SPDX-License-Identifier: GPL-3.0-or-later
#if DM_GAME_NVN2
#include "nvn2_shader_control.h"
#define PB2(b) ((nvn2_program_builder *)(b))
#define CPB2(b) ((const nvn2_program_builder *)(b))
static void n2_nvnProgramBuilderSetDefaults(void *b) {
    nvn2_program_builder *p=PB2(b);
    p->sample_shading=p->hash_count=0; p->hashes=NULL; p->shader_count=0; p->shaders=NULL;
    p->ray_count=0; p->ray_modules=NULL; p->linkage_count=0; p->linkage=NULL;
    p->trace_depth=p->callable_depth=2; p->ray_flags=0;
}
static void n2_nvnProgramBuilderSetDevice(void *b, NVNdevice *device) { PB2(b)->device=device; }
static NVNdevice *n2_nvnProgramBuilderGetDevice(const void *b) { return CPB2(b)->device; }
static void n2_nvnProgramBuilderSetShaders(void *b, int n, const NVNshaderData *shaders) { PB2(b)->shader_count=n; PB2(b)->shaders=shaders; }
static const NVNshaderData *n2_nvnProgramBuilderGetShaders(const void *b) { return CPB2(b)->shaders; }
static int n2_nvnProgramBuilderGetShaderCount(const void *b) { return CPB2(b)->shader_count; }
static void n2_nvnProgramBuilderSetSampleShading(void *b, uint32_t enabled) { PB2(b)->sample_shading=enabled; }
static uint32_t n2_nvnProgramBuilderGetSampleShading(const void *b) { return CPB2(b)->sample_shading; }
static size_t nvn2_readable_extent(const void *p) {
    if (!p) return 0;
    vm_address_t address=(vm_address_t)p; vm_size_t size=0;
    vm_region_basic_info_data_64_t info; mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t result=vm_region_64(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    if (object!=MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),object);
    uintptr_t pointer=(uintptr_t)p;
    if (result!=KERN_SUCCESS || !(info.protection&VM_PROT_READ) || pointer<address || pointer-address>=size) return 0;
    return size-(pointer-address);
}
static void nvn2_program_shader(const NVNshaderData *shader, struct nvn2_ctl_info *info, uint8_t key[8]) {
    enum nvn2_ctl_result result=nvn2_ctl_read(shader->control,nvn2_readable_extent(shader->control),info);
    if (result!=NVN2_CTL_OK) die("[RED] NVN2 shader control: %s",nvn2_ctl_error(result));
    if (info->stage>1) die("[RED] NVN2 shader stage %u has no validated Metal program path",info->stage);
    void *buffer=NULL; uint64_t offset=0;
    if (!shader->data || !mtl_addr(shader->data,info->code_bytes,&buffer,&offset))
        die("[RED] NVN2 shader code is outside registered Metal pool storage");
    const void *code=(const void *)(uintptr_t)shader->data;
    size_t available=nvn2_readable_extent(code);
    result=nvn2_ctl_check_graphics_code(info,code,available);
    if (result!=NVN2_CTL_OK) die("[RED] NVN2 shader code: %s",nvn2_ctl_error(result));
    CC_SHA256_CTX hash; uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Init(&hash); CC_SHA256_Update(&hash,shader->control,info->control_bytes);
    CC_SHA256_Update(&hash,code,info->code_bytes); CC_SHA256_Final(digest,&hash); memcpy(key,digest,8);
}
static NVNboolean n2_nvnProgramSetShaders(NVNprogram *program, int count, const NVNshaderData *shaders) {
    if (count<1 || count>6 || nvn2_readable_extent(shaders)<(size_t)count*sizeof *shaders)
        die("[RED] NVN2 program shader array exceeds known readable CPU extent");
    prog_keys_t metadata={0}; uint32_t stages=0;
    for (int i=0;i<count;i++) {
        struct nvn2_ctl_info info; nvn2_program_shader(&shaders[i],&info,metadata.key[i]);
        if (stages&(1u<<info.stage)) die("[RED] NVN2 program contains duplicate shader stages");
        stages|=1u<<info.stage; metadata.stage[i]=info.stage; metadata.ok|=1u<<i;
        OLOG("NVN2 program stage=%u control=%u code=%u key=%02x%02x%02x%02x%02x%02x%02x%02x",info.stage,info.control_bytes,info.code_bytes,
            metadata.key[i][0],metadata.key[i][1],metadata.key[i][2],metadata.key[i][3],metadata.key[i][4],metadata.key[i][5],metadata.key[i][6],metadata.key[i][7]);
    }
    prog_t *p=(prog_t *)program;
    if (!p || p->magic!=MAGIC('P','R','O','G')) die("[RED] NVN2 shaders require an initialized program");
    mtl_prog_release(p); pk_drop(p); p->serial=nvn_new_serial(); p->nstage=count;
    memcpy(p->sd,shaders,(size_t)count*sizeof *shaders); *pk_get(p,1)=metadata;
    // Preparation must load real translated Metal functions; unknown keys or stale
    // libraries fail explicitly before the API can report successful initialization.
    pthread_mutex_lock(&g_sub_mu);
    int prepared=mtl_nvn2_program_prepare(p);
    pthread_mutex_unlock(&g_sub_mu);
    if (!prepared) die("[RED] NVN2 program has no validated Metal functions");
    return 1;
}
static NVNboolean n2_nvnProgramInitializeFromBuilder(NVNprogram *program, const void *builder) {
    const nvn2_program_builder *b=CPB2(builder);
    if (b->sample_shading || b->ray_count || b->ray_modules || b->linkage_count || b->linkage || b->ray_flags ||
        b->trace_depth!=2 || b->callable_depth!=2)
        die("[RED] NVN2 program builder requests unsupported sample shading, linkage or ray tracing");
    _Static_assert(sizeof(prog_t)<=160,"NVN2 program initialized extent");
    h_nvnProgramInitialize(program,b->device);
    return n2_nvnProgramSetShaders(program,b->shader_count,b->shaders);
}
static void *nvn2_program_impl(const char *name) {
#define N2_PROGRAM_PTR(n) if (!strcmp(name,#n)) return (void *)n2_##n;
    N2_PROGRAM_PTR(nvnProgramBuilderSetDefaults) N2_PROGRAM_PTR(nvnProgramBuilderSetDevice) N2_PROGRAM_PTR(nvnProgramBuilderGetDevice)
    N2_PROGRAM_PTR(nvnProgramBuilderSetShaders) N2_PROGRAM_PTR(nvnProgramBuilderGetShaders) N2_PROGRAM_PTR(nvnProgramBuilderGetShaderCount)
    N2_PROGRAM_PTR(nvnProgramBuilderSetSampleShading) N2_PROGRAM_PTR(nvnProgramBuilderGetSampleShading)
    N2_PROGRAM_PTR(nvnProgramInitializeFromBuilder) N2_PROGRAM_PTR(nvnProgramSetShaders)
#undef N2_PROGRAM_PTR
    return NULL;
}
#undef PB2
#undef CPB2
#else
static void *nvn2_program_impl(const char *name) { (void)name; return NULL; }
#endif
