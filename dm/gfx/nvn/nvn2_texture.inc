// SPDX-License-Identifier: GPL-3.0-or-later
// Included after hle_nvn.c's shared semantic functions. No original GPU driver runs.
#if DM_GAME_NVN2
#include "../tex/nvn_fmt_mtl.h"
#define B2(b) ((nvn2_tex_builder *)(b))
#define CB2(b) ((const nvn2_tex_builder *)(b))
#define T2_FN(name, ret, ...) static ret n2_##name(__VA_ARGS__)
T2_FN(nvnTextureBuilderSetDefaults, void, NVNtextureBuilder *b) {
    nvn2_tex_builder *o = B2(b);
    // The native setter preserves the reserved prefix, device, and two padding bytes.
    o->pool = NULL; o->off = 0; o->flags = 0; o->target = 1;
    o->width = o->height = o->depth = 0; o->levels = 1; o->format = o->samples = 0;
    for (unsigned i = 0; i < 4; ++i) o->swz[i] = 2 + i;
    o->depth_mode = 0; o->packaged = 0; o->stride = o->gl_name = 0; o->data = NULL;
}
T2_FN(nvnTextureBuilderSetDevice, void, NVNtextureBuilder *b, NVNdevice *d) { B2(b)->dev = d; }
#define B2_SCALAR(Name, field, Type) \
T2_FN(nvnTextureBuilderSet##Name, void, NVNtextureBuilder *b, Type x) { B2(b)->field = x; } \
T2_FN(nvnTextureBuilderGet##Name, Type, const NVNtextureBuilder *b) { return (Type)CB2(b)->field; }
B2_SCALAR(Flags, flags, NVNtextureFlags)
B2_SCALAR(Target, target, NVNtextureTarget)
B2_SCALAR(Width, width, int)
B2_SCALAR(Height, height, int)
B2_SCALAR(Depth, depth, int)
B2_SCALAR(Levels, levels, int)
B2_SCALAR(Format, format, NVNformat)
B2_SCALAR(Samples, samples, int)
B2_SCALAR(Stride, stride, ptrdiff_t)
B2_SCALAR(DepthStencilMode, depth_mode, NVNtextureDepthStencilMode)
#undef B2_SCALAR
T2_FN(nvnTextureBuilderSetSize1D, void, NVNtextureBuilder *b, int w) { B2(b)->width = w; B2(b)->height = B2(b)->depth = 1; }
T2_FN(nvnTextureBuilderSetSize2D, void, NVNtextureBuilder *b, int w, int h) { B2(b)->width = w; B2(b)->height = h; B2(b)->depth = 1; }
T2_FN(nvnTextureBuilderSetSize3D, void, NVNtextureBuilder *b, int w, int h, int d) { B2(b)->width = w; B2(b)->height = h; B2(b)->depth = d; }
T2_FN(nvnTextureBuilderSetStorage, void, NVNtextureBuilder *b, NVNmemoryPool *p, ptrdiff_t off) { B2(b)->pool = p; B2(b)->off = off; }
T2_FN(nvnTextureBuilderSetSwizzle, void, NVNtextureBuilder *b, NVNtextureSwizzle r, NVNtextureSwizzle g, NVNtextureSwizzle bl, NVNtextureSwizzle a) {
    B2(b)->swz[0] = r; B2(b)->swz[1] = g; B2(b)->swz[2] = bl; B2(b)->swz[3] = a;
}
T2_FN(nvnTextureBuilderGetSwizzle, void, const NVNtextureBuilder *b, NVNtextureSwizzle *r, NVNtextureSwizzle *g, NVNtextureSwizzle *bl, NVNtextureSwizzle *a) {
    *r = CB2(b)->swz[0]; *g = CB2(b)->swz[1]; *bl = CB2(b)->swz[2]; *a = CB2(b)->swz[3];
}
T2_FN(nvnTextureBuilderGetDevice, NVNdevice *, const NVNtextureBuilder *b) { return CB2(b)->dev; }
T2_FN(nvnTextureBuilderGetMemoryPool, NVNmemoryPool *, const NVNtextureBuilder *b) { return CB2(b)->pool; }
T2_FN(nvnTextureBuilderGetMemoryOffset, ptrdiff_t, const NVNtextureBuilder *b) { return CB2(b)->off; }
T2_FN(nvnTextureBuilderSetPackagedTextureData, void, NVNtextureBuilder *b, const void *data) { B2(b)->data = data; }
T2_FN(nvnTextureBuilderGetPackagedTextureData, const void *, const NVNtextureBuilder *b) { return CB2(b)->data; }
T2_FN(nvnTextureBuilderSetPackagedTextureLayout, void, NVNtextureBuilder *b, const NVNpackagedTextureLayout *layout) {
    // NVN2 uses a 32-bit public layout, with three payload nibbles. Its native setter requires a valid pointer.
    uint16_t bits; memcpy(&bits, layout, sizeof bits); B2(b)->packaged = (uint16_t)((bits & 0xfff) << 4 | 1);
}
T2_FN(nvnTextureBuilderGetPackagedTextureLayout, NVNboolean, const NVNtextureBuilder *b, NVNpackagedTextureLayout *layout) {
    uint32_t bits = CB2(b)->packaged & 15 ? CB2(b)->packaged >> 4 : 0;
    memcpy(layout, &bits, sizeof bits); return CB2(b)->packaged & 15;
}
// The audited 64-byte by 8-row GOB/mip math agrees with the shared layout.
// The host uses its own storage alignment; no SDK helper sees a Metal device record.
// Sparse/3D layouts and new formats stay closed. Metal representation is validated
// separately from the audited CPU geometry used by storage queries.
static int nvn2_texture_layout_decode(const nvn2_tex_builder *b, tex_builder_t *o) {
    if (b->gl_name || (b->flags & ~(1 | 4 | 8 | 0x10 | 0x40)) ||
        b->target < 0 || b->target > 10 || b->target == 2 ||
        (b->samples!=0 && b->samples!=1 && b->samples!=2 && b->samples!=4 && b->samples!=8) ||
        b->width < 1 || b->height < 1 || b->depth < 1 || b->levels < 1 || b->levels > 15 ||
        b->format < 1 || b->format > 0x9c || !nvn_fmt_bpb(b->format, NULL, NULL) ||
        b->depth_mode > 1 || ((b->packaged & 15) && (b->packaged & 0xff00))) return 0;
    // Audited native storage scaling is2x1/2x2/4x2 for2/4/8 samples.
    // The inherited layout matches those modes;16 samples remains unknown here.
    if (b->samples>1 && ((b->target!=5 && b->target!=6) || b->levels!=1 || (b->flags&0x50))) return 0;
    if ((b->target==5 || b->target==6) && b->samples<=1) return 0;
    for (unsigned i = 0; i < 4; ++i) if (b->swz[i] > 5) return 0;
    if ((b->flags & 0x50) && (!b->stride || b->levels != 1 || b->target != 1)) return 0;
    memset(o, 0, sizeof *o); o->dev = b->dev; o->pool = b->pool; o->off = b->off;
    o->d = (nvn_texdesc){.target=b->target, .format=b->format, .flags=b->flags, .samples=b->samples,
        .width=b->width, .height=b->height, .depth=b->depth, .levels=b->levels, .stride=b->stride};
    o->has_layout = (b->packaged & 15) != 0;
    if (o->has_layout) {
        // Packaged payload nibble 0 is block height. Nonzero block depth/width is rejected above.
        o->layout[0] = (b->packaged >> 4) & 15; o->d.pkg_block_h = o->layout[0] + 1;
        if (o->layout[0] > 4) return 0;
    }
    o->pkg_data = b->data; o->set_swz = 1; o->stencil = b->depth_mode;
    o->swz = b->swz[0] | b->swz[1] << 3 | b->swz[2] << 6 | b->swz[3] << 9;
    return 1;
}
static int nvn2_texture_decode(const nvn2_tex_builder *b,tex_builder_t *out) {
    // Storage geometry is independent of Metal channel/bit representation.
    // Creation still requires an exact representation or the documented GPU-only
    // D24/D24S8 promotion. Raw depth import/readback and packed substitutions stay gated.
    tex_builder_t decoded;
    if (!nvn2_texture_layout_decode(b,&decoded) || b->samples>1 || b->target==5 || b->target==6) return 0;
    // COMPRESSIBLE is GPU-owned storage; NVIDIA tag bytes are never imported.
    // A native linear class ignores this hint, but its Metal creation path has
    // not established compression/data ownership for that combination.
    if ((b->flags&8) && (b->target!=1 || b->data || (b->packaged&15) || (b->flags&0x50))) return 0;
    int promoted_depth=(b->flags&8) && (b->format==0x33 || b->format==0x35);
    if (nvn_fmt_to_mtl(b->format,NULL,NULL)!=1 && !promoted_depth) return 0;
    *out=decoded; return 1;
}
static void nvn2_texture_layout_require(const nvn2_tex_builder *builder,tex_builder_t *out) {
    if (!nvn2_texture_layout_decode(builder,out))
        die("[RED] NVN2 texture storage layout unsupported: %dx%dx%d levels=%d format=%#x target=%d flags=%#x samples=%d stride=%u packaged=%#x",
            builder->width,builder->height,builder->depth,builder->levels,builder->format,builder->target,builder->flags,builder->samples,builder->stride,builder->packaged);
}
static void nvn2_texture_require(const nvn2_tex_builder *b, tex_builder_t *o) {
    nvn2_texture_layout_require(b,o);
    if (!nvn2_texture_decode(b, o))
        die("[RED] NVN2 texture Metal representation unsupported: %dx%dx%d levels=%d format=%#x target=%d flags=%#x samples=%d stride=%u packaged=%#x",
            b->width,b->height,b->depth,b->levels,b->format,b->target,b->flags,b->samples,b->stride,b->packaged);
}
// Host storage classes describe guest byte-allocation compatibility, not format capability
// or alias permission. Equal classes have identical guest block geometry, depth
// plane category and linear/compressible storage policy. Metal images remain
// independently allocated; reinterpretation and MapVirtual are not admitted.
// A conservative representative format supplies a deterministic opaque token;
// neither its numeric value nor NVIDIA page-kind numbers are exposed semantics.
static unsigned nvn2_storage_plane(int format) {
    if (format==0x31) return 1; // Stencil only.
    if (format>=0x32 && format<=0x34) return 2; // Depth only.
    if (format==0x35 || format==0x36) return 3; // Separate depth/stencil semantics.
    return 0;
}
static NVNstorageClass nvn2_storage_class(const nvn_texdesc *desc) {
    int bw,bh,bpb=nvn_fmt_bpb(desc->format,&bw,&bh),representative=desc->format;
    for (int f=1;f<desc->format;f++) {
        int w,h,n=nvn_fmt_bpb(f,&w,&h);
        if (n==bpb && w==bw && h==bh && nvn2_storage_plane(f)==nvn2_storage_plane(desc->format)) { representative=f; break; }
    }
    unsigned linear=(desc->flags&0x50 || desc->target==10)!=0;
    unsigned policy=linear?1:(desc->flags&8?2:0);
    unsigned samples=desc->samples==2?1:desc->samples==4?2:desc->samples==8?3:0;
    return (NVNstorageClass)(0x4000u|(samples<<10)|((unsigned)representative<<2)|policy);
}
T2_FN(nvnTextureBuilderGetStorageClass, NVNstorageClass, const NVNtextureBuilder *builder) {
    tex_builder_t decoded; nvn2_texture_layout_require(CB2(builder),&decoded);
    return nvn2_storage_class(&decoded.d);
}
T2_FN(nvnTextureGetStorageClass, NVNstorageClass, const NVNtexture *texture) {
    const tex_t *object=CTX(texture);
    if (!object || object->magic!=MAGIC('T','E','X',0)) die("[RED] NVN2 storage class requires an initialized texture");
    return nvn2_storage_class(&object->d);
}
T2_FN(nvnTextureBuilderGetStorageSize, size_t, const NVNtextureBuilder *b) {
    tex_builder_t decoded; nvn2_texture_layout_require(CB2(b), &decoded);
    nvn_texlayout l; nvn_tex_layout(&decoded.d, &l); return l.size;
}
T2_FN(nvnTextureBuilderGetStorageAlignment, size_t, const NVNtextureBuilder *b) {
    tex_builder_t decoded; nvn2_texture_layout_require(CB2(b), &decoded);
    nvn_texlayout l; nvn_tex_layout(&decoded.d, &l); return l.align;
}
T2_FN(nvnTextureInitialize, NVNboolean, NVNtexture *t, const NVNtextureBuilder *b) {
    tex_builder_t decoded; nvn2_texture_require(CB2(b), &decoded);
    nvn_texlayout l; nvn_tex_layout(&decoded.d, &l);
    if (!decoded.pool || decoded.pool->magic != MAGIC('P','O','O','L') || !decoded.pool->gpu_mem || decoded.off < 0 ||
        (uint64_t)decoded.off > decoded.pool->size || l.size > decoded.pool->size - (uint64_t)decoded.off)
        die("[RED] NVN2 texture storage is outside its initialized pool");
    if ((decoded.pool->flags&0x200) && !nvn_host_readable((uintptr_t)decoded.pool->gpu_mem+(uint64_t)decoded.off,l.size))
        die("[RED] NVN2 texture initialization requires a fully mapped virtual range");
    if ((decoded.d.flags&8) && ((decoded.pool->flags&0x81)!=0x81))
        die("[RED] NVN2 compressible texture requires GPU-only compressible storage");
    // Native TextureInitialize writes exactly 0x80 bytes. The same-size host record
    // carries its serial through guest copies by value; every admitted accessor is intercepted.
    _Static_assert(sizeof(tex_t) <= 128, "NVN2 texture object extent");
    return h_nvnTextureInitialize(t, (const NVNtextureBuilder *)&decoded);
}
T2_FN(nvnTexturePoolRegisterTexture, void, const NVNtexturePool *pool, int id, const NVNtexture *texture, const NVNtextureView *view) {
    const dpool_t *p=(const dpool_t *)pool;
    if (view) die("[RED] NVN2 texture descriptor views have not been adapted");
    if (!p || id<0 || id>=p->n || !texture || CTX(texture)->magic!=MAGIC('T','E','X',0))
        die("[RED] NVN2 texture descriptor references an invalid object or index");
    mtl_reg_tex(dpool_base(g_tpb,g_ntpb,pool)+id,CTX(texture),NULL);
}
static void *nvn2_texture_impl(const char *name) {
#define NVN2_PTR(n) if (!strcmp(name, #n)) return (void *)n2_##n;
    NVN2_TEXTURE_BUILDER_API(NVN2_PTR)
    NVN2_PTR(nvnTextureInitialize) NVN2_PTR(nvnTexturePoolRegisterTexture) NVN2_PTR(nvnTextureGetStorageClass)
#undef NVN2_PTR
#define NVN2_SHARED(n) if (!strcmp(name, #n)) return (void *)h_##n;
    NVN2_TEXTURE_OBJECT_API(NVN2_SHARED)
#undef NVN2_SHARED
    return NULL;
}
#undef T2_FN
#undef CB2
#undef B2
#else
static void *nvn2_texture_impl(const char *name) { (void)name; return NULL; }
#endif
