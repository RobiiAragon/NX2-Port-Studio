// SPDX-License-Identifier: GPL-3.0-or-later
// Independently implemented NVN2 CPU state storage and checked bind codecs.
// Include after nvn_obj.h. Guest state stays in its native packed bytes; only
// successful decodes may be copied into canonical command records. This header
// does not admit API names or register wrappers. No SDK instructions are used.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if DM_GAME_NVN2
enum nvn2_state_status {
    NVN2_STATE_OK,
    NVN2_STATE_BAD_VALUE,
    NVN2_STATE_UNSUPPORTED_LOGIC_OP,
    NVN2_STATE_UNSUPPORTED_ALPHA_TEST,
    NVN2_STATE_UNSUPPORTED_ADVANCED_BLEND,
    NVN2_STATE_UNSUPPORTED_POLYGON,
    NVN2_STATE_UNSUPPORTED_MULTISAMPLE
};
enum {
    NVN2_STATE_BLEND_BYTES = 8, NVN2_STATE_COLOR_BYTES = 4,
    NVN2_STATE_CHANNEL_BYTES = 4, NVN2_STATE_DS_BYTES = 8,
    NVN2_STATE_POLYGON_BYTES = 4, NVN2_STATE_MS_BYTES = 24,
    NVN2_STATE_STREAM_BYTES = 8
};
static inline const char *nvn2_state_status_string(enum nvn2_state_status status) {
    switch (status) {
    case NVN2_STATE_OK: return "supported";
    case NVN2_STATE_BAD_VALUE: return "invalid state value";
    case NVN2_STATE_UNSUPPORTED_LOGIC_OP: return "logic operation is not implemented";
    case NVN2_STATE_UNSUPPORTED_ALPHA_TEST: return "alpha test is not implemented";
    case NVN2_STATE_UNSUPPORTED_ADVANCED_BLEND: return "advanced blend is not admitted";
    case NVN2_STATE_UNSUPPORTED_POLYGON: return "polygon mode or offset is not implemented";
    case NVN2_STATE_UNSUPPORTED_MULTISAMPLE: return "multisample feature is not admitted";
    }
    return "unknown state status";
}

// Explicit little-endian loads also handle unaligned copied objects safely.
static inline uint32_t nvn2_state_load(const void *state, unsigned byte) {
    const uint8_t *p = (const uint8_t *)state + byte;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void nvn2_state_store(void *state, unsigned byte, uint32_t value) {
    uint8_t *p = (uint8_t *)state + byte;
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static inline uint32_t nvn2_state_field(const void *s, unsigned byte, unsigned shift, uint32_t mask) {
    return nvn2_state_load(s, byte) >> shift & mask;
}
static inline enum nvn2_state_status nvn2_state_put_field(
    void *s, unsigned byte, unsigned shift, uint32_t mask, uint32_t value) {
    if (value > mask) return NVN2_STATE_BAD_VALUE;
    nvn2_state_store(s, byte, (nvn2_state_load(s, byte) & ~(mask << shift)) | value << shift);
    return NVN2_STATE_OK;
}
// Fields use their audited storage width. Semantic/backend admission happens
// at decode, so an unused unsupported state can still be built and read back.
#define NVN2_STATE_SCALAR(group, name, byte, shift, mask) \
    static inline uint32_t nvn2_state_##group##_get_##name(const void *s) { \
        return nvn2_state_field(s, byte, shift, mask); \
    } \
    static inline enum nvn2_state_status nvn2_state_##group##_set_##name(void *s, uint32_t value) { \
        return nvn2_state_put_field(s, byte, shift, mask, value); \
    }
#define NVN2_STATE_BOOL(group, name, bit) \
    static inline uint32_t nvn2_state_##group##_get_##name(const void *s) { \
        return nvn2_state_field(s, 0, bit, 1); \
    } \
    static inline enum nvn2_state_status nvn2_state_##group##_set_##name(void *s, uint32_t value) { \
        return nvn2_state_put_field(s, 0, bit, 1, (value & 255) != 0); \
    }

