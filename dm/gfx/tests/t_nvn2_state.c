// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic CPU fixture: literal native vectors were derived independently
// from the state ABI audit. No game files, SDK code, Metal or profile required.
// clang -std=c11 -Wall -Wextra -Werror -I dm/gfx/nvn/include \
//   dm/gfx/tests/t_nvn2_state.c -o .nx2-build/t_nvn2_state
#define DM_GAME_NVN2 1
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../nvn/nvn_obj.h"
#include "../nvn/nvn2_state.h"

typedef struct { uint8_t bytes[64]; } guarded;
static uint8_t *reset(guarded *g) { memset(g, 0xa5, sizeof *g); return g->bytes + 17; }
static void guards(const guarded *g, size_t extent) {
    for (size_t i = 0; i < 17; i++) assert(g->bytes[i] == 0xa5);
    for (size_t i = 17 + extent; i < sizeof g->bytes; i++) assert(g->bytes[i] == 0xa5);
}
static void expect(const guarded *g, const uint8_t *expected, size_t extent) {
    assert(!memcmp(g->bytes + 17, expected, extent)); guards(g, extent);
}
#define OK(call) assert((call) == NVN2_STATE_OK)
#define REJECT_UNCHANGED(call, output, expected_status) do { \
    uint8_t previous[sizeof(output)]; memcpy(previous, &(output), sizeof(output)); \
    assert((call) == (expected_status)); assert(!memcmp(previous, &(output), sizeof(output))); \
} while (0)

static void blend_fixture(void) {
    guarded g; uint8_t *p = reset(&g);
    static const uint8_t defaults[] = {0, 0, 0x0c, 0x11, 2, 1, 2, 1};
    nvn2_state_blend_defaults(p); expect(&g, defaults, sizeof defaults);
    blend_st canonical; memset(&canonical, 0xcc, sizeof canonical);
    OK(nvn2_state_decode_blend(&canonical, p));
    assert(canonical.target == 0 && canonical.src_rgb == 2 && canonical.dst_rgb == 1 &&
        canonical.src_a == 2 && canonical.dst_a == 1 && canonical.eq_rgb == 1 && canonical.eq_a == 1 &&
        canonical.adv == 0 && canonical.adv_npm == 0 && canonical.adv_ovl == 0);
    static const uint8_t reserved[] = {0xf8, 0xc0, 0xf0, 0x88, 0x80, 0x80, 0x80, 0x80};
    memcpy(p, reserved, sizeof reserved);
    OK(nvn2_state_blend_set_target(p, 5));
    OK(nvn2_state_blend_set_advanced_mode(p, 50));
    OK(nvn2_state_blend_set_advanced_overlap(p, 2));
    OK(nvn2_state_blend_set_advanced_premultiplied_src(p, 0x101));
    assert(nvn2_state_blend_get_advanced_premultiplied_src(p) == 1);
    OK(nvn2_state_blend_set_advanced_premultiplied_src(p, 0x100));
    OK(nvn2_state_blend_set_advanced_normalized_dst(p, 0x101));
    OK(nvn2_state_blend_set_equation(p, 3, 5));
    OK(nvn2_state_blend_set_func(p, 97, 17, 5, 8));
    static const uint8_t packed[] = {0xfd, 0xf2, 0xfa, 0xdb, 0xe1, 0x91, 0x85, 0x88};
    expect(&g, packed, sizeof packed);
    assert(nvn2_state_blend_get_target(p) == 5 && nvn2_state_blend_get_advanced_mode(p) == 50 &&
        nvn2_state_blend_get_advanced_overlap(p) == 2 &&
        nvn2_state_blend_get_advanced_premultiplied_src(p) == 0 &&
        nvn2_state_blend_get_advanced_normalized_dst(p) == 1);
    _Static_assert(sizeof(NVNblendFunc) == 4 && sizeof(NVNblendEquation) == 4, "enum output ABI");
    NVNblendFunc factors[6] = {NVN_BLEND_FUNC_LARGE, 0, 0, 0, 0, NVN_BLEND_FUNC_LARGE};
    nvn2_state_blend_get_func(p, &factors[1], &factors[2], &factors[3], &factors[4]);
    assert(factors[0] == NVN_BLEND_FUNC_LARGE && factors[5] == NVN_BLEND_FUNC_LARGE &&
        factors[1] == 97 && factors[2] == 17 && factors[3] == 5 && factors[4] == 8);
    NVNblendEquation equations[4] = {NVN_BLEND_EQUATION_LARGE, 0, 0, NVN_BLEND_EQUATION_LARGE};
    nvn2_state_blend_get_equation(p, &equations[1], &equations[2]);
    assert(equations[0] == NVN_BLEND_EQUATION_LARGE && equations[3] == NVN_BLEND_EQUATION_LARGE &&
        equations[1] == 3 && equations[2] == 5);
    REJECT_UNCHANGED(nvn2_state_decode_blend(&canonical, p), canonical, NVN2_STATE_UNSUPPORTED_ADVANCED_BLEND);
    assert(nvn2_state_blend_set_target(p, 8) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_blend_set_func(p, 128, 1, 1, 1) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_blend_set_equation(p, 1, 8) == NVN2_STATE_BAD_VALUE);
    expect(&g, packed, sizeof packed);
    OK(nvn2_state_blend_set_advanced_mode(p, 0));
    OK(nvn2_state_decode_blend(&canonical, p));
    assert(canonical.target == 5 && canonical.src_rgb == 97 && canonical.dst_rgb == 17 &&
        canonical.src_a == 5 && canonical.dst_a == 8 && canonical.eq_rgb == 3 && canonical.eq_a == 5 &&
        canonical.adv == 0 && canonical.adv_npm == 1 && canonical.adv_ovl == 2);
    // A copied native value remains self-contained after its source changes.
    uint8_t copy[8]; memcpy(copy, p, sizeof copy); nvn2_state_blend_defaults(p);
    OK(nvn2_state_decode_blend(&canonical, copy)); assert(canonical.target == 5 && canonical.src_rgb == 97);
    OK(nvn2_state_blend_set_func(copy, 12, 1, 2, 1));
    REJECT_UNCHANGED(nvn2_state_decode_blend(&canonical, copy), canonical, NVN2_STATE_BAD_VALUE);
    guards(&g, 8);
}

