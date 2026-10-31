// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental NVN2 protocol boundary. Unverified entry points stop explicitly.
#pragma once
#include <string.h>
#include "nvn2_texture.h"
#include "nvn2_window.h"
#include "nvn2_program.h"
#include "nvn2_vertex.h"
#include "nvn2_state_api.h"
#ifndef DM_GAME_NVN2
#define DM_GAME_NVN2 0
#endif

static inline int nvn2_common_abi_verified(const char *name) {
    if (nvn2_texture_abi_verified(name) || nvn2_window_abi_verified(name) || nvn2_program_abi_verified(name) || nvn2_vertex_abi_verified(name) || nvn2_state_abi_verified(name)) return 1;
    // Checked pointer/int signatures; host records fit the NVN2 opaque objects.
    static const char *const names[] = {
        "nvnDeviceBuilderSetDefaults", "nvnDeviceBuilderSetFlags", "nvnDeviceBuilderGetFlags",
        "nvnDeviceInitialize", "nvnDeviceFinalize", "nvnDeviceGetProcAddress",
        "nvnDeviceRegisterFastClearColor", "nvnDeviceGetInteger", "nvnDeviceInstallDebugCallback", "nvnDeviceGetFlags",
        "nvnDeviceSetWindowOriginMode", "nvnDeviceGetWindowOriginMode",
        "nvnDeviceSetDepthMode", "nvnDeviceGetDepthMode",
        "nvnMemoryPoolBuilderSetDefaults", "nvnMemoryPoolBuilderSetDevice",
        "nvnMemoryPoolBuilderSetStorage", "nvnMemoryPoolBuilderSetFlags",
        "nvnMemoryPoolInitialize", "nvnMemoryPoolFinalize", "nvnMemoryPoolGetFlags",
        "nvnMemoryPoolMap", "nvnMemoryPoolGetSize", "nvnMemoryPoolGetBufferAddress",
        "nvnMemoryPoolFlushMappedRange", "nvnMemoryPoolMapVirtual",
        "nvnSamplerPoolInitialize", "nvnSamplerPoolGetMemoryPool",
        "nvnSamplerPoolGetMemoryOffset", "nvnSamplerPoolGetSize",
        "nvnTexturePoolInitialize", "nvnTexturePoolGetMemoryPool",
        "nvnTexturePoolGetMemoryOffset", "nvnTexturePoolGetSize",
        "nvnSamplerBuilderSetDefaults", "nvnSamplerBuilderSetDevice",
        "nvnSamplerBuilderSetMinMagFilter", "nvnSamplerBuilderSetWrapMode",
        "nvnSamplerBuilderSetLodClamp", "nvnSamplerBuilderSetLodBias",
        "nvnSamplerBuilderSetCompare", "nvnSamplerBuilderSetBorderColor",
        "nvnSamplerBuilderSetMaxAnisotropy", "nvnSamplerBuilderSetLodSnap",
        "nvnSamplerBuilderSetReductionFilter", "nvnSamplerBuilderGetMinMagFilter",
        "nvnSamplerBuilderGetWrapMode", "nvnSamplerBuilderGetLodClamp",
        "nvnSamplerInitialize", "nvnSamplerFinalize", "nvnSamplerGetMinMagFilter",
        "nvnSamplerGetWrapMode", "nvnSamplerGetLodClamp", "nvnSamplerGetLodBias",
        "nvnSamplerGetCompare", "nvnSamplerGetBorderColor", "nvnSamplerGetMaxAnisotropy",
        "nvnSamplerGetReductionFilter", "nvnSamplerCompare",
        "nvnSamplerPoolRegisterSampler", "nvnSamplerPoolRegisterSamplerBuilder",
        "nvnQueueBuilderSetPrimaryQueue", "nvnQueueBuilderSetDefaults", "nvnQueueBuilderSetDevice", "nvnQueueBuilderSetFlags",
        "nvnQueueBuilderSetCommandMemorySize", "nvnQueueBuilderSetComputeMemorySize",
        "nvnQueueBuilderSetControlMemorySize", "nvnQueueBuilderSetCommandFlushThreshold",
        "nvnQueueBuilderGetQueueMemorySize", "nvnQueueBuilderSetQueueMemory",
        "nvnQueueInitialize", "nvnQueueFinalize", "nvnQueueFinish",
        "nvnBufferBuilderSetDefaults", "nvnBufferBuilderSetDevice", "nvnBufferBuilderSetStorage",
        "nvnBufferBuilderGetMemoryOffset", "nvnBufferBuilderGetSize",
        "nvnBufferInitialize", "nvnBufferFinalize", "nvnBufferMap", "nvnBufferGetAddress",
        "nvnBufferGetSize", "nvnBufferGetMemoryPool", "nvnBufferGetMemoryOffset",
        "nvnBufferFlushMappedRange", "nvnBufferInvalidateMappedRange",
        "nvnEventBuilderSetDefaults", "nvnEventBuilderSetStorage", "nvnEventBuilderGetStorage",
        "nvnEventBuilderGetMemoryPool", "nvnEventBuilderGetMemoryOffset", "nvnEventInitialize", "nvnEventFinalize",
        "nvnEventGetValue", "nvnEventGetMemoryPool", "nvnEventGetMemoryOffset", "nvnEventSignal",
        "nvnSyncInitialize", "nvnSyncFinalize", "nvnSyncWait",
        "nvnCommandBufferInitialize", "nvnCommandBufferFinalize",
        "nvnCommandBufferSetMemoryCallback", "nvnCommandBufferSetMemoryCallbackData",
        "nvnCommandBufferSetCommandMemoryCallbackEnabled",
        "nvnCommandBufferAddCommandMemory", "nvnCommandBufferAddControlMemory",
        "nvnCommandBufferBeginRecording", "nvnCommandBufferEndRecording", "nvnCommandBufferIsRecording",
        "nvnCommandBufferGetCommandMemoryUsed", "nvnCommandBufferGetCommandMemorySize", "nvnCommandBufferGetCommandMemoryFree",
        "nvnCommandBufferGetControlMemoryUsed", "nvnCommandBufferSetShaderScratchMemory",
    };
    for (unsigned i=0; i<sizeof names/sizeof *names; i++) if (!strcmp(name,names[i])) return 1;
    return 0;
}