static inline void nvn2_state_blend_defaults(void *s) {
    nvn2_state_store(s, 0, UINT32_C(0x110c0000));
    nvn2_state_store(s, 4, UINT32_C(0x01020102));
}
NVN2_STATE_SCALAR(blend, target, 0, 0, 7)
NVN2_STATE_SCALAR(blend, advanced_mode, 0, 8, 63)
NVN2_STATE_SCALAR(blend, advanced_overlap, 0, 16, 3)
NVN2_STATE_BOOL(blend, advanced_premultiplied_src, 18)
NVN2_STATE_BOOL(blend, advanced_normalized_dst, 19)
static inline enum nvn2_state_status nvn2_state_blend_set_func(
    void *s, uint32_t sr, uint32_t dr, uint32_t sa, uint32_t da) {
    if (sr > 127 || dr > 127 || sa > 127 || da > 127) return NVN2_STATE_BAD_VALUE;
    nvn2_state_store(s, 4, (nvn2_state_load(s, 4) & UINT32_C(0x80808080)) |
        sr | dr << 8 | sa << 16 | da << 24);
    return NVN2_STATE_OK;
}
static inline void nvn2_state_blend_get_func(const void *s,
    NVNblendFunc *sr, NVNblendFunc *dr, NVNblendFunc *sa, NVNblendFunc *da) {
    *sr = (NVNblendFunc)nvn2_state_field(s, 4, 0, 127);
    *dr = (NVNblendFunc)nvn2_state_field(s, 4, 8, 127);
    *sa = (NVNblendFunc)nvn2_state_field(s, 4, 16, 127);
    *da = (NVNblendFunc)nvn2_state_field(s, 4, 24, 127);
}
static inline enum nvn2_state_status nvn2_state_blend_set_equation(void *s, uint32_t rgb, uint32_t alpha) {
    if (rgb > 7 || alpha > 7) return NVN2_STATE_BAD_VALUE;
    nvn2_state_store(s, 0, (nvn2_state_load(s, 0) & UINT32_C(0x88ffffff)) | rgb << 24 | alpha << 28);
    return NVN2_STATE_OK;
}
static inline void nvn2_state_blend_get_equation(const void *s, NVNblendEquation *rgb, NVNblendEquation *alpha) {
    *rgb = (NVNblendEquation)nvn2_state_field(s, 0, 24, 7);
    *alpha = (NVNblendEquation)nvn2_state_field(s, 0, 28, 7);
}

static inline void nvn2_state_color_defaults(void *s) { nvn2_state_store(s, 0, UINT32_C(0x08030000)); }
NVN2_STATE_SCALAR(color, logic_op, 0, 16, 255)
NVN2_STATE_SCALAR(color, alpha_test, 0, 24, 15)
static inline enum nvn2_state_status nvn2_state_color_set_blend_enable(void *s, int target, uint32_t enable) {
    if (target < 0 || target > 7) return NVN2_STATE_BAD_VALUE;
    return nvn2_state_put_field(s, 0, 8 + (unsigned)target, 1, (enable & 255) != 0);
}
static inline enum nvn2_state_status nvn2_state_color_get_blend_enable(const void *s, int target, NVNboolean *enable) {
    if (target < 0 || target > 7) return NVN2_STATE_BAD_VALUE;
    *enable = (NVNboolean)nvn2_state_field(s, 0, 8 + (unsigned)target, 1);
    return NVN2_STATE_OK;
}
static inline void nvn2_state_channel_defaults(void *s) { nvn2_state_store(s, 0, UINT32_MAX); }
static inline enum nvn2_state_status nvn2_state_channel_set_mask(
    void *s, int target, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    if (target < 0 || target > 7) return NVN2_STATE_BAD_VALUE;
    uint32_t value = ((r & 255) != 0) | (uint32_t)((g & 255) != 0) << 1 |
        (uint32_t)((b & 255) != 0) << 2 | (uint32_t)((a & 255) != 0) << 3;
    return nvn2_state_put_field(s, 0, 4 * (unsigned)target, 15, value);
}
static inline enum nvn2_state_status nvn2_state_channel_get_mask(
    const void *s, int target, NVNboolean *r, NVNboolean *g, NVNboolean *b, NVNboolean *a) {
    if (target < 0 || target > 7) return NVN2_STATE_BAD_VALUE;
    uint32_t value = nvn2_state_field(s, 0, 4 * (unsigned)target, 15);
    *r = (NVNboolean)(value & 1); *g = (NVNboolean)(value >> 1 & 1);
    *b = (NVNboolean)(value >> 2 & 1); *a = (NVNboolean)(value >> 3 & 1);
    return NVN2_STATE_OK;
}