static void color_channel_fixture(void) {
    guarded g; uint8_t *p = reset(&g); color_st color;
    static const uint8_t defaults[] = {0, 0, 3, 8};
    nvn2_state_color_defaults(p); expect(&g, defaults, 4);
    OK(nvn2_state_decode_color(&color, p)); assert(color.blend_en == 0 && color.logic_op == 3 && color.alpha_func == 8);
    p[0] = 0x5a; p[3] = 0xa8; // Reserved prefix and high alpha byte bits survive setters.
    OK(nvn2_state_color_set_blend_enable(p, 0, 0x100));
    OK(nvn2_state_color_set_blend_enable(p, 3, 0x101));
    OK(nvn2_state_color_set_blend_enable(p, 7, 2));
    OK(nvn2_state_color_set_logic_op(p, 0x96)); OK(nvn2_state_color_set_alpha_test(p, 15));
    static const uint8_t packed[] = {0x5a, 0x88, 0x96, 0xaf}; expect(&g, packed, 4);
    assert(nvn2_state_color_get_logic_op(p) == 0x96 && nvn2_state_color_get_alpha_test(p) == 15);
    NVNboolean enabled[3] = {0xa5, 0, 0xa5};
    OK(nvn2_state_color_get_blend_enable(p, 3, &enabled[1]));
    assert(enabled[0] == 0xa5 && enabled[1] == 1 && enabled[2] == 0xa5);
    assert(nvn2_state_color_set_blend_enable(p, 8, 1) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_color_get_blend_enable(p, -1, &enabled[1]) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_color_set_logic_op(p, 256) == NVN2_STATE_BAD_VALUE); expect(&g, packed, 4);
    REJECT_UNCHANGED(nvn2_state_decode_color(&color, p), color, NVN2_STATE_UNSUPPORTED_LOGIC_OP);
    OK(nvn2_state_color_set_logic_op(p, 3));
    REJECT_UNCHANGED(nvn2_state_decode_color(&color, p), color, NVN2_STATE_UNSUPPORTED_ALPHA_TEST);
    OK(nvn2_state_color_set_alpha_test(p, 8)); OK(nvn2_state_decode_color(&color, p));
    assert(color.blend_en == 0x88 && color.logic_op == 3 && color.alpha_func == 8); guards(&g, 4);

    p = reset(&g); nvn2_state_channel_defaults(p);
    static const uint8_t all_channels[] = {255, 255, 255, 255}; expect(&g, all_channels, 4);
    OK(nvn2_state_channel_set_mask(p, 3, 0x100, 0x101, 2, 0));
    OK(nvn2_state_channel_set_mask(p, 7, 1, 0, 1, 0));
    static const uint8_t mask[] = {0xff, 0x6f, 0xff, 0x5f}; expect(&g, mask, 4);
    NVNboolean channels[8]; memset(channels, 0xa5, sizeof channels);
    OK(nvn2_state_channel_get_mask(p, 7, &channels[2], &channels[3], &channels[4], &channels[5]));
    static const uint8_t outputs[] = {0xa5, 0xa5, 1, 0, 1, 0, 0xa5, 0xa5};
    assert(!memcmp(channels, outputs, sizeof outputs));
    assert(nvn2_state_channel_set_mask(p, -1, 0, 0, 0, 0) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_channel_get_mask(p, 8, &channels[2], &channels[3], &channels[4], &channels[5]) == NVN2_STATE_BAD_VALUE);
    expect(&g, mask, 4); assert(!memcmp(channels, outputs, sizeof outputs));
    chmask_st canonical; OK(nvn2_state_decode_channel(&canonical, p)); assert(canonical.mask == UINT32_C(0x5fff6fff));
}

