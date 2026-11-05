// SPDX-License-Identifier: MS-PL
// TQ-064: XNA Refresh values through the documented C POD boundary.
#include <CNA/C/cna.h>
#include "CnaReconSmoke.h"
#include "CnaTestReport.h"
#include <string.h>
#include <stdio.h>

/* No expectations are sampled from CNA. Defaults/presets: Refresh SetDefaults and .cctor;
   header version/size/reserved fields: graphics_state.h. Binary fractions below are exact. */
static int preset_values(void)
{
    int failed = 0;
    CnaRuntimeSmoke smoke = {0};
    CNA_BlendState blend;
    CNA_DepthStencilState depth;
    CNA_RasterizerState rasterizer;
    CNA_SamplerState sampler;
    const struct { CNA_BlendStatePreset preset; CNA_Blend source, destination; } blends[] = {
        {CNA_BLEND_STATE_PRESET_DEFAULT, CNA_BLEND_ONE, CNA_BLEND_ZERO},
        {CNA_BLEND_STATE_PRESET_OPAQUE, CNA_BLEND_ONE, CNA_BLEND_ZERO},
        {CNA_BLEND_STATE_PRESET_ADDITIVE, CNA_BLEND_SOURCE_ALPHA, CNA_BLEND_ONE},
        {CNA_BLEND_STATE_PRESET_ALPHA_BLEND, CNA_BLEND_ONE, CNA_BLEND_INVERSE_SOURCE_ALPHA},
        {CNA_BLEND_STATE_PRESET_NON_PREMULTIPLIED, CNA_BLEND_SOURCE_ALPHA, CNA_BLEND_INVERSE_SOURCE_ALPHA}
    };
    const struct { CNA_DepthStencilStatePreset preset; CNA_Bool enabled, write; } depths[] = {
        {CNA_DEPTH_STENCIL_STATE_PRESET_DEFAULT, CNA_TRUE, CNA_TRUE},
        {CNA_DEPTH_STENCIL_STATE_PRESET_DEPTH_READ, CNA_TRUE, CNA_FALSE},
        {CNA_DEPTH_STENCIL_STATE_PRESET_NONE, CNA_FALSE, CNA_FALSE}
    };
    const struct { CNA_RasterizerStatePreset preset; CNA_CullMode cull; } rasters[] = {
        {CNA_RASTERIZER_STATE_PRESET_DEFAULT, CNA_CULL_COUNTER_CLOCKWISE_FACE},
        {CNA_RASTERIZER_STATE_PRESET_CULL_NONE, CNA_CULL_NONE},
        {CNA_RASTERIZER_STATE_PRESET_CULL_CLOCKWISE, CNA_CULL_CLOCKWISE_FACE},
        {CNA_RASTERIZER_STATE_PRESET_CULL_COUNTER_CLOCKWISE, CNA_CULL_COUNTER_CLOCKWISE_FACE}
    };
    const struct { CNA_SamplerStatePreset preset; CNA_TextureFilter filter; CNA_TextureAddressMode address; } samplers[] = {
        {CNA_SAMPLER_STATE_PRESET_DEFAULT, CNA_TEXTURE_FILTER_LINEAR, CNA_TEXTURE_ADDRESS_WRAP},
        {CNA_SAMPLER_STATE_PRESET_POINT_WRAP, CNA_TEXTURE_FILTER_POINT, CNA_TEXTURE_ADDRESS_WRAP},
        {CNA_SAMPLER_STATE_PRESET_POINT_CLAMP, CNA_TEXTURE_FILTER_POINT, CNA_TEXTURE_ADDRESS_CLAMP},
        {CNA_SAMPLER_STATE_PRESET_LINEAR_WRAP, CNA_TEXTURE_FILTER_LINEAR, CNA_TEXTURE_ADDRESS_WRAP},
        {CNA_SAMPLER_STATE_PRESET_LINEAR_CLAMP, CNA_TEXTURE_FILTER_LINEAR, CNA_TEXTURE_ADDRESS_CLAMP},
        {CNA_SAMPLER_STATE_PRESET_ANISOTROPIC_WRAP, CNA_TEXTURE_FILTER_ANISOTROPIC, CNA_TEXTURE_ADDRESS_WRAP},
        {CNA_SAMPLER_STATE_PRESET_ANISOTROPIC_CLAMP, CNA_TEXTURE_FILTER_ANISOTROPIC, CNA_TEXTURE_ADDRESS_CLAMP}
    };
    for (size_t i = 0; i < sizeof(blends) / sizeof(blends[0]); ++i) {
        REC_EQ(cna_blend_state_init(blends[i].preset, &blend), CNA_RESULT_SUCCESS);
        REC_EQ(blend.struct_size, sizeof(blend));
        REC_EQ(blend.struct_version, 1);
        REC_EQ(blend.alpha_blend_function, CNA_BLEND_FUNCTION_ADD);
        REC_EQ(blend.alpha_destination_blend, blends[i].destination);
        REC_EQ(blend.alpha_source_blend, blends[i].source);
        REC_EQ(blend.color_blend_function, CNA_BLEND_FUNCTION_ADD);
        REC_EQ(blend.color_destination_blend, blends[i].destination);
        REC_EQ(blend.color_source_blend, blends[i].source);
        REC_EQ(blend.color_write_channels, CNA_COLOR_WRITE_ALL);
        REC_EQ(blend.color_write_channels1, CNA_COLOR_WRITE_ALL);
        REC_EQ(blend.color_write_channels2, CNA_COLOR_WRITE_ALL);
        REC_EQ(blend.color_write_channels3, CNA_COLOR_WRITE_ALL);
        REC_EQ(blend.blend_factor.r, 255);
        REC_EQ(blend.blend_factor.g, 255);
        REC_EQ(blend.blend_factor.b, 255);
        REC_EQ(blend.blend_factor.a, 255);
        REC_EQ(blend.multi_sample_mask, -1);
    }
    REC_EQ(cna_blend_state_init(0, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_blend_state_init(UINT32_MAX, &blend), CNA_RESULT_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); ++i) {
        REC_EQ(cna_depth_stencil_state_init(depths[i].preset, &depth), CNA_RESULT_SUCCESS);
        REC_EQ(depth.struct_size, sizeof(depth));
        REC_EQ(depth.struct_version, 1);
        REC_EQ(depth.depth_buffer_enable, depths[i].enabled);
        REC_EQ(depth.depth_buffer_write_enable, depths[i].write);
        REC_EQ(depth.stencil_enable, CNA_FALSE);
        REC_EQ(depth.two_sided_stencil_mode, CNA_FALSE);
        REC_EQ(depth.depth_buffer_function, CNA_COMPARE_LESS_EQUAL);
        REC_EQ(depth.stencil_function, CNA_COMPARE_ALWAYS);
        REC_EQ(depth.stencil_mask, -1);
        REC_EQ(depth.stencil_write_mask, -1);
        REC_EQ(depth.reference_stencil, 0);
        REC_EQ(depth.stencil_fail, CNA_STENCIL_KEEP);
        REC_EQ(depth.stencil_depth_buffer_fail, CNA_STENCIL_KEEP);
        REC_EQ(depth.stencil_pass, CNA_STENCIL_KEEP);
        REC_EQ(depth.counter_clockwise_stencil_function, CNA_COMPARE_ALWAYS);
        REC_EQ(depth.counter_clockwise_stencil_fail, CNA_STENCIL_KEEP);
        REC_EQ(depth.counter_clockwise_stencil_depth_buffer_fail, CNA_STENCIL_KEEP);
        REC_EQ(depth.counter_clockwise_stencil_pass, CNA_STENCIL_KEEP);
        REC_EQ(depth.reserved, 0);
    }
    REC_EQ(cna_depth_stencil_state_init(0, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_depth_stencil_state_init(UINT32_MAX, &depth), CNA_RESULT_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(rasters) / sizeof(rasters[0]); ++i) {
        REC_EQ(cna_rasterizer_state_init(rasters[i].preset, &rasterizer), CNA_RESULT_SUCCESS);
        REC_EQ(rasterizer.struct_size, sizeof(rasterizer));
        REC_EQ(rasterizer.struct_version, 1);
        REC_EQ(rasterizer.cull_mode, rasters[i].cull);
        REC_EQ(rasterizer.fill_mode, CNA_FILL_SOLID);
        REC_FLOAT(rasterizer.depth_bias, 0.0f);
        REC_FLOAT(rasterizer.slope_scale_depth_bias, 0.0f);
        REC_EQ(rasterizer.multi_sample_anti_alias, CNA_TRUE);
        REC_EQ(rasterizer.scissor_test_enable, CNA_FALSE);
        REC_EQ(rasterizer.reserved[0], 0);
        REC_EQ(rasterizer.reserved[1], 0);
    }
    REC_EQ(cna_rasterizer_state_init(0, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_rasterizer_state_init(UINT32_MAX, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(samplers) / sizeof(samplers[0]); ++i) {
        REC_EQ(cna_sampler_state_init(samplers[i].preset, &sampler), CNA_RESULT_SUCCESS);
        REC_EQ(sampler.struct_size, sizeof(sampler));
        REC_EQ(sampler.struct_version, 1);
        REC_EQ(sampler.address_u, samplers[i].address);
        REC_EQ(sampler.address_v, samplers[i].address);
        REC_EQ(sampler.address_w, samplers[i].address);
        REC_EQ(sampler.filter, samplers[i].filter);
        REC_EQ(sampler.max_anisotropy, 4);
        REC_EQ(sampler.max_mip_level, 0);
        REC_FLOAT(sampler.mip_map_level_of_detail_bias, 0.0f);
        REC_EQ(sampler.reserved, 0);
    }
    REC_EQ(cna_sampler_state_init(0, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_sampler_state_init(UINT32_MAX, &sampler), CNA_RESULT_INVALID_ARGUMENT);
cleanup:
    failed |= cna_smoke_cleanup(&smoke);
    return !failed;
}

/* These are independent caller-supplied values, not snapshots of a native getter. */
static int device_values(CNA_Handle device)
{
    int failed = 0;
    CnaRuntimeSmoke smoke = {0};
    CNA_BlendState blend, got_blend;
    CNA_DepthStencilState depth, got_depth;
    CNA_RasterizerState rasterizer, got_rasterizer;
    CNA_SamplerState sampler, got_sampler;
    CNA_RendererInfo renderer = {sizeof(renderer), 1, 0, 0, 0, 0};
    CNA_RendererFeatureSupport mask_support = CNA_RENDERER_FEATURE_SUPPORT_UNKNOWN;
    REC_EQ(cna_blend_state_init(CNA_BLEND_STATE_PRESET_OPAQUE, &blend), CNA_RESULT_SUCCESS);
    REC_EQ(cna_depth_stencil_state_init(CNA_DEPTH_STENCIL_STATE_PRESET_NONE, &depth), CNA_RESULT_SUCCESS);
    REC_EQ(cna_rasterizer_state_init(CNA_RASTERIZER_STATE_PRESET_CULL_NONE, &rasterizer), CNA_RESULT_SUCCESS);
    REC_EQ(cna_sampler_state_init(CNA_SAMPLER_STATE_PRESET_POINT_CLAMP, &sampler), CNA_RESULT_SUCCESS);
    blend.alpha_source_blend = CNA_BLEND_SOURCE_ALPHA;
    blend.alpha_destination_blend = CNA_BLEND_INVERSE_SOURCE_ALPHA;
    blend.color_source_blend = CNA_BLEND_DESTINATION_COLOR;
    blend.color_destination_blend = CNA_BLEND_INVERSE_DESTINATION_COLOR;
    blend.alpha_blend_function = CNA_BLEND_FUNCTION_SUBTRACT;
    blend.color_blend_function = CNA_BLEND_FUNCTION_REVERSE_SUBTRACT;
    blend.color_write_channels = CNA_COLOR_WRITE_RED;
    blend.color_write_channels1 = CNA_COLOR_WRITE_GREEN;
    blend.color_write_channels2 = CNA_COLOR_WRITE_BLUE;
    blend.color_write_channels3 = CNA_COLOR_WRITE_ALPHA;
    blend.blend_factor.r = 17;
    blend.blend_factor.g = 34;
    blend.blend_factor.b = 51;
    blend.blend_factor.a = 68;
    blend.multi_sample_mask = -1;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_SUCCESS);
    blend.alpha_source_blend = CNA_BLEND_ZERO;
    got_blend.struct_size = sizeof(got_blend);
    got_blend.struct_version = 1;
    REC_EQ(cna_graphics_device_get_blend_state(device, &got_blend), CNA_RESULT_SUCCESS);
    REC_EQ(got_blend.alpha_source_blend, CNA_BLEND_SOURCE_ALPHA);
    REC_EQ(got_blend.alpha_destination_blend, CNA_BLEND_INVERSE_SOURCE_ALPHA);
    REC_EQ(got_blend.color_source_blend, CNA_BLEND_DESTINATION_COLOR);
    REC_EQ(got_blend.color_destination_blend, CNA_BLEND_INVERSE_DESTINATION_COLOR);
    REC_EQ(got_blend.alpha_blend_function, CNA_BLEND_FUNCTION_SUBTRACT);
    REC_EQ(got_blend.color_blend_function, CNA_BLEND_FUNCTION_REVERSE_SUBTRACT);
    REC_EQ(got_blend.color_write_channels, CNA_COLOR_WRITE_RED);
    REC_EQ(got_blend.color_write_channels1, CNA_COLOR_WRITE_GREEN);
    REC_EQ(got_blend.color_write_channels2, CNA_COLOR_WRITE_BLUE);
    REC_EQ(got_blend.color_write_channels3, CNA_COLOR_WRITE_ALPHA);
    REC_EQ(got_blend.blend_factor.r, 17);
    REC_EQ(got_blend.blend_factor.g, 34);
    REC_EQ(got_blend.blend_factor.b, 51);
    REC_EQ(got_blend.blend_factor.a, 68);
    REC_EQ(got_blend.multi_sample_mask, -1);
    blend.alpha_source_blend = CNA_BLEND_SOURCE_ALPHA;
    REC_EQ(cna_graphics_device_set_blend_state(device, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_graphics_device_get_blend_state(device, NULL), CNA_RESULT_INVALID_ARGUMENT);
    blend.alpha_blend_function = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.alpha_blend_function = CNA_BLEND_FUNCTION_SUBTRACT;
    blend.alpha_source_blend = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.alpha_source_blend = CNA_BLEND_SOURCE_ALPHA;
    blend.alpha_destination_blend = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.alpha_destination_blend = CNA_BLEND_INVERSE_SOURCE_ALPHA;
    blend.color_blend_function = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.color_blend_function = CNA_BLEND_FUNCTION_REVERSE_SUBTRACT;
    blend.color_source_blend = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.color_source_blend = CNA_BLEND_DESTINATION_COLOR;
    blend.color_destination_blend = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.color_destination_blend = CNA_BLEND_INVERSE_DESTINATION_COLOR;
    blend.color_write_channels = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.color_write_channels = CNA_COLOR_WRITE_RED;
    blend.color_write_channels1 = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.color_write_channels1 = CNA_COLOR_WRITE_GREEN;
    blend.color_write_channels2 = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.color_write_channels2 = CNA_COLOR_WRITE_BLUE;
    blend.color_write_channels3 = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.color_write_channels3 = CNA_COLOR_WRITE_ALPHA;
    blend.struct_size = 0;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.struct_size = sizeof(blend);
    blend.struct_version = 2;
    REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_INVALID_ARGUMENT);
    blend.struct_version = 1;
    REC_EQ(cna_graphics_device_get_blend_state(device, &got_blend), CNA_RESULT_SUCCESS);
    REC_EQ(got_blend.alpha_source_blend, CNA_BLEND_SOURCE_ALPHA);
    REC_EQ(got_blend.alpha_destination_blend, CNA_BLEND_INVERSE_SOURCE_ALPHA);
    REC_EQ(got_blend.color_source_blend, CNA_BLEND_DESTINATION_COLOR);
    REC_EQ(got_blend.color_destination_blend, CNA_BLEND_INVERSE_DESTINATION_COLOR);
    REC_EQ(got_blend.alpha_blend_function, CNA_BLEND_FUNCTION_SUBTRACT);
    REC_EQ(got_blend.color_blend_function, CNA_BLEND_FUNCTION_REVERSE_SUBTRACT);
    REC_EQ(got_blend.color_write_channels, CNA_COLOR_WRITE_RED);
    REC_EQ(got_blend.color_write_channels1, CNA_COLOR_WRITE_GREEN);
    REC_EQ(got_blend.color_write_channels2, CNA_COLOR_WRITE_BLUE);
    REC_EQ(got_blend.color_write_channels3, CNA_COLOR_WRITE_ALPHA);
    REC_EQ(got_blend.blend_factor.r, 17);
    REC_EQ(got_blend.blend_factor.g, 34);
    REC_EQ(got_blend.blend_factor.b, 51);
    REC_EQ(got_blend.blend_factor.a, 68);
    REC_EQ(got_blend.multi_sample_mask, -1);
    depth.depth_buffer_enable = CNA_FALSE;
    depth.depth_buffer_write_enable = CNA_TRUE;
    depth.depth_buffer_function = CNA_COMPARE_LESS;
    depth.stencil_enable = CNA_TRUE;
    depth.stencil_function = CNA_COMPARE_EQUAL;
    depth.stencil_mask = 0x1357;
    depth.stencil_write_mask = 0x2468;
    depth.reference_stencil = 7;
    depth.stencil_fail = CNA_STENCIL_REPLACE;
    depth.stencil_depth_buffer_fail = CNA_STENCIL_INVERT;
    depth.stencil_pass = CNA_STENCIL_INCREMENT;
    depth.two_sided_stencil_mode = CNA_TRUE;
    depth.counter_clockwise_stencil_function = CNA_COMPARE_GREATER;
    depth.counter_clockwise_stencil_fail = CNA_STENCIL_ZERO;
    depth.counter_clockwise_stencil_depth_buffer_fail = CNA_STENCIL_DECREMENT;
    depth.counter_clockwise_stencil_pass = CNA_STENCIL_INCREMENT_SATURATION;
    depth.reserved = 0;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_SUCCESS);
    depth.depth_buffer_enable = CNA_TRUE;
    got_depth.struct_size = sizeof(got_depth);
    got_depth.struct_version = 1;
    REC_EQ(cna_graphics_device_get_depth_stencil_state(device, &got_depth), CNA_RESULT_SUCCESS);
    REC_EQ(got_depth.depth_buffer_enable, CNA_FALSE);
    REC_EQ(got_depth.depth_buffer_write_enable, CNA_TRUE);
    REC_EQ(got_depth.depth_buffer_function, CNA_COMPARE_LESS);
    REC_EQ(got_depth.stencil_enable, CNA_TRUE);
    REC_EQ(got_depth.stencil_function, CNA_COMPARE_EQUAL);
    REC_EQ(got_depth.stencil_mask, 0x1357);
    REC_EQ(got_depth.stencil_write_mask, 0x2468);
    REC_EQ(got_depth.reference_stencil, 7);
    REC_EQ(got_depth.stencil_fail, CNA_STENCIL_REPLACE);
    REC_EQ(got_depth.stencil_depth_buffer_fail, CNA_STENCIL_INVERT);
    REC_EQ(got_depth.stencil_pass, CNA_STENCIL_INCREMENT);
    REC_EQ(got_depth.two_sided_stencil_mode, CNA_TRUE);
    REC_EQ(got_depth.counter_clockwise_stencil_function, CNA_COMPARE_GREATER);
    REC_EQ(got_depth.counter_clockwise_stencil_fail, CNA_STENCIL_ZERO);
    REC_EQ(got_depth.counter_clockwise_stencil_depth_buffer_fail, CNA_STENCIL_DECREMENT);
    REC_EQ(got_depth.counter_clockwise_stencil_pass, CNA_STENCIL_INCREMENT_SATURATION);
    REC_EQ(got_depth.reserved, 0);
    depth.depth_buffer_enable = CNA_FALSE;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_graphics_device_get_depth_stencil_state(device, NULL), CNA_RESULT_INVALID_ARGUMENT);
    depth.depth_buffer_function = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.depth_buffer_function = CNA_COMPARE_LESS;
    depth.stencil_function = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.stencil_function = CNA_COMPARE_EQUAL;
    depth.counter_clockwise_stencil_function = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.counter_clockwise_stencil_function = CNA_COMPARE_GREATER;
    depth.stencil_fail = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.stencil_fail = CNA_STENCIL_REPLACE;
    depth.stencil_depth_buffer_fail = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.stencil_depth_buffer_fail = CNA_STENCIL_INVERT;
    depth.stencil_pass = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.stencil_pass = CNA_STENCIL_INCREMENT;
    depth.counter_clockwise_stencil_fail = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.counter_clockwise_stencil_fail = CNA_STENCIL_ZERO;
    depth.counter_clockwise_stencil_depth_buffer_fail = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.counter_clockwise_stencil_depth_buffer_fail = CNA_STENCIL_DECREMENT;
    depth.counter_clockwise_stencil_pass = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.counter_clockwise_stencil_pass = CNA_STENCIL_INCREMENT_SATURATION;
    depth.depth_buffer_enable = 2;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.depth_buffer_enable = CNA_FALSE;
    depth.depth_buffer_write_enable = 2;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.depth_buffer_write_enable = CNA_TRUE;
    depth.stencil_enable = 2;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.stencil_enable = CNA_TRUE;
    depth.two_sided_stencil_mode = 2;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.two_sided_stencil_mode = CNA_TRUE;
    depth.struct_size = 0;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.struct_size = sizeof(depth);
    depth.struct_version = 2;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.struct_version = 1;
    depth.reserved = 1;
    REC_EQ(cna_graphics_device_set_depth_stencil_state(device, &depth), CNA_RESULT_INVALID_ARGUMENT);
    depth.reserved = 0;
    REC_EQ(cna_graphics_device_get_depth_stencil_state(device, &got_depth), CNA_RESULT_SUCCESS);
    REC_EQ(got_depth.depth_buffer_enable, CNA_FALSE);
    REC_EQ(got_depth.depth_buffer_write_enable, CNA_TRUE);
    REC_EQ(got_depth.depth_buffer_function, CNA_COMPARE_LESS);
    REC_EQ(got_depth.stencil_enable, CNA_TRUE);
    REC_EQ(got_depth.stencil_function, CNA_COMPARE_EQUAL);
    REC_EQ(got_depth.stencil_mask, 0x1357);
    REC_EQ(got_depth.stencil_write_mask, 0x2468);
    REC_EQ(got_depth.reference_stencil, 7);
    REC_EQ(got_depth.stencil_fail, CNA_STENCIL_REPLACE);
    REC_EQ(got_depth.stencil_depth_buffer_fail, CNA_STENCIL_INVERT);
    REC_EQ(got_depth.stencil_pass, CNA_STENCIL_INCREMENT);
    REC_EQ(got_depth.two_sided_stencil_mode, CNA_TRUE);
    REC_EQ(got_depth.counter_clockwise_stencil_function, CNA_COMPARE_GREATER);
    REC_EQ(got_depth.counter_clockwise_stencil_fail, CNA_STENCIL_ZERO);
    REC_EQ(got_depth.counter_clockwise_stencil_depth_buffer_fail, CNA_STENCIL_DECREMENT);
    REC_EQ(got_depth.counter_clockwise_stencil_pass, CNA_STENCIL_INCREMENT_SATURATION);
    REC_EQ(got_depth.reserved, 0);
    rasterizer.cull_mode = CNA_CULL_NONE;
    rasterizer.fill_mode = CNA_FILL_SOLID;
    rasterizer.depth_bias = -0.25f;
    rasterizer.slope_scale_depth_bias = 1.5f;
    rasterizer.multi_sample_anti_alias = CNA_FALSE;
    rasterizer.scissor_test_enable = CNA_TRUE;
    rasterizer.reserved[0] = 0;
    rasterizer.reserved[1] = 0;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_SUCCESS);
    rasterizer.cull_mode = CNA_CULL_CLOCKWISE_FACE;
    got_rasterizer.struct_size = sizeof(got_rasterizer);
    got_rasterizer.struct_version = 1;
    REC_EQ(cna_graphics_device_get_rasterizer_state(device, &got_rasterizer), CNA_RESULT_SUCCESS);
    REC_EQ(got_rasterizer.cull_mode, CNA_CULL_NONE);
    REC_EQ(got_rasterizer.fill_mode, CNA_FILL_SOLID);
    REC_FLOAT(got_rasterizer.depth_bias, -0.25f);
    REC_FLOAT(got_rasterizer.slope_scale_depth_bias, 1.5f);
    REC_EQ(got_rasterizer.multi_sample_anti_alias, CNA_FALSE);
    REC_EQ(got_rasterizer.scissor_test_enable, CNA_TRUE);
    REC_EQ(got_rasterizer.reserved[0], 0);
    REC_EQ(got_rasterizer.reserved[1], 0);
    rasterizer.cull_mode = CNA_CULL_NONE;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_graphics_device_get_rasterizer_state(device, NULL), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.cull_mode = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.cull_mode = CNA_CULL_NONE;
    rasterizer.fill_mode = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.fill_mode = CNA_FILL_SOLID;
    rasterizer.multi_sample_anti_alias = 2;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.multi_sample_anti_alias = CNA_FALSE;
    rasterizer.scissor_test_enable = 2;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.scissor_test_enable = CNA_TRUE;
    rasterizer.struct_size = 0;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.struct_size = sizeof(rasterizer);
    rasterizer.struct_version = 2;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.struct_version = 1;
    rasterizer.reserved[0] = 1;
    REC_EQ(cna_graphics_device_set_rasterizer_state(device, &rasterizer), CNA_RESULT_INVALID_ARGUMENT);
    rasterizer.reserved[0] = 0;
    REC_EQ(cna_graphics_device_get_rasterizer_state(device, &got_rasterizer), CNA_RESULT_SUCCESS);
    REC_EQ(got_rasterizer.cull_mode, CNA_CULL_NONE);
    REC_EQ(got_rasterizer.fill_mode, CNA_FILL_SOLID);
    REC_FLOAT(got_rasterizer.depth_bias, -0.25f);
    REC_FLOAT(got_rasterizer.slope_scale_depth_bias, 1.5f);
    REC_EQ(got_rasterizer.multi_sample_anti_alias, CNA_FALSE);
    REC_EQ(got_rasterizer.scissor_test_enable, CNA_TRUE);
    REC_EQ(got_rasterizer.reserved[0], 0);
    REC_EQ(got_rasterizer.reserved[1], 0);
    sampler.address_u = CNA_TEXTURE_ADDRESS_WRAP;
    sampler.address_v = CNA_TEXTURE_ADDRESS_MIRROR;
    sampler.address_w = CNA_TEXTURE_ADDRESS_CLAMP;
    sampler.filter = CNA_TEXTURE_FILTER_POINT;
    sampler.max_anisotropy = 9;
    sampler.max_mip_level = 2;
    sampler.mip_map_level_of_detail_bias = 1.5f;
    sampler.reserved = 0;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_SUCCESS);
    sampler.address_u = CNA_TEXTURE_ADDRESS_CLAMP;
    got_sampler.struct_size = sizeof(got_sampler);
    got_sampler.struct_version = 1;
    REC_EQ(cna_graphics_device_get_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &got_sampler), CNA_RESULT_SUCCESS);
    REC_EQ(got_sampler.address_u, CNA_TEXTURE_ADDRESS_WRAP);
    REC_EQ(got_sampler.address_v, CNA_TEXTURE_ADDRESS_MIRROR);
    REC_EQ(got_sampler.address_w, CNA_TEXTURE_ADDRESS_CLAMP);
    REC_EQ(got_sampler.filter, CNA_TEXTURE_FILTER_POINT);
    REC_EQ(got_sampler.max_anisotropy, 9);
    REC_EQ(got_sampler.max_mip_level, 2);
    REC_FLOAT(got_sampler.mip_map_level_of_detail_bias, 1.5f);
    REC_EQ(got_sampler.reserved, 0);
    sampler.address_u = CNA_TEXTURE_ADDRESS_WRAP;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, NULL), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_graphics_device_get_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, NULL), CNA_RESULT_INVALID_ARGUMENT);
    sampler.address_u = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_INVALID_ARGUMENT);
    sampler.address_u = CNA_TEXTURE_ADDRESS_WRAP;
    sampler.address_v = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_INVALID_ARGUMENT);
    sampler.address_v = CNA_TEXTURE_ADDRESS_MIRROR;
    sampler.address_w = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_INVALID_ARGUMENT);
    sampler.address_w = CNA_TEXTURE_ADDRESS_CLAMP;
    sampler.filter = UINT32_MAX;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_INVALID_ARGUMENT);
    sampler.filter = CNA_TEXTURE_FILTER_POINT;
    sampler.struct_size = 0;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_INVALID_ARGUMENT);
    sampler.struct_size = sizeof(sampler);
    sampler.struct_version = 2;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_INVALID_ARGUMENT);
    sampler.struct_version = 1;
    sampler.reserved = 1;
    REC_EQ(cna_graphics_device_set_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &sampler), CNA_RESULT_INVALID_ARGUMENT);
    sampler.reserved = 0;
    REC_EQ(cna_graphics_device_get_sampler_state(device, CNA_SHADER_STAGE_PIXEL, 3, &got_sampler), CNA_RESULT_SUCCESS);
    REC_EQ(got_sampler.address_u, CNA_TEXTURE_ADDRESS_WRAP);
    REC_EQ(got_sampler.address_v, CNA_TEXTURE_ADDRESS_MIRROR);
    REC_EQ(got_sampler.address_w, CNA_TEXTURE_ADDRESS_CLAMP);
    REC_EQ(got_sampler.filter, CNA_TEXTURE_FILTER_POINT);
    REC_EQ(got_sampler.max_anisotropy, 9);
    REC_EQ(got_sampler.max_mip_level, 2);
    REC_FLOAT(got_sampler.mip_map_level_of_detail_bias, 1.5f);
    REC_EQ(got_sampler.reserved, 0);
    /* Default mask alone cannot detect a converter that drops MultiSampleMask. */
    REC_EQ(cna_graphics_device_get_renderer_info(device, &renderer), CNA_RESULT_SUCCESS);
    REC_EQ(cna_graphics_device_get_renderer_feature_support_ext(device,
        CNA_RENDERER_FEATURE_MULTI_SAMPLE_MASK, &mask_support), CNA_RESULT_SUCCESS);
    if (renderer.renderer_type == CNA_GRAPHICS_RENDERER_HEADLESS ||
        mask_support == CNA_RENDERER_FEATURE_SUPPORT_SUPPORTED) {
        blend.multi_sample_mask = 0x1357;
        REC_EQ(cna_graphics_device_set_blend_state(device, &blend), CNA_RESULT_SUCCESS);
        REC_EQ(cna_graphics_device_get_blend_state(device, &got_blend), CNA_RESULT_SUCCESS);
        REC_EQ(got_blend.multi_sample_mask, 0x1357);
    } else {
        puts("SKIP subcase: nondefault BlendState.MultiSampleMask requires renderer support; other state transport assertions ran.");
    }