static inline void nvn2_state_ds_defaults(void *s) {
    nvn2_state_store(s, 0, UINT32_C(0x00000022)); nvn2_state_store(s, 4, UINT32_C(0x11181118));
}
NVN2_STATE_BOOL(ds, depth_test_enable, 0)
NVN2_STATE_BOOL(ds, depth_write_enable, 1)
NVN2_STATE_BOOL(ds, stencil_test_enable, 2)
NVN2_STATE_SCALAR(ds, depth_func, 0, 4, 15)
static inline enum nvn2_state_status nvn2_state_ds_set_stencil_func(void *s, uint32_t face, uint32_t func) {
    if (face > 3 || func > 15) return NVN2_STATE_BAD_VALUE;
    if (face & 1) nvn2_state_put_field(s, 4, 0, 15, func);
    if (face & 2) nvn2_state_put_field(s, 4, 16, 15, func);
    return NVN2_STATE_OK;
}
static inline uint32_t nvn2_state_ds_get_stencil_func(const void *s, uint32_t face) {
    return nvn2_state_field(s, 4, face == 2 ? 16 : 0, 15);
}
static inline enum nvn2_state_status nvn2_state_ds_set_stencil_op(
    void *s, uint32_t face, uint32_t fail, uint32_t zfail, uint32_t zpass) {
    if (face > 3 || fail > 15 || zfail > 15 || zpass > 15) return NVN2_STATE_BAD_VALUE;
    for (unsigned side = 0; side < 2; side++) if (face & (1u << side)) {
        nvn2_state_put_field(s, 4, 4 + 16 * side, 15, fail);
        nvn2_state_put_field(s, 4, 12 + 16 * side, 15, zfail);
        nvn2_state_put_field(s, 4, 8 + 16 * side, 15, zpass);
    }
    return NVN2_STATE_OK;
}
static inline void nvn2_state_ds_get_stencil_op(const void *s, uint32_t face,
    NVNstencilOp *fail, NVNstencilOp *zfail, NVNstencilOp *zpass) {
    unsigned base = face == 2 ? 16 : 0;
    *fail = (NVNstencilOp)nvn2_state_field(s, 4, base + 4, 15);
    *zfail = (NVNstencilOp)nvn2_state_field(s, 4, base + 12, 15);
    *zpass = (NVNstencilOp)nvn2_state_field(s, 4, base + 8, 15);
}

static inline void nvn2_state_polygon_defaults(void *s) { nvn2_state_store(s, 0, UINT32_C(0x14)); }
NVN2_STATE_SCALAR(polygon, cull_face, 0, 0, 3)
NVN2_STATE_SCALAR(polygon, front_face, 0, 2, 1)
NVN2_STATE_SCALAR(polygon, mode, 0, 3, 3)
NVN2_STATE_SCALAR(polygon, offset_enables, 0, 15, 15)

static inline void nvn2_state_ms_defaults(void *s) {
    nvn2_state_store(s, 0, 5); memset((uint8_t *)s + 4, 0x88, 16); nvn2_state_store(s, 20, 0);
}
NVN2_STATE_BOOL(ms, multisample_enable, 0)
NVN2_STATE_BOOL(ms, alpha_to_coverage_enable, 1)
NVN2_STATE_BOOL(ms, alpha_to_coverage_dither, 2)
NVN2_STATE_BOOL(ms, sample_locations_enable, 3)
NVN2_STATE_BOOL(ms, sample_locations_grid_enable, 4)
NVN2_STATE_BOOL(ms, coverage_truncation_enable, 5)
NVN2_STATE_SCALAR(ms, samples, 0, 8, 31)
NVN2_STATE_SCALAR(ms, coverage_modulation_mode, 0, 13, 3)
NVN2_STATE_SCALAR(ms, raster_samples, 0, 15, 31)
NVN2_STATE_BOOL(ms, coverage_to_color_enable, 20)
NVN2_STATE_SCALAR(ms, coverage_to_color_output, 0, 21, 7)
static inline enum nvn2_state_status nvn2_state_ms_get_sample_locations_grid(const void *s, int *width, int *height) {
    uint32_t n = nvn2_state_ms_get_samples(s), raster = nvn2_state_ms_get_raster_samples(s);
    if (raster > n) n = raster;
    int w, h;
    switch (n) {
    case 0: case 1: w = 4; h = 4; break;
    case 2: w = 2; h = 4; break;
    case 4: w = 2; h = 2; break;
    case 8: w = 1; h = 2; break;
    case 16: w = 1; h = 1; break;
    default: return NVN2_STATE_BAD_VALUE; // Native getter leaves both outputs unchanged.
    }
    *width = w; *height = h; return NVN2_STATE_OK;
}
static inline uint8_t nvn2_state_ms_coordinate(float value) {
    if (!(value >= 0)) return 0; // Includes NaN, matching the CPU setter.
    if (value >= 0.9375f) return 15;
    return (uint8_t)(value * 16.0f + 0.5f);
}
static inline enum nvn2_state_status nvn2_state_ms_set_sample_locations(void *s, int start, int count, const float *xy) {
    if (count <= 0) return NVN2_STATE_OK;
    if (start < 0 || start > 16 || count > 16 - start || !xy) return NVN2_STATE_BAD_VALUE;
    // Copy before writing: the caller's input may overlap the copied state.
    float coordinates[32]; memcpy(coordinates, xy, (size_t)count * 2 * sizeof(float));
    for (int i = 0; i < count; i++) ((uint8_t *)s)[4 + start + i] =
        nvn2_state_ms_coordinate(coordinates[2 * i]) |
        (uint8_t)(nvn2_state_ms_coordinate(coordinates[2 * i + 1]) << 4);
    return NVN2_STATE_OK;
}
static inline void nvn2_state_stream_defaults(void *s) { memset(s, 0, NVN2_STATE_STREAM_BYTES); }
static inline void nvn2_state_stream_set_stride(void *s, ptrdiff_t stride) { nvn2_state_store(s, 0, (uint32_t)stride); }
static inline void nvn2_state_stream_set_divisor(void *s, int divisor) { nvn2_state_store(s, 4, (uint32_t)divisor); }
static inline ptrdiff_t nvn2_state_stream_get_stride(const void *s) { return (ptrdiff_t)(uint64_t)nvn2_state_load(s, 0); }
static inline int nvn2_state_stream_get_divisor(const void *s) { return (int32_t)nvn2_state_load(s, 4); }
#undef NVN2_STATE_SCALAR
#undef NVN2_STATE_BOOL