static void depth_polygon_fixture(void) {
    guarded g; uint8_t *p = reset(&g); ds_st depth;
    static const uint8_t defaults[] = {0x22, 0, 0, 0, 0x18, 0x11, 0x18, 0x11};
    nvn2_state_ds_defaults(p); expect(&g, defaults, 8); OK(nvn2_state_decode_ds(&depth, p));
    assert(depth.dt == 0 && depth.dw == 1 && depth.df == 2 && depth.st == 0 &&
        depth.sf_f == 8 && depth.sf_b == 8 && depth.fail_f == 1 && depth.zfail_f == 1 &&
        depth.zpass_f == 1 && depth.fail_b == 1 && depth.zfail_b == 1 && depth.zpass_b == 1);
    static const uint8_t reserved[] = {8, 0x5a, 0xc3, 0xf0, 0, 0, 0, 0}; memcpy(p, reserved, 8);
    OK(nvn2_state_ds_set_depth_test_enable(p, 0x101)); OK(nvn2_state_ds_set_depth_write_enable(p, 0x100));
    OK(nvn2_state_ds_set_stencil_test_enable(p, 2)); OK(nvn2_state_ds_set_depth_func(p, 7));
    OK(nvn2_state_ds_set_stencil_func(p, 1, 3)); OK(nvn2_state_ds_set_stencil_func(p, 2, 6));
    OK(nvn2_state_ds_set_stencil_op(p, 1, 2, 4, 8)); OK(nvn2_state_ds_set_stencil_op(p, 2, 5, 7, 6));
    static const uint8_t packed[] = {0x7d, 0x5a, 0xc3, 0xf0, 0x23, 0x48, 0x56, 0x76}; expect(&g, packed, 8);
    assert(nvn2_state_ds_get_depth_test_enable(p) == 1 && nvn2_state_ds_get_depth_write_enable(p) == 0 &&
        nvn2_state_ds_get_stencil_test_enable(p) == 1 && nvn2_state_ds_get_depth_func(p) == 7 &&
        nvn2_state_ds_get_stencil_func(p, 1) == 3 && nvn2_state_ds_get_stencil_func(p, 2) == 6 &&
        nvn2_state_ds_get_stencil_func(p, 3) == 3);
    NVNstencilOp ops[5] = {NVN_STENCIL_OP_LARGE, 0, 0, 0, NVN_STENCIL_OP_LARGE};
    nvn2_state_ds_get_stencil_op(p, 2, &ops[1], &ops[2], &ops[3]);
    assert(ops[0] == NVN_STENCIL_OP_LARGE && ops[1] == 5 && ops[2] == 7 && ops[3] == 6 && ops[4] == NVN_STENCIL_OP_LARGE);
    OK(nvn2_state_decode_ds(&depth, p));
    assert(depth.dt == 1 && depth.dw == 0 && depth.df == 7 && depth.st == 1 && depth.sf_f == 3 && depth.sf_b == 6 &&
        depth.fail_f == 2 && depth.zfail_f == 4 && depth.zpass_f == 8 && depth.fail_b == 5 && depth.zfail_b == 7 && depth.zpass_b == 6);
    assert(nvn2_state_ds_set_stencil_op(p, 3, 1, 16, 1) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_ds_set_stencil_func(p, 4, 1) == NVN2_STATE_BAD_VALUE); expect(&g, packed, 8);
    OK(nvn2_state_ds_set_stencil_op(p, 0, 1, 1, 1)); expect(&g, packed, 8);
    OK(nvn2_state_ds_set_stencil_op(p, 3, 1, 2, 3));
    static const uint8_t both[] = {0x7d, 0x5a, 0xc3, 0xf0, 0x13, 0x23, 0x16, 0x23}; expect(&g, both, 8);
    OK(nvn2_state_ds_set_stencil_func(p, 3, 8));
    assert(nvn2_state_ds_get_stencil_func(p, 1) == 8 && nvn2_state_ds_get_stencil_func(p, 2) == 8);
    OK(nvn2_state_ds_set_depth_func(p, 0));
    REJECT_UNCHANGED(nvn2_state_decode_ds(&depth, p), depth, NVN2_STATE_BAD_VALUE); guards(&g, 8);

    p = reset(&g); poly_st polygon; nvn2_state_polygon_defaults(p);
    static const uint8_t poly_default[] = {0x14, 0, 0, 0}; expect(&g, poly_default, 4);
    OK(nvn2_state_decode_polygon(&polygon, p)); assert(polygon.cull == 0 && polygon.front == 1 && polygon.mode == 2 && polygon.offset_en == 0);
    static const uint8_t poly_reserved[] = {0xe0, 0x7f, 0xf8, 0xa5}; memcpy(p, poly_reserved, 4);
    OK(nvn2_state_polygon_set_cull_face(p, 2)); OK(nvn2_state_polygon_set_front_face(p, 1));
    OK(nvn2_state_polygon_set_mode(p, 1)); OK(nvn2_state_polygon_set_offset_enables(p, 4));
    static const uint8_t poly_line[] = {0xee, 0x7f, 0xfa, 0xa5}; expect(&g, poly_line, 4);
    assert(nvn2_state_polygon_get_cull_face(p) == 2 && nvn2_state_polygon_get_front_face(p) == 1 &&
        nvn2_state_polygon_get_mode(p) == 1 && nvn2_state_polygon_get_offset_enables(p) == 4);
    REJECT_UNCHANGED(nvn2_state_decode_polygon(&polygon, p), polygon, NVN2_STATE_UNSUPPORTED_POLYGON);
    OK(nvn2_state_polygon_set_mode(p, 2)); OK(nvn2_state_decode_polygon(&polygon, p));
    assert(polygon.cull == 2 && polygon.front == 1 && polygon.mode == 2 && polygon.offset_en == 4);
    OK(nvn2_state_polygon_set_offset_enables(p, 8));
    static const uint8_t poly_fourth_bit[] = {0xf6, 0x7f, 0xfc, 0xa5}; expect(&g, poly_fourth_bit, 4);
    REJECT_UNCHANGED(nvn2_state_decode_polygon(&polygon, p), polygon, NVN2_STATE_UNSUPPORTED_POLYGON);
    OK(nvn2_state_polygon_set_offset_enables(p, 0)); OK(nvn2_state_polygon_set_mode(p, 0));
    REJECT_UNCHANGED(nvn2_state_decode_polygon(&polygon, p), polygon, NVN2_STATE_UNSUPPORTED_POLYGON);
    OK(nvn2_state_polygon_set_mode(p, 3));
    REJECT_UNCHANGED(nvn2_state_decode_polygon(&polygon, p), polygon, NVN2_STATE_BAD_VALUE); guards(&g, 4);
}

