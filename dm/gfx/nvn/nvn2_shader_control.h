// SPDX-License-Identifier: GPL-3.0-or-later
// Audited NVN2 201.11 serialized control metadata, independent of NVN1.
// The caller supplies actual readable extents from its memory mapping. This
// reader neither resolves GPU addresses nor assumes an arbitrary pointer valid.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum nvn2_ctl_result {
    NVN2_CTL_OK = 0,
    NVN2_CTL_TRUNCATED,
    NVN2_CTL_MAGIC,
    NVN2_CTL_VERSION,
    NVN2_CTL_SIZE,
    NVN2_CTL_STAGE,
    NVN2_CTL_COMPILER_RANGE,
    NVN2_CTL_CODE_RANGE,
    NVN2_CTL_CODE_STAGE,
};

struct nvn2_ctl_info {
    uint32_t control_bytes;
    uint32_t code_bytes;
    uint32_t compiler_offset;
    uint32_t compiler_bytes;
    // API enumeration: vertex0, fragment1, geometry2, hull3, domain4, compute5.
    // This is different from the native graphics header's stage enumeration.
    uint8_t stage;
};

static inline uint32_t nvn2_ctl_u32(const unsigned char *p) {
    // Serialized little-endian fields; no unaligned or aliasing-unsafe loads.
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline enum nvn2_ctl_result nvn2_ctl_read(const void *control,
                                                size_t available,
                                                struct nvn2_ctl_info *out) {
    struct nvn2_ctl_info info = {0};
    const unsigned char *p = (const unsigned char *)control;
    if (!p || available < 0xd8) return NVN2_CTL_TRUNCATED;
    if (nvn2_ctl_u32(p) != UINT32_C(0x98761234)) return NVN2_CTL_MAGIC;
    if (nvn2_ctl_u32(p + 4) != 201 || nvn2_ctl_u32(p + 8) != 11)
        return NVN2_CTL_VERSION;
    // Each size is independently a u32. The adjacent u32 is not its high half.
    info.control_bytes = nvn2_ctl_u32(p + 0x30);
    info.code_bytes = nvn2_ctl_u32(p + 0x38);
    info.compiler_offset = nvn2_ctl_u32(p + 0xbc);
    info.compiler_bytes = nvn2_ctl_u32(p + 0xc0);
    info.stage = p[0x56];
    if (info.control_bytes < 0xd8 || info.control_bytes > UINT32_C(0x1000000) ||
        !info.code_bytes || info.code_bytes > UINT32_C(0x4000000))
        return NVN2_CTL_SIZE;
    if (info.control_bytes > available) return NVN2_CTL_TRUNCATED;
    if (info.stage > 5) return NVN2_CTL_STAGE;
    if (info.compiler_offset < 0xd8 || info.compiler_offset > info.control_bytes ||
        info.compiler_bytes > info.control_bytes - info.compiler_offset)
        return NVN2_CTL_COMPILER_RANGE;
    if (out) *out = info;
    return NVN2_CTL_OK;
}

static inline enum nvn2_ctl_result nvn2_ctl_check_graphics_code(
    const struct nvn2_ctl_info *info, const void *code, size_t available) {
    // Describing a stage does not admit it to the native lowerer or runtime.
    static const unsigned char sph_stage[5] = {1, 5, 4, 2, 3};
    if (!info || info->stage >= 5) return NVN2_CTL_STAGE;
    if (!code || info->code_bytes < 144 || info->code_bytes > available ||
        (info->code_bytes - 128) % 16) return NVN2_CTL_CODE_RANGE;
    if (((nvn2_ctl_u32((const unsigned char *)code) >> 10) & 15) != sph_stage[info->stage])
        return NVN2_CTL_CODE_STAGE;
    return NVN2_CTL_OK;
}

static inline const char *nvn2_ctl_error(enum nvn2_ctl_result result) {
    switch (result) {
    case NVN2_CTL_OK: return "valid metadata";
    case NVN2_CTL_TRUNCATED: return "control exceeds known readable CPU extent";
    case NVN2_CTL_MAGIC: return "control magic does not match";
    case NVN2_CTL_VERSION: return "control version is not audited (requires 201.11)";
    case NVN2_CTL_SIZE: return "invalid declared control or code size";
    case NVN2_CTL_STAGE: return "stage is not audited for this operation";
    case NVN2_CTL_COMPILER_RANGE: return "compiler data exceeds control or overlaps header";
    case NVN2_CTL_CODE_RANGE: return "graphics code exceeds resolved pool extent or has invalid framing";
    case NVN2_CTL_CODE_STAGE: return "native graphics stage disagrees with control stage";
    }
    return "unknown control parser error";
}