static inline int nvn2_protocol_integer(unsigned what, int *value) {
    // Protocol/shader-container versions checked against the SDK resolver and
    // constant query cases. Device-dependent capabilities are handled separately.
    switch (what) {
    case 0: *value=231; return 1;
    case 1: *value=16; return 1;
    case 0x0f: *value=32; return 1; // Texture pool entry stride; the host tracks descriptors by index.
    case 0x10: *value=32; return 1; // Sampler pool entry stride, independent of the Metal sampler object.
    case 0x11: case 0x12: *value=256; return 1; // Reserved descriptor pool IDs.
    case 0x13: *value=4; return 1; // Command data alignment.
    case 0x14: *value=8; return 1; // Command control alignment.
    case 0x15: *value=512; return 1; // Minimum command block.
    case 0x16: *value=256; return 1; // Minimum command control block.
    // The pinned LDL/STL translation uses SPIR-V Private -> MSL local arrays.
    // No external pool supplies those bytes; 0 is the actual host requirement.
    case 0x17: case 0x18: case 0x37: *value=0; return 1;
    case 0x19: *value=4096; return 1; // Audited shader scratch storage alignment.
    case 0x1a: *value=16384; return 1; // Audited scratch allocation granularity.
    case 0x27: *value=4096; return 1; // Pool texture-object alignment.
    case 0x2a: *value=1; return 1; // ASTC formats: the Apple GPU/Metal texture path supports them.
    case 0x2e: *value=201; return 1;
    case 0x2f: *value=201; return 1;
    case 0x30: *value=11; return 1;
    case 0x31: *value=11; return 1;
    // Host guest-byte layout minima (nvn_tex_layout). A direct Metal buffer
    // view is used only when its stricter device alignment also holds; otherwise
    // the existing upload path copies these rows into a separate Metal image.
    case 0x38: *value=32; return 1;
    case 0x39: *value=128; return 1;
    case 0x3a: *value=65536; return 1; // NVN pool allocation granularity, separate from Horizon process pages.
    case 0x50: case 0x56: case 0x5b: *value=4096; return 1; // Queue allocation alignment/minimums.
    case 0x51: case 0x52: *value=65536; return 1;
    case 0x53: *value=256; return 1;
    case 0x54: case 0x5c: *value=16384; return 1;
    case 0x55: *value=262144; return 1;
    case 0x58: *value=4; return 1; // Swapchain maximum textures.
    case 0x59: *value=2; return 1; // Swapchain minimum textures.
    case 0x5a: *value=0; return 1; // NVN debug layer is not supplied by this backend.
    case 0x67: *value=256; return 1; // Shader control section alignment.
    default: return 0;
    }
}