static void multisample_stream_fixture(void) {
    guarded g; uint8_t *p = reset(&g); ms_st sample;
    static const uint8_t defaults[] = {5, 0, 0, 0,
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0, 0, 0, 0};
    nvn2_state_ms_defaults(p); expect(&g, defaults, 24); OK(nvn2_state_decode_ms(&sample, p));
    assert(sample.enable == 1 && sample.samples == 0 && sample.a2c == 0 && sample.a2c_dither == 1 &&
        sample.raster_samples == 0 && sample.cov_mod == 0 && sample.cov2color_en == 0 &&
        sample.cov2color_out == 0 && sample.sl_en == 0 && sample.sl_grid == 0);
    p[0] = 0xc5; p[3] = 0xa7;
    static const uint8_t ms_reserved_tail[] = {0x12, 0x34, 0x56, 0x78}; memcpy(p + 20, ms_reserved_tail, 4);
    OK(nvn2_state_ms_set_multisample_enable(p, 0x100)); OK(nvn2_state_ms_set_alpha_to_coverage_enable(p, 0x101));
    OK(nvn2_state_ms_set_alpha_to_coverage_dither(p, 0x100)); OK(nvn2_state_ms_set_sample_locations_enable(p, 2));
    OK(nvn2_state_ms_set_sample_locations_grid_enable(p, 1)); OK(nvn2_state_ms_set_coverage_truncation_enable(p, 0x101));
    OK(nvn2_state_ms_set_samples(p, 16)); OK(nvn2_state_ms_set_coverage_modulation_mode(p, 3));
    OK(nvn2_state_ms_set_raster_samples(p, 8)); OK(nvn2_state_ms_set_coverage_to_color_enable(p, 1));
    OK(nvn2_state_ms_set_coverage_to_color_output(p, 5));
    static const uint8_t packed[] = {0xfa, 0x70, 0xb4, 0xa7,
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x12, 0x34, 0x56, 0x78}; expect(&g, packed, 24);
    assert(nvn2_state_ms_get_multisample_enable(p) == 0 && nvn2_state_ms_get_alpha_to_coverage_enable(p) == 1 &&
        nvn2_state_ms_get_alpha_to_coverage_dither(p) == 0 && nvn2_state_ms_get_sample_locations_enable(p) == 1 &&
        nvn2_state_ms_get_sample_locations_grid_enable(p) == 1 && nvn2_state_ms_get_coverage_truncation_enable(p) == 1 &&
        nvn2_state_ms_get_samples(p) == 16 && nvn2_state_ms_get_coverage_modulation_mode(p) == 3 &&
        nvn2_state_ms_get_raster_samples(p) == 8 && nvn2_state_ms_get_coverage_to_color_enable(p) == 1 &&
        nvn2_state_ms_get_coverage_to_color_output(p) == 5);
    const float xy[] = {-1, 2, 0, .5f, .03125f, .09375f, .9375f, 1,
        NAN, INFINITY, -INFINITY, .46875f, .1f, .9f, .49f, .51f};
    OK(nvn2_state_ms_set_sample_locations(p, 3, 8, xy));
    static const uint8_t locations[] = {0xfa, 0x70, 0xb4, 0xa7,
        0x88, 0x88, 0x88, 0xf0, 0x80, 0x21, 0xff, 0xf0,
        0x80, 0xe2, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x12, 0x34, 0x56, 0x78}; expect(&g, locations, 24);
    assert(nvn2_state_ms_set_sample_locations(p, 15, 2, xy) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_ms_set_sample_locations(p, -1, 1, xy) == NVN2_STATE_BAD_VALUE);
    assert(nvn2_state_ms_set_samples(p, 32) == NVN2_STATE_BAD_VALUE);
    OK(nvn2_state_ms_set_sample_locations(p, -1, 0, NULL)); expect(&g, locations, 24);
    REJECT_UNCHANGED(nvn2_state_decode_ms(&sample, p), sample, NVN2_STATE_UNSUPPORTED_MULTISAMPLE);
    static const int grids[][3] = {{0, 4, 4}, {1, 4, 4}, {2, 2, 4}, {4, 2, 2}, {8, 1, 2}, {16, 1, 1}};
    nvn2_state_ms_defaults(p);
    for (unsigned i = 0; i < sizeof grids / sizeof *grids; i++) {
        int w = -1, h = -1; OK(nvn2_state_ms_set_samples(p, (uint32_t)grids[i][0]));
        OK(nvn2_state_ms_get_sample_locations_grid(p, &w, &h)); assert(w == grids[i][1] && h == grids[i][2]);
    }
    OK(nvn2_state_ms_set_samples(p, 2)); OK(nvn2_state_ms_set_raster_samples(p, 8));
    int w = 99, h = 88; OK(nvn2_state_ms_get_sample_locations_grid(p, &w, &h)); assert(w == 1 && h == 2);
    OK(nvn2_state_ms_set_raster_samples(p, 0)); OK(nvn2_state_ms_set_samples(p, 3));
    w = 99; h = 88; assert(nvn2_state_ms_get_sample_locations_grid(p, &w, &h) == NVN2_STATE_BAD_VALUE && w == 99 && h == 88);
    struct unsupported { enum nvn2_state_status (*set)(void *, uint32_t); uint32_t value; } unsupported[] = {
        {nvn2_state_ms_set_samples, 2}, {nvn2_state_ms_set_alpha_to_coverage_enable, 1},
        {nvn2_state_ms_set_raster_samples, 1}, {nvn2_state_ms_set_coverage_modulation_mode, 1},
        {nvn2_state_ms_set_coverage_to_color_enable, 1}, {nvn2_state_ms_set_coverage_to_color_output, 1},
        {nvn2_state_ms_set_sample_locations_enable, 1}, {nvn2_state_ms_set_sample_locations_grid_enable, 1},
        {nvn2_state_ms_set_coverage_truncation_enable, 1}
    };
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; i++) {
        nvn2_state_ms_defaults(p); OK(unsupported[i].set(p, unsupported[i].value));
        REJECT_UNCHANGED(nvn2_state_decode_ms(&sample, p), sample, NVN2_STATE_UNSUPPORTED_MULTISAMPLE);
    }
    nvn2_state_ms_defaults(p); OK(nvn2_state_ms_set_samples(p, 1));
    OK(nvn2_state_ms_set_multisample_enable(p, 0)); OK(nvn2_state_ms_set_alpha_to_coverage_dither(p, 0));
    OK(nvn2_state_decode_ms(&sample, p)); assert(sample.enable == 0 && sample.samples == 1 && sample.a2c_dither == 0); guards(&g, 24);

    p = reset(&g); vs_st stream; nvn2_state_stream_defaults(p);
    static const uint8_t stream_default[8] = {0}; expect(&g, stream_default, 8);
    nvn2_state_stream_set_stride(p, (ptrdiff_t)UINT64_C(0x1000000f3)); nvn2_state_stream_set_divisor(p, -7);
    static const uint8_t stream_packed[] = {0xf3, 0, 0, 0, 0xf9, 0xff, 0xff, 0xff}; expect(&g, stream_packed, 8);
    assert(nvn2_state_stream_get_stride(p) == 243 && nvn2_state_stream_get_divisor(p) == -7);
    OK(nvn2_state_decode_stream(&stream, p)); assert(stream.stride == 243 && stream.divisor == UINT32_C(0xfffffff9));
    nvn2_state_stream_set_stride(p, -1); assert(nvn2_state_stream_get_stride(p) == (ptrdiff_t)UINT32_MAX); guards(&g, 8);
}

int main(void) {
    blend_fixture(); color_channel_fixture(); depth_polygon_fixture(); multisample_stream_fixture();
    puts("t_nvn2_state: native storage, value copies, canonical codecs and unsupported guards passed");
    return 0;
}