cleanup:
    failed |= cna_smoke_cleanup(&smoke);
    return !failed;
}

/* Profile counts are Refresh ProfileCapabilities::.cctor IL_00aa-00b3/02e5-02ee.
   Null descriptors are argument errors; textures allow null but sampler states do not. */
static int sampler_bounds(CNA_Handle device, uint32_t profile)
{
    int failed = 0;
    CnaRuntimeSmoke smoke = {0};
    CNA_SamplerState state, actual;
    CNA_ErrorInfo error = {sizeof(error), 1, 0, 0, 0};
    const uint32_t vertex_count = profile == CNA_GRAPHICS_PROFILE_HI_DEF ? 4U : 0U;
    REC_EQ(cna_sampler_state_init(CNA_SAMPLER_STATE_PRESET_POINT_CLAMP, &state), CNA_RESULT_SUCCESS);
    actual.struct_size = sizeof(actual); actual.struct_version = 1;
    const uint32_t stages[] = {CNA_SHADER_STAGE_PIXEL, CNA_SHADER_STAGE_VERTEX};
    for (size_t stage_index = 0; stage_index < 2U; ++stage_index) {
        const uint32_t stage = stages[stage_index];
        const uint32_t count = stage == CNA_SHADER_STAGE_PIXEL ? 16U : vertex_count;
        const uint32_t invalid[] = {count, 16U, UINT32_MAX};
        /* docs/c-api/ERRORS.md: System.ArgumentOutOfRangeException is an argument exception
           (INVALID_ARGUMENT / ARGUMENT). A slot below CNA_MAX_SAMPLERS but past the profile's
           count is refused by the native collection, so it carries that exception type; a slot at
           or past CNA_MAX_SAMPLERS is refused by C-layer validation and carries none. */
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            static const char expected_type[] = "System.ArgumentOutOfRangeException";
            char type_name[64];
            uint64_t type_size = UINT64_MAX;
            REC_EQ(cna_graphics_device_get_sampler_state(device, stage, invalid[i], &actual), CNA_RESULT_INVALID_ARGUMENT);
            REC_EQ(cna_error_get_last_info(&error), CNA_RESULT_SUCCESS);
            REC_EQ(error.category, CNA_ERROR_CATEGORY_ARGUMENT);
            REC_EQ(cna_error_get_last_exception_type_size_ext(&type_size), CNA_RESULT_SUCCESS);
            if (invalid[i] < 16U) {
                REC_EQ(type_size, (uint64_t)(sizeof(expected_type) - 1U));
                REC_EQ(cna_error_copy_last_exception_type_ext(type_name, sizeof(type_name), &type_size), CNA_RESULT_SUCCESS);
                REC_EQ(memcmp(type_name, expected_type, sizeof(expected_type) - 1U), 0);
            } else {
                REC_EQ(type_size, UINT64_C(0));
            }
            REC_EQ(cna_graphics_device_set_sampler_state(device, stage, invalid[i], &state), CNA_RESULT_INVALID_ARGUMENT);
            REC_EQ(cna_error_get_last_info(&error), CNA_RESULT_SUCCESS);
            REC_EQ(error.category, CNA_ERROR_CATEGORY_ARGUMENT);
            REC_EQ(cna_error_get_last_exception_type_size_ext(&type_size), CNA_RESULT_SUCCESS);
            REC_EQ(type_size, invalid[i] < 16U ? (uint64_t)(sizeof(expected_type) - 1U) : UINT64_C(0));
        }
        if (count > 0U) {
            REC_EQ(cna_graphics_device_set_sampler_state(device, stage, count - 1U, &state), CNA_RESULT_SUCCESS);
            REC_EQ(cna_graphics_device_get_sampler_state(device, stage, count - 1U, &actual), CNA_RESULT_SUCCESS);
            REC_EQ(actual.filter, CNA_TEXTURE_FILTER_POINT);
            REC_EQ(actual.address_w, CNA_TEXTURE_ADDRESS_CLAMP);
            REC_EQ(cna_graphics_device_set_sampler_state(device, stage, count - 1U, NULL), CNA_RESULT_INVALID_ARGUMENT);
            REC_EQ(cna_graphics_device_get_sampler_state(device, stage, count - 1U, &actual), CNA_RESULT_SUCCESS);
            REC_EQ(actual.filter, CNA_TEXTURE_FILTER_POINT);
        }
    }
    REC_EQ(cna_graphics_device_get_sampler_state(device, UINT32_MAX, 0, &actual), CNA_RESULT_INVALID_ARGUMENT);
    REC_EQ(cna_graphics_device_set_sampler_state(device, UINT32_MAX, 0, &state), CNA_RESULT_INVALID_ARGUMENT);
