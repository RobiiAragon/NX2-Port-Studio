// SPDX-License-Identifier: GPL-3.0-or-later
// Native CPU state bytes are decoded and copied at bind; no guest pointer is retained.
#if DM_GAME_NVN2
#include "nvn2_state.h"
static void nvn2_state_require(enum nvn2_state_status status) {
    if (status!=NVN2_STATE_OK) die("[RED] NVN2 state: %s",nvn2_state_status_string(status));
}
static void n2_nvnBlendStateSetDefaults(NVNblendState *s) { nvn2_state_blend_defaults(s); }
static void n2_nvnColorStateSetDefaults(NVNcolorState *s) { nvn2_state_color_defaults(s); }
static void n2_nvnChannelMaskStateSetDefaults(NVNchannelMaskState *s) { nvn2_state_channel_defaults(s); }
static void n2_nvnDepthStencilStateSetDefaults(NVNdepthStencilState *s) { nvn2_state_ds_defaults(s); }
static void n2_nvnPolygonStateSetDefaults(NVNpolygonState *s) { nvn2_state_polygon_defaults(s); }
static void n2_nvnMultisampleStateSetDefaults(NVNmultisampleState *s) { nvn2_state_ms_defaults(s); }
static void n2_nvnBlendStateSetBlendTarget(NVNblendState *s,int value) { nvn2_state_require(nvn2_state_blend_set_target(s,value)); }
static int n2_nvnBlendStateGetBlendTarget(const NVNblendState *s) { return (int)nvn2_state_blend_get_target(s); }
static void n2_nvnBlendStateSetAdvancedMode(NVNblendState *s,NVNblendAdvancedMode value) { nvn2_state_require(nvn2_state_blend_set_advanced_mode(s,value)); }
static NVNblendAdvancedMode n2_nvnBlendStateGetAdvancedMode(const NVNblendState *s) { return (NVNblendAdvancedMode)nvn2_state_blend_get_advanced_mode(s); }
static void n2_nvnBlendStateSetAdvancedOverlap(NVNblendState *s,NVNblendAdvancedOverlap value) { nvn2_state_require(nvn2_state_blend_set_advanced_overlap(s,value)); }
static NVNblendAdvancedOverlap n2_nvnBlendStateGetAdvancedOverlap(const NVNblendState *s) { return (NVNblendAdvancedOverlap)nvn2_state_blend_get_advanced_overlap(s); }
static void n2_nvnBlendStateSetAdvancedPremultipliedSrc(NVNblendState *s,uint32_t value) { nvn2_state_require(nvn2_state_blend_set_advanced_premultiplied_src(s,value)); }
static NVNboolean n2_nvnBlendStateGetAdvancedPremultipliedSrc(const NVNblendState *s) { return (NVNboolean)nvn2_state_blend_get_advanced_premultiplied_src(s); }
static void n2_nvnBlendStateSetAdvancedNormalizedDst(NVNblendState *s,uint32_t value) { nvn2_state_require(nvn2_state_blend_set_advanced_normalized_dst(s,value)); }
static NVNboolean n2_nvnBlendStateGetAdvancedNormalizedDst(const NVNblendState *s) { return (NVNboolean)nvn2_state_blend_get_advanced_normalized_dst(s); }
static void n2_nvnColorStateSetLogicOp(NVNcolorState *s,NVNlogicOp value) { nvn2_state_require(nvn2_state_color_set_logic_op(s,value)); }
static NVNlogicOp n2_nvnColorStateGetLogicOp(const NVNcolorState *s) { return (NVNlogicOp)nvn2_state_color_get_logic_op(s); }
static void n2_nvnColorStateSetAlphaTest(NVNcolorState *s,NVNalphaFunc value) { nvn2_state_require(nvn2_state_color_set_alpha_test(s,value)); }
static NVNalphaFunc n2_nvnColorStateGetAlphaTest(const NVNcolorState *s) { return (NVNalphaFunc)nvn2_state_color_get_alpha_test(s); }
static void n2_nvnDepthStencilStateSetDepthTestEnable(NVNdepthStencilState *s,uint32_t value) { nvn2_state_require(nvn2_state_ds_set_depth_test_enable(s,value)); }
static NVNboolean n2_nvnDepthStencilStateGetDepthTestEnable(const NVNdepthStencilState *s) { return (NVNboolean)nvn2_state_ds_get_depth_test_enable(s); }
static void n2_nvnDepthStencilStateSetDepthWriteEnable(NVNdepthStencilState *s,uint32_t value) { nvn2_state_require(nvn2_state_ds_set_depth_write_enable(s,value)); }
static NVNboolean n2_nvnDepthStencilStateGetDepthWriteEnable(const NVNdepthStencilState *s) { return (NVNboolean)nvn2_state_ds_get_depth_write_enable(s); }
static void n2_nvnDepthStencilStateSetDepthFunc(NVNdepthStencilState *s,NVNdepthFunc value) { nvn2_state_require(nvn2_state_ds_set_depth_func(s,value)); }
static NVNdepthFunc n2_nvnDepthStencilStateGetDepthFunc(const NVNdepthStencilState *s) { return (NVNdepthFunc)nvn2_state_ds_get_depth_func(s); }
static void n2_nvnDepthStencilStateSetStencilTestEnable(NVNdepthStencilState *s,uint32_t value) { nvn2_state_require(nvn2_state_ds_set_stencil_test_enable(s,value)); }
static NVNboolean n2_nvnDepthStencilStateGetStencilTestEnable(const NVNdepthStencilState *s) { return (NVNboolean)nvn2_state_ds_get_stencil_test_enable(s); }
static void n2_nvnPolygonStateSetCullFace(NVNpolygonState *s,NVNface value) { nvn2_state_require(nvn2_state_polygon_set_cull_face(s,value)); }
static NVNface n2_nvnPolygonStateGetCullFace(const NVNpolygonState *s) { return (NVNface)nvn2_state_polygon_get_cull_face(s); }
static void n2_nvnPolygonStateSetFrontFace(NVNpolygonState *s,NVNfrontFace value) { nvn2_state_require(nvn2_state_polygon_set_front_face(s,value)); }
static NVNfrontFace n2_nvnPolygonStateGetFrontFace(const NVNpolygonState *s) { return (NVNfrontFace)nvn2_state_polygon_get_front_face(s); }
static void n2_nvnPolygonStateSetPolygonMode(NVNpolygonState *s,NVNpolygonMode value) { nvn2_state_require(nvn2_state_polygon_set_mode(s,value)); }
static NVNpolygonMode n2_nvnPolygonStateGetPolygonMode(const NVNpolygonState *s) { return (NVNpolygonMode)nvn2_state_polygon_get_mode(s); }
static void n2_nvnPolygonStateSetPolygonOffsetEnables(NVNpolygonState *s,NVNpolygonOffsetEnable value) { nvn2_state_require(nvn2_state_polygon_set_offset_enables(s,value)); }
static NVNpolygonOffsetEnable n2_nvnPolygonStateGetPolygonOffsetEnables(const NVNpolygonState *s) { return (NVNpolygonOffsetEnable)nvn2_state_polygon_get_offset_enables(s); }
static void n2_nvnMultisampleStateSetMultisampleEnable(NVNmultisampleState *s,uint32_t value) { nvn2_state_require(nvn2_state_ms_set_multisample_enable(s,value)); }
static NVNboolean n2_nvnMultisampleStateGetMultisampleEnable(const NVNmultisampleState *s) { return (NVNboolean)nvn2_state_ms_get_multisample_enable(s); }
static void n2_nvnMultisampleStateSetAlphaToCoverageEnable(NVNmultisampleState *s,uint32_t value) { nvn2_state_require(nvn2_state_ms_set_alpha_to_coverage_enable(s,value)); }
static NVNboolean n2_nvnMultisampleStateGetAlphaToCoverageEnable(const NVNmultisampleState *s) { return (NVNboolean)nvn2_state_ms_get_alpha_to_coverage_enable(s); }
static void n2_nvnMultisampleStateSetAlphaToCoverageDither(NVNmultisampleState *s,uint32_t value) { nvn2_state_require(nvn2_state_ms_set_alpha_to_coverage_dither(s,value)); }
static NVNboolean n2_nvnMultisampleStateGetAlphaToCoverageDither(const NVNmultisampleState *s) { return (NVNboolean)nvn2_state_ms_get_alpha_to_coverage_dither(s); }
static void n2_nvnMultisampleStateSetSamples(NVNmultisampleState *s,int value) { nvn2_state_require(nvn2_state_ms_set_samples(s,value)); }
static int n2_nvnMultisampleStateGetSamples(const NVNmultisampleState *s) { return (int)nvn2_state_ms_get_samples(s); }
static void n2_nvnMultisampleStateSetRasterSamples(NVNmultisampleState *s,int value) { nvn2_state_require(nvn2_state_ms_set_raster_samples(s,value)); }
static int n2_nvnMultisampleStateGetRasterSamples(const NVNmultisampleState *s) { return (int)nvn2_state_ms_get_raster_samples(s); }
static void n2_nvnMultisampleStateSetCoverageModulationMode(NVNmultisampleState *s,NVNcoverageModulationMode value) { nvn2_state_require(nvn2_state_ms_set_coverage_modulation_mode(s,value)); }
static NVNcoverageModulationMode n2_nvnMultisampleStateGetCoverageModulationMode(const NVNmultisampleState *s) { return (NVNcoverageModulationMode)nvn2_state_ms_get_coverage_modulation_mode(s); }
static void n2_nvnMultisampleStateSetCoverageTruncationEnable(NVNmultisampleState *s,uint32_t value) { nvn2_state_require(nvn2_state_ms_set_coverage_truncation_enable(s,value)); }
static NVNboolean n2_nvnMultisampleStateGetCoverageTruncationEnable(const NVNmultisampleState *s) { return (NVNboolean)nvn2_state_ms_get_coverage_truncation_enable(s); }
static void n2_nvnMultisampleStateSetCoverageToColorEnable(NVNmultisampleState *s,uint32_t value) { nvn2_state_require(nvn2_state_ms_set_coverage_to_color_enable(s,value)); }
static NVNboolean n2_nvnMultisampleStateGetCoverageToColorEnable(const NVNmultisampleState *s) { return (NVNboolean)nvn2_state_ms_get_coverage_to_color_enable(s); }
static void n2_nvnMultisampleStateSetCoverageToColorOutput(NVNmultisampleState *s,int value) { nvn2_state_require(nvn2_state_ms_set_coverage_to_color_output(s,value)); }
static int n2_nvnMultisampleStateGetCoverageToColorOutput(const NVNmultisampleState *s) { return (int)nvn2_state_ms_get_coverage_to_color_output(s); }
static void n2_nvnMultisampleStateSetSampleLocationsEnable(NVNmultisampleState *s,uint32_t value) { nvn2_state_require(nvn2_state_ms_set_sample_locations_enable(s,value)); }
static NVNboolean n2_nvnMultisampleStateGetSampleLocationsEnable(const NVNmultisampleState *s) { return (NVNboolean)nvn2_state_ms_get_sample_locations_enable(s); }
static void n2_nvnMultisampleStateSetSampleLocationsGridEnable(NVNmultisampleState *s,uint32_t value) { nvn2_state_require(nvn2_state_ms_set_sample_locations_grid_enable(s,value)); }
static NVNboolean n2_nvnMultisampleStateGetSampleLocationsGridEnable(const NVNmultisampleState *s) { return (NVNboolean)nvn2_state_ms_get_sample_locations_grid_enable(s); }
static void n2_nvnBlendStateSetBlendFunc(NVNblendState *s,NVNblendFunc sr,NVNblendFunc dr,NVNblendFunc sa,NVNblendFunc da) { nvn2_state_require(nvn2_state_blend_set_func(s,sr,dr,sa,da)); }
static void n2_nvnBlendStateGetBlendFunc(const NVNblendState *s,NVNblendFunc *sr,NVNblendFunc *dr,NVNblendFunc *sa,NVNblendFunc *da) { nvn2_state_blend_get_func(s,sr,dr,sa,da); }
static void n2_nvnBlendStateSetBlendEquation(NVNblendState *s,NVNblendEquation rgb,NVNblendEquation alpha) { nvn2_state_require(nvn2_state_blend_set_equation(s,rgb,alpha)); }
static void n2_nvnBlendStateGetBlendEquation(const NVNblendState *s,NVNblendEquation *rgb,NVNblendEquation *alpha) { nvn2_state_blend_get_equation(s,rgb,alpha); }
static void n2_nvnColorStateSetBlendEnable(NVNcolorState *s,int target,uint32_t enable) { nvn2_state_require(nvn2_state_color_set_blend_enable(s,target,enable)); }
static NVNboolean n2_nvnColorStateGetBlendEnable(const NVNcolorState *s,int target) { NVNboolean value; nvn2_state_require(nvn2_state_color_get_blend_enable(s,target,&value)); return value; }
static void n2_nvnChannelMaskStateSetChannelMask(NVNchannelMaskState *s,int target,uint32_t r,uint32_t g,uint32_t b,uint32_t a) { nvn2_state_require(nvn2_state_channel_set_mask(s,target,r,g,b,a)); }
static void n2_nvnChannelMaskStateGetChannelMask(const NVNchannelMaskState *s,int target,NVNboolean *r,NVNboolean *g,NVNboolean *b,NVNboolean *a) { nvn2_state_require(nvn2_state_channel_get_mask(s,target,r,g,b,a)); }
static void n2_nvnDepthStencilStateSetStencilFunc(NVNdepthStencilState *s,NVNface face,NVNstencilFunc func) { nvn2_state_require(nvn2_state_ds_set_stencil_func(s,face,func)); }
static NVNstencilFunc n2_nvnDepthStencilStateGetStencilFunc(const NVNdepthStencilState *s,NVNface face) { return (NVNstencilFunc)nvn2_state_ds_get_stencil_func(s,face); }
static void n2_nvnDepthStencilStateSetStencilOp(NVNdepthStencilState *s,NVNface face,NVNstencilOp fail,NVNstencilOp zfail,NVNstencilOp zpass) { nvn2_state_require(nvn2_state_ds_set_stencil_op(s,face,fail,zfail,zpass)); }
static void n2_nvnDepthStencilStateGetStencilOp(const NVNdepthStencilState *s,NVNface face,NVNstencilOp *fail,NVNstencilOp *zfail,NVNstencilOp *zpass) { nvn2_state_ds_get_stencil_op(s,face,fail,zfail,zpass); }
static void n2_nvnMultisampleStateGetSampleLocationsGrid(const NVNmultisampleState *s,int *width,int *height) { /* Native invalid-count query preserves both outputs. */ (void)nvn2_state_ms_get_sample_locations_grid(s,width,height); }
static void n2_nvnMultisampleStateSetSampleLocations(NVNmultisampleState *s,int start,int count,const float *xy) { if (count>0 && (count>16 || nvn2_readable_extent(xy)<(size_t)count*8)) die("[RED] NVN2 sample-location input exceeds readable range"); nvn2_state_require(nvn2_state_ms_set_sample_locations(s,start,count,xy)); }
static void n2_nvnCommandBufferBindBlendState(NVNcommandBuffer *command,const NVNblendState *state) { if (nvn2_readable_extent(state)<8) die("[RED] NVN2 unreadable Blend state"); blend_st decoded; nvn2_state_require(nvn2_state_decode_blend(&decoded,state)); bind_state(command,0,&decoded,sizeof decoded); }
static void n2_nvnCommandBufferBindColorState(NVNcommandBuffer *command,const NVNcolorState *state) { if (nvn2_readable_extent(state)<4) die("[RED] NVN2 unreadable Color state"); color_st decoded; nvn2_state_require(nvn2_state_decode_color(&decoded,state)); bind_state(command,1,&decoded,sizeof decoded); }
static void n2_nvnCommandBufferBindChannelMaskState(NVNcommandBuffer *command,const NVNchannelMaskState *state) { if (nvn2_readable_extent(state)<4) die("[RED] NVN2 unreadable ChannelMask state"); chmask_st decoded; nvn2_state_require(nvn2_state_decode_channel(&decoded,state)); bind_state(command,2,&decoded,sizeof decoded); }
static void n2_nvnCommandBufferBindMultisampleState(NVNcommandBuffer *command,const NVNmultisampleState *state) { if (nvn2_readable_extent(state)<24) die("[RED] NVN2 unreadable Multisample state"); ms_st decoded; nvn2_state_require(nvn2_state_decode_ms(&decoded,state)); bind_state(command,3,&decoded,sizeof decoded); }
static void n2_nvnCommandBufferBindPolygonState(NVNcommandBuffer *command,const NVNpolygonState *state) { if (nvn2_readable_extent(state)<4) die("[RED] NVN2 unreadable Polygon state"); poly_st decoded; nvn2_state_require(nvn2_state_decode_polygon(&decoded,state)); bind_state(command,4,&decoded,sizeof decoded); }
static void n2_nvnCommandBufferBindDepthStencilState(NVNcommandBuffer *command,const NVNdepthStencilState *state) { if (nvn2_readable_extent(state)<8) die("[RED] NVN2 unreadable DepthStencil state"); ds_st decoded; nvn2_state_require(nvn2_state_decode_ds(&decoded,state)); bind_state(command,5,&decoded,sizeof decoded); }
static void *nvn2_state_impl(const char *name) {
#define N2_STATE_PTR(n) if (!strcmp(name,#n)) return (void *)n2_##n;
    NVN2_STATE_API(N2_STATE_PTR)
#undef N2_STATE_PTR
    return NULL;
}
#else
static void *nvn2_state_impl(const char *name) { (void)name; return NULL; }
#endif
