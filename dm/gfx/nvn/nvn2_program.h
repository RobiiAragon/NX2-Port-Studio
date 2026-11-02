// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct {
    uint64_t reserved; void *device; uint64_t reserved10;
    uint32_t sample_shading, hash_count; const void *hashes;
    int32_t shader_count; uint32_t padding2c; const void *shaders;
    uint32_t ray_count, padding3c; const void *ray_modules;
    uint32_t linkage_count, padding4c; const void *linkage;
    uint32_t trace_depth, callable_depth, ray_flags, padding64;
} nvn2_program_builder;
_Static_assert(sizeof(nvn2_program_builder)==104 && offsetof(nvn2_program_builder,shaders)==48 && offsetof(nvn2_program_builder,ray_flags)==96,"NVN2 program builder ABI");
#define NVN2_PROGRAM_API(X) \
    X(nvnProgramBuilderSetDefaults) X(nvnProgramBuilderSetDevice) X(nvnProgramBuilderGetDevice) \
    X(nvnProgramBuilderSetShaders) X(nvnProgramBuilderGetShaders) X(nvnProgramBuilderGetShaderCount) \
    X(nvnProgramBuilderSetSampleShading) X(nvnProgramBuilderGetSampleShading) \
    X(nvnProgramInitializeFromBuilder) X(nvnProgramSetShaders) X(nvnProgramInitialize) X(nvnProgramFinalize)
static inline int nvn2_program_abi_verified(const char *name) {
#define N2_PROGRAM_NAME(n) if (!strcmp(name,#n)) return 1;
    NVN2_PROGRAM_API(N2_PROGRAM_NAME)
#undef N2_PROGRAM_NAME
    return 0;
}
