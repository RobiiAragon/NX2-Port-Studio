// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct {
    uint64_t reserved; void *dev; int32_t ntex, active; void *native;
    int32_t interval_minus_one, mode; void *textures; float color[12];
} nvn2_win_builder;
_Static_assert(sizeof(nvn2_win_builder) == 96 && offsetof(nvn2_win_builder, textures) == 40, "NVN2 window builder extent");
#define NVN2_WINDOW_API(X) \
    X(nvnWindowBuilderSetDefaults) X(nvnWindowBuilderSetDevice) X(nvnWindowBuilderSetNativeWindow) \
    X(nvnWindowBuilderSetTextures) X(nvnWindowBuilderSetPresentInterval) X(nvnWindowBuilderSetNumActiveTextures) \
    X(nvnWindowBuilderGetDevice) X(nvnWindowBuilderGetNativeWindow) X(nvnWindowBuilderGetTexture) \
    X(nvnWindowBuilderGetNumTextures) X(nvnWindowBuilderGetNumActiveTextures) X(nvnWindowBuilderGetPresentInterval) \
    X(nvnWindowInitialize) X(nvnWindowFinalize) X(nvnWindowGetNumActiveTextures) \
    X(nvnWindowAcquireTexture) X(nvnWindowGetPresentInterval) X(nvnWindowSetPresentInterval) X(nvnWindowSetCrop) X(nvnWindowGetCrop)
static inline int nvn2_window_abi_verified(const char *name) {
#define N2_WIN_NAME(n) if (!strcmp(name, #n)) return 1;
    NVN2_WINDOW_API(N2_WIN_NAME)
#undef N2_WIN_NAME
    return 0;
}
