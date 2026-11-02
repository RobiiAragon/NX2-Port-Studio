// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string.h>
#define NVN2_VERTEX_API(X) \
    X(nvnVertexAttribStateSetDefaults) X(nvnVertexAttribStateSetFormat) X(nvnVertexAttribStateSetStreamIndex) \
    X(nvnVertexAttribStateGetFormat) X(nvnVertexAttribStateGetStreamIndex) \
    X(nvnVertexStreamStateSetDefaults) X(nvnVertexStreamStateSetStride) X(nvnVertexStreamStateSetDivisor) \
    X(nvnVertexStreamStateGetStride) X(nvnVertexStreamStateGetDivisor) X(nvnCommandBufferBindVertexAttribState) \
    X(nvnCommandBufferBindVertexStreamState)
static inline int nvn2_vertex_abi_verified(const char *name) {
#define V2_NAME(n) if (!strcmp(name,#n)) return 1;
    NVN2_VERTEX_API(V2_NAME)
#undef V2_NAME
    return 0;
}