cleanup:
    failed |= cna_smoke_cleanup(&smoke);
    return !failed;
}

static int runtime_values(void)
{
    int failed = 0;
    CnaRuntimeSmoke smoke = {0};
    CNA_PresentationParameters parameters;
    CNA_Handle device = CNA_INVALID_HANDLE;
    const uint32_t profiles[] = {CNA_GRAPHICS_PROFILE_REACH, CNA_GRAPHICS_PROFILE_HI_DEF};
    REC_EQ(cna_presentation_parameters_init(&parameters), CNA_RESULT_SUCCESS);
    parameters.is_full_screen = CNA_FALSE;
    parameters.back_buffer_width = 64; parameters.back_buffer_height = 64;
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        REC_OWN(cna_graphics_device_create(0, profiles[i], &parameters, &device), device, cna_graphics_device_destroy);
        REC_EQ(sampler_bounds(device, profiles[i]), 1);
        if (profiles[i] == CNA_GRAPHICS_PROFILE_HI_DEF) REC_EQ(device_values(device), 1);
        REC_DROP(cna_graphics_device_destroy(device), device);
    }
cleanup:
    failed |= cna_smoke_cleanup(&smoke);
    return !failed;
}

int main(int argc, char** argv)
{
    if (!preset_values()) return CNA_TEST_FAIL(1);
    if (argc > 1 && strcmp(argv[1], "--runtime") == 0 && !runtime_values()) return CNA_TEST_FAIL(2);
    return 0;
}