static inline int nvn2_state_blend_factor_valid(uint32_t value) {
    return (value >= 1 && value <= 11) || (value >= 16 && value <= 19) || (value >= 97 && value <= 100);
}
static inline enum nvn2_state_status nvn2_state_decode_blend(blend_st *out, const void *s) {
    if (!out || !s) return NVN2_STATE_BAD_VALUE;
    if (nvn2_state_blend_get_advanced_mode(s)) return NVN2_STATE_UNSUPPORTED_ADVANCED_BLEND;
    blend_st value; memset(&value, 0, sizeof value);
    uint32_t sr = nvn2_state_field(s, 4, 0, 127), dr = nvn2_state_field(s, 4, 8, 127);
    uint32_t sa = nvn2_state_field(s, 4, 16, 127), da = nvn2_state_field(s, 4, 24, 127);
    uint32_t er = nvn2_state_field(s, 0, 24, 7), ea = nvn2_state_field(s, 0, 28, 7);
    if (!nvn2_state_blend_factor_valid(sr) || !nvn2_state_blend_factor_valid(dr) ||
        !nvn2_state_blend_factor_valid(sa) || !nvn2_state_blend_factor_valid(da) ||
        er < 1 || er > 5 || ea < 1 || ea > 5) return NVN2_STATE_BAD_VALUE;
    value.target = nvn2_state_blend_get_target(s);
    value.src_rgb = sr; value.dst_rgb = dr; value.src_a = sa; value.dst_a = da;
    value.eq_rgb = er; value.eq_a = ea;
    value.adv_npm = !nvn2_state_blend_get_advanced_premultiplied_src(s);
    value.adv_ovl = nvn2_state_blend_get_advanced_overlap(s);
    // normalizedDst and overlap only affect advanced mode, rejected above.
    memcpy(out, &value, sizeof value); return NVN2_STATE_OK;
}
static inline enum nvn2_state_status nvn2_state_decode_color(color_st *out, const void *s) {
    if (!out || !s) return NVN2_STATE_BAD_VALUE;
    if (nvn2_state_color_get_logic_op(s) != NVN_LOGIC_OP_COPY) return NVN2_STATE_UNSUPPORTED_LOGIC_OP;
    if (nvn2_state_color_get_alpha_test(s) != NVN_ALPHA_FUNC_ALWAYS) return NVN2_STATE_UNSUPPORTED_ALPHA_TEST;
    color_st value; memset(&value, 0, sizeof value);
    value.blend_en = nvn2_state_field(s, 0, 8, 255);
    value.logic_op = NVN_LOGIC_OP_COPY; value.alpha_func = NVN_ALPHA_FUNC_ALWAYS;
    memcpy(out, &value, sizeof value); return NVN2_STATE_OK;
}
static inline enum nvn2_state_status nvn2_state_decode_channel(chmask_st *out, const void *s) {
    if (!out || !s) return NVN2_STATE_BAD_VALUE;
    _Static_assert(sizeof(chmask_st) == NVN2_STATE_CHANNEL_BYTES, "channel state remains compatible");
    chmask_st value = {nvn2_state_load(s, 0)}; memcpy(out, &value, sizeof value); return NVN2_STATE_OK;
}
static inline enum nvn2_state_status nvn2_state_decode_ds(ds_st *out, const void *s) {
    if (!out || !s) return NVN2_STATE_BAD_VALUE;
    uint32_t function = nvn2_state_ds_get_depth_func(s);
    uint32_t sf = nvn2_state_ds_get_stencil_func(s, 1), sb = nvn2_state_ds_get_stencil_func(s, 2);
    NVNstencilOp ff, zff, zpf, fb, zfb, zpb;
    nvn2_state_ds_get_stencil_op(s, 1, &ff, &zff, &zpf);
    nvn2_state_ds_get_stencil_op(s, 2, &fb, &zfb, &zpb);
    if (function < 1 || function > 8 || sf < 1 || sf > 8 || sb < 1 || sb > 8 ||
        ff < 1 || ff > 8 || zff < 1 || zff > 8 || zpf < 1 || zpf > 8 ||
        fb < 1 || fb > 8 || zfb < 1 || zfb > 8 || zpb < 1 || zpb > 8) return NVN2_STATE_BAD_VALUE;
    ds_st value; memset(&value, 0, sizeof value);
    value.dt = nvn2_state_ds_get_depth_test_enable(s); value.dw = nvn2_state_ds_get_depth_write_enable(s);
    value.df = function; value.st = nvn2_state_ds_get_stencil_test_enable(s);
    value.sf_f = sf; value.sf_b = sb;
    value.fail_f = ff; value.zfail_f = zff; value.zpass_f = zpf;
    value.fail_b = fb; value.zfail_b = zfb; value.zpass_b = zpb;
    memcpy(out, &value, sizeof value); return NVN2_STATE_OK;
}
static inline enum nvn2_state_status nvn2_state_decode_polygon(poly_st *out, const void *s) {
    if (!out || !s) return NVN2_STATE_BAD_VALUE;
    uint32_t mode = nvn2_state_polygon_get_mode(s), offset = nvn2_state_polygon_get_offset_enables(s);
    if (mode > NVN_POLYGON_MODE_FILL) return NVN2_STATE_BAD_VALUE;
    // Metal replay supports line/fill mode and fill offset. Point mode, point/
    // line offset and the native fourth offset bit have no implementation.
    if (mode == NVN_POLYGON_MODE_POINT || (offset & ~4u) ||
        (offset && mode != NVN_POLYGON_MODE_FILL)) return NVN2_STATE_UNSUPPORTED_POLYGON;
    poly_st value; memset(&value, 0, sizeof value);
    value.cull = nvn2_state_polygon_get_cull_face(s); value.front = nvn2_state_polygon_get_front_face(s);
    value.mode = mode; value.offset_en = offset;
    memcpy(out, &value, sizeof value); return NVN2_STATE_OK;
}
static inline enum nvn2_state_status nvn2_state_decode_ms(ms_st *out, const void *s) {
    if (!out || !s) return NVN2_STATE_BAD_VALUE;
    // Actual NVN2 multisample Metal creation is still gated. Keep CPU state
    // faithful, but admit only the single-sample behavior currently rendered.
    if (nvn2_state_ms_get_samples(s) > 1 || nvn2_state_ms_get_alpha_to_coverage_enable(s) ||
        nvn2_state_ms_get_raster_samples(s) || nvn2_state_ms_get_coverage_modulation_mode(s) ||
        nvn2_state_ms_get_coverage_to_color_enable(s) || nvn2_state_ms_get_coverage_to_color_output(s) ||
        nvn2_state_ms_get_sample_locations_enable(s) || nvn2_state_ms_get_sample_locations_grid_enable(s) ||
        nvn2_state_ms_get_coverage_truncation_enable(s)) return NVN2_STATE_UNSUPPORTED_MULTISAMPLE;
    ms_st value; memset(&value, 0, sizeof value);
    value.enable = (uint8_t)nvn2_state_ms_get_multisample_enable(s);
    value.samples = (uint8_t)nvn2_state_ms_get_samples(s);
    value.a2c_dither = (uint8_t)nvn2_state_ms_get_alpha_to_coverage_dither(s);
    memcpy(out, &value, sizeof value); return NVN2_STATE_OK;
}
static inline enum nvn2_state_status nvn2_state_decode_stream(vs_st *out, const void *s) {
    if (!out || !s) return NVN2_STATE_BAD_VALUE;
    _Static_assert(sizeof(vs_st) == NVN2_STATE_STREAM_BYTES, "vertex stream state remains compatible");
    vs_st value = {nvn2_state_load(s, 0), nvn2_state_load(s, 4)};
    memcpy(out, &value, sizeof value); return NVN2_STATE_OK;
}
#endif // DM_GAME_NVN2
