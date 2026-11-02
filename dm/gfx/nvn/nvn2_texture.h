// SPDX-License-Identifier: GPL-3.0-or-later
// Audited NVN2 CPU texture ABI. This is separate from the NVN 1 host description.
#pragma once
#include <stddef.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    uint64_t reserved; void *dev, *pool; int64_t off;
    int32_t flags, target, width, height, depth, levels, format, samples;
    uint32_t swz[4], depth_mode; uint16_t packaged, padding;
    uint32_t stride, gl_name; const void *data;
} nvn2_tex_builder;
_Static_assert(sizeof(nvn2_tex_builder) == 104, "NVN2 texture builder extent");
_Static_assert(offsetof(nvn2_tex_builder, dev) == 8 && offsetof(nvn2_tex_builder, flags) == 32 &&
               offsetof(nvn2_tex_builder, swz) == 64 && offsetof(nvn2_tex_builder, packaged) == 84 &&
               offsetof(nvn2_tex_builder, data) == 96, "NVN2 texture builder fields");

#define NVN2_TEXTURE_BUILDER_API(X) \
    X(nvnTextureBuilderSetDefaults) X(nvnTextureBuilderSetDevice) \
    X(nvnTextureBuilderSetFlags) X(nvnTextureBuilderSetTarget) \
    X(nvnTextureBuilderSetWidth) X(nvnTextureBuilderSetHeight) X(nvnTextureBuilderSetDepth) \
    X(nvnTextureBuilderSetSize1D) X(nvnTextureBuilderSetSize2D) X(nvnTextureBuilderSetSize3D) \
    X(nvnTextureBuilderSetLevels) X(nvnTextureBuilderSetFormat) X(nvnTextureBuilderSetSamples) \
    X(nvnTextureBuilderSetSwizzle) X(nvnTextureBuilderSetDepthStencilMode) \
    X(nvnTextureBuilderSetStorage) X(nvnTextureBuilderSetStride) \
    X(nvnTextureBuilderSetPackagedTextureData) X(nvnTextureBuilderSetPackagedTextureLayout) \
    X(nvnTextureBuilderGetDevice) X(nvnTextureBuilderGetFlags) X(nvnTextureBuilderGetTarget) \
    X(nvnTextureBuilderGetWidth) X(nvnTextureBuilderGetHeight) X(nvnTextureBuilderGetDepth) \
    X(nvnTextureBuilderGetLevels) X(nvnTextureBuilderGetFormat) X(nvnTextureBuilderGetSamples) \
    X(nvnTextureBuilderGetSwizzle) X(nvnTextureBuilderGetDepthStencilMode) \
    X(nvnTextureBuilderGetMemoryPool) X(nvnTextureBuilderGetMemoryOffset) X(nvnTextureBuilderGetStride) \
    X(nvnTextureBuilderGetPackagedTextureData) X(nvnTextureBuilderGetPackagedTextureLayout) \
    X(nvnTextureBuilderGetStorageSize) X(nvnTextureBuilderGetStorageAlignment) X(nvnTextureBuilderGetStorageClass)

#define NVN2_TEXTURE_OBJECT_API(X) \
    X(nvnTexturePoolRegisterTexture) X(nvnTextureInitialize) X(nvnTextureFinalize) X(nvnTextureGetFlags) \
    X(nvnTextureGetTarget) X(nvnTextureGetWidth) X(nvnTextureGetHeight) X(nvnTextureGetDepth) \
    X(nvnTextureGetLevels) X(nvnTextureGetFormat) X(nvnTextureGetSamples) \
    X(nvnTextureGetSwizzle) X(nvnTextureGetDepthStencilMode) X(nvnTextureGetStride) \
    X(nvnTextureGetStorageClass) X(nvnTextureGetMemoryPool) X(nvnTextureGetMemoryOffset) X(nvnTextureGetTextureAddress) X(nvnTextureGetStorageSize)

static inline int nvn2_texture_abi_verified(const char *name) {
#define NVN2_NAME(n) if (!strcmp(name, #n)) return 1;
    NVN2_TEXTURE_BUILDER_API(NVN2_NAME)
    NVN2_TEXTURE_OBJECT_API(NVN2_NAME)
#undef NVN2_NAME
    return 0;
}
