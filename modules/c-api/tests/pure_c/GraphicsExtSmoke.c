// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct ExtState {
    CNA_Bool available;
    int validated;
} ExtState;

static int near_float(const float left, const float right)
{
    return fabsf(left - right) <= 0.0001F;
}

static int is_supported(const CNA_Result result)
{
    return result == CNA_RESULT_SUCCESS || result == CNA_RESULT_NOT_SUPPORTED;
}

static int validate_identities(void)
{
    return CNA_ASCII_QUANTIZE_MODE_BLACK_WHITE == UINT32_C(0) &&
        CNA_ASCII_QUANTIZE_MODE_COLOR == UINT32_C(1) &&
        CNA_CRT_MASK_TYPE_NONE == UINT32_C(0) &&
        CNA_CRT_MASK_TYPE_APERTURE_GRILLE == UINT32_C(1) &&
        CNA_CRT_MASK_TYPE_SHADOW_MASK == UINT32_C(2) &&
        CNA_DITHER_MODE_NONE == UINT32_C(0) &&
        CNA_DITHER_MODE_BAYER_4X4 == UINT32_C(1) &&
        CNA_DITHER_MODE_BAYER_8X8 == UINT32_C(2) &&
        CNA_RENDER_QUALITY_LOW == UINT32_C(0) &&
        CNA_RENDER_QUALITY_MEDIUM == UINT32_C(1) &&
        CNA_RENDER_QUALITY_HIGH == UINT32_C(2) &&
        CNA_RENDER_QUALITY_ULTRA == UINT32_C(3) &&
        CNA_SHADOW_QUALITY_DISABLED == UINT32_C(0) &&
        CNA_SHADOW_QUALITY_LOW == UINT32_C(1) &&
        CNA_SHADOW_QUALITY_MEDIUM == UINT32_C(2) &&
        CNA_SHADOW_QUALITY_HIGH == UINT32_C(3) &&
        CNA_SHADOW_QUALITY_ULTRA == UINT32_C(4) &&
        CNA_TONEMAPPING_MODE_NONE == UINT32_C(0) &&
        CNA_TONEMAPPING_MODE_REINHARD == UINT32_C(1) &&
        CNA_TONEMAPPING_MODE_FILMIC == UINT32_C(2) &&
        CNA_TONEMAPPING_MODE_ACES == UINT32_C(3) &&
        CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT == UINT32_C(0) &&
        CNA_DEPTH_EFFECT_MODE_COLOR_8_BIT == UINT32_C(1) &&
        CNA_DEPTH_EFFECT_MODE_GRAYSCALE_4_BIT == UINT32_C(2) &&
        CNA_DEPTH_EFFECT_MODE_GRAYSCALE_2_BIT == UINT32_C(3) &&
        CNA_DEPTH_EFFECT_MODE_GRAYSCALE_1_BIT == UINT32_C(4) &&
        CNA_DEPTH_EFFECT_MODE_PALETTE_256 == UINT32_C(5) &&
        CNA_DEPTH_EFFECT_MODE_PALETTE_16 == UINT32_C(6);
}

static int validate_values(void)
{
    CNA_PbrMaterial material;
    memset(&material, 0x5A, sizeof(material));
    if (cna_pbr_material_init(&material) != CNA_RESULT_SUCCESS ||
        material.albedo_texture != CNA_INVALID_HANDLE ||
        material.normal_texture != CNA_INVALID_HANDLE ||
        material.metallic_roughness_texture != CNA_INVALID_HANDLE ||
        material.ambient_occlusion_texture != CNA_INVALID_HANDLE ||
        material.emissive_texture != CNA_INVALID_HANDLE ||
        material.albedo_color.r != UINT8_C(255) || material.albedo_color.a != UINT8_C(255) ||
        material.emissive_color.r != UINT8_C(0) || material.emissive_color.a != UINT8_C(255) ||
        material.metallic_factor != 0.0F || material.roughness_factor != 0.5F ||
        material.normal_scale != 1.0F || material.occlusion_strength != 1.0F ||
        material.alpha_cutoff != 0.5F || material.alpha_blend_enabled != CNA_FALSE ||
        material.reserved[0] != 0U || material.reserved[1] != 0U ||
        material.reserved[2] != 0U) {
        return 0;
    }

    /* The canonical accessors assign without clamping, so the POD fields are written directly. */
    material.metallic_factor = 2.5F;
    material.alpha_blend_enabled = CNA_TRUE;
    if (!near_float(material.metallic_factor, 2.5F) ||
        material.alpha_blend_enabled != CNA_TRUE) {
        return 0;
    }

    CNA_PbrMaterialEXT full;
    memset(&full, 0x5A, sizeof(full));
    if (cna_pbr_material_ext_init(&full) != CNA_RESULT_SUCCESS ||
        full.struct_size != (uint32_t)sizeof(CNA_PbrMaterialEXT) ||
        full.struct_version != CNA_PBR_MATERIAL_EXT_VERSION ||
        full.albedo_texture != CNA_INVALID_HANDLE ||
        full.specular_texture != CNA_INVALID_HANDLE ||
        full.specular_color_texture != CNA_INVALID_HANDLE ||
        full.albedo_color.r != UINT8_C(255) || full.albedo_color.a != UINT8_C(255) ||
        full.emissive_factor.x != 0.0F || full.emissive_factor.z != 0.0F ||
        full.specular_color_factor.x != 1.0F ||
        full.metallic_factor != 1.0F || full.roughness_factor != 1.0F ||
        full.normal_scale != 1.0F || full.occlusion_strength != 1.0F ||
        full.ior != 1.5F || full.specular_factor != 1.0F ||
        full.alpha_cutoff != 0.5F || full.alpha_mode != CNA_ALPHA_MODE_OPAQUE_EXT ||
        full.double_sided != CNA_FALSE ||
        full.base_color_texture_srgb != CNA_TRUE ||
        full.emissive_texture_srgb != CNA_TRUE ||
        full.specular_color_texture_srgb != CNA_TRUE ||
        full.output_encoded_to_srgb != CNA_TRUE ||
        full.reserved[0] != 0U || full.reserved[1] != 0U || full.reserved[2] != 0U ||
        full.texture_coordinate_sets[0] != 0 || full.texture_coordinate_sets[6] != 0 ||
        full.texture_transforms[0].scale.x != 1.0F ||
        full.texture_transforms[6].rotation != 0.0F ||
        cna_pbr_material_ext_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    full.alpha_mode = CNA_ALPHA_MODE_BLEND_EXT;
    full.double_sided = CNA_TRUE;
    full.texture_coordinate_sets[3] = 1;
    if (full.alpha_mode != CNA_ALPHA_MODE_BLEND_EXT || full.double_sided != CNA_TRUE ||
        full.texture_coordinate_sets[3] != 1) {
        return 0;
    }

    CNA_RenderPipelineSettings settings;
    memset(&settings, 0x5A, sizeof(settings));
    if (cna_render_pipeline_settings_init(&settings) != CNA_RESULT_SUCCESS ||
        settings.hdr_enabled != CNA_FALSE || !near_float(settings.exposure, 1.0F) ||
        !near_float(settings.gamma, 2.2F) ||
        settings.tonemapping_mode != CNA_TONEMAPPING_MODE_NONE ||
        settings.bloom_enabled != CNA_FALSE ||
        !near_float(settings.bloom_intensity, 1.0F) ||
        settings.ssao_enabled != CNA_FALSE ||
        settings.render_quality != CNA_RENDER_QUALITY_MEDIUM ||
        settings.shadow_quality != CNA_SHADOW_QUALITY_DISABLED ||
        settings.shadows_enabled != CNA_FALSE) {
        return 0;
    }

    CNA_Bool available = (CNA_Bool)7;
    if (cna_graphics_ext_is_available(&available) != CNA_RESULT_SUCCESS ||
        (available != CNA_TRUE && available != CNA_FALSE) ||
        cna_graphics_ext_is_available(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_pbr_material_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_render_pipeline_settings_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_unavailable(CNA_Handle graphics_device)
{
    CNA_EffectHandle effect = UINT64_C(7);
    CNA_AsciiPostProcessEffectHandle ascii = UINT64_C(7);
    float value = 0.0F;
    CNA_CRTMaskType mask = UINT32_MAX;
    CNA_DepthEffectMode mode = UINT32_MAX;
    CNA_DitherMode dither = UINT32_MAX;
    int32_t width = 0;
    int32_t height = 0;

    return cna_crt_effect_create(graphics_device, &effect) == CNA_RESULT_NOT_SUPPORTED &&
        effect == CNA_INVALID_HANDLE &&
        cna_crt_effect_get_scanline_intensity(effect, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_set_scanline_intensity(effect, 0.5F) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_get_curvature(effect, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_set_curvature(effect, 0.5F) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_get_vignette_intensity(effect, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_set_vignette_intensity(effect, 0.5F) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_get_mask_intensity(effect, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_set_mask_intensity(effect, 0.5F) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_get_mask_type(effect, &mask) == CNA_RESULT_NOT_SUPPORTED &&
        cna_crt_effect_set_mask_type(effect, CNA_CRT_MASK_TYPE_NONE) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_depth_effect_create(graphics_device, &effect) == CNA_RESULT_NOT_SUPPORTED &&
        cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_NOT_SUPPORTED &&
        cna_depth_effect_set_mode(effect, CNA_DEPTH_EFFECT_MODE_COLOR_8_BIT) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_depth_effect_get_dither_mode(effect, &dither) == CNA_RESULT_NOT_SUPPORTED &&
        cna_depth_effect_set_dither_mode(effect, CNA_DITHER_MODE_BAYER_4X4) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_ascii_post_process_effect_create(graphics_device, &ascii) ==
            CNA_RESULT_NOT_SUPPORTED &&
        ascii == CNA_INVALID_HANDLE &&
        cna_ascii_post_process_effect_get_cell_size(ascii, &width, &height) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_ascii_post_process_effect_set_cell_size(ascii, 4, 4) == CNA_RESULT_NOT_SUPPORTED &&
        cna_ascii_post_process_effect_get_quantize_mode(ascii, &mask) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_ascii_post_process_effect_set_quantize_mode(
            ascii, CNA_ASCII_QUANTIZE_MODE_COLOR) == CNA_RESULT_NOT_SUPPORTED &&
        cna_ascii_post_process_effect_draw(ascii, CNA_INVALID_HANDLE, 0) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_ascii_post_process_effect_get_last_grid_dimensions(ascii, &width, &height) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_ascii_post_process_effect_destroy(ascii) == CNA_RESULT_NOT_SUPPORTED;
}

static int validate_crt_effect(CNA_Handle graphics_device)
{
    CNA_EffectHandle effect = CNA_INVALID_HANDLE;
    const CNA_Result created = cna_crt_effect_create(graphics_device, &effect);
    if (created == CNA_RESULT_NOT_SUPPORTED) {
        return effect == CNA_INVALID_HANDLE;
    }
    if (created != CNA_RESULT_SUCCESS) {
        return 0;
    }

    float value = -1.0F;
    CNA_CRTMaskType mask = UINT32_MAX;
    int ok = cna_crt_effect_get_scanline_intensity(effect, &value) == CNA_RESULT_SUCCESS &&
        near_float(value, 0.3F) &&
        cna_crt_effect_get_curvature(effect, &value) == CNA_RESULT_SUCCESS &&
        near_float(value, 0.08F) &&
        cna_crt_effect_get_vignette_intensity(effect, &value) == CNA_RESULT_SUCCESS &&
        near_float(value, 0.25F) &&
        cna_crt_effect_get_mask_intensity(effect, &value) == CNA_RESULT_SUCCESS &&
        near_float(value, 0.35F) &&
        cna_crt_effect_get_mask_type(effect, &mask) == CNA_RESULT_SUCCESS &&
        mask == CNA_CRT_MASK_TYPE_APERTURE_GRILLE;

    if (ok) {
        /* The canonical setters clamp to 0..1; the C route only rejects non-finite input. */
        ok = cna_crt_effect_set_scanline_intensity(effect, 0.75F) == CNA_RESULT_SUCCESS &&
            cna_crt_effect_get_scanline_intensity(effect, &value) == CNA_RESULT_SUCCESS &&
            near_float(value, 0.75F) &&
            cna_crt_effect_set_curvature(effect, 5.0F) == CNA_RESULT_SUCCESS &&
            cna_crt_effect_get_curvature(effect, &value) == CNA_RESULT_SUCCESS &&
            near_float(value, 1.0F) &&
            cna_crt_effect_set_vignette_intensity(effect, -3.0F) == CNA_RESULT_SUCCESS &&
            cna_crt_effect_get_vignette_intensity(effect, &value) == CNA_RESULT_SUCCESS &&
            near_float(value, 0.0F) &&
            cna_crt_effect_set_mask_intensity(effect, 0.5F) == CNA_RESULT_SUCCESS &&
            cna_crt_effect_set_mask_type(effect, CNA_CRT_MASK_TYPE_SHADOW_MASK) ==
                CNA_RESULT_SUCCESS &&
            cna_crt_effect_get_mask_type(effect, &mask) == CNA_RESULT_SUCCESS &&
            mask == CNA_CRT_MASK_TYPE_SHADOW_MASK;
    }

    if (ok) {
        ok = cna_crt_effect_set_scanline_intensity(effect, NAN) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_crt_effect_set_curvature(effect, INFINITY) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_crt_effect_set_mask_type(effect, UINT32_C(9)) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_crt_effect_get_scanline_intensity(effect, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_crt_effect_get_mask_type(effect, 0) == CNA_RESULT_INVALID_ARGUMENT;
    }

    /* A CRT effect is an ordinary effect handle, and depth routes reject it by type. */
    if (ok) {
        uint64_t type_bytes = 0U;
        CNA_DepthEffectMode mode = UINT32_MAX;
        ok = cna_effect_get_type_name_byte_count(effect, &type_bytes) == CNA_RESULT_SUCCESS &&
            type_bytes != 0U &&
            cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_INVALID_HANDLE;
    }

    return cna_effect_destroy(effect) == CNA_RESULT_SUCCESS && ok;
}

static int validate_depth_effect(CNA_Handle graphics_device)
{
    CNA_EffectHandle effect = CNA_INVALID_HANDLE;
    const CNA_Result created = cna_depth_effect_create(graphics_device, &effect);
    if (created == CNA_RESULT_NOT_SUPPORTED) {
        return effect == CNA_INVALID_HANDLE;
    }
    if (created != CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_DepthEffectMode mode = UINT32_MAX;
    CNA_DitherMode dither = UINT32_MAX;
    int ok = cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_SUCCESS &&
        mode == CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT &&
        cna_depth_effect_get_dither_mode(effect, &dither) == CNA_RESULT_SUCCESS &&
        dither == CNA_DITHER_MODE_NONE;

    for (CNA_DepthEffectMode candidate = CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT;
         ok && candidate <= CNA_DEPTH_EFFECT_MODE_PALETTE_16;
         ++candidate) {
        ok = cna_depth_effect_set_mode(effect, candidate) == CNA_RESULT_SUCCESS &&
            cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_SUCCESS &&
            mode == candidate;
    }
    for (CNA_DitherMode candidate = CNA_DITHER_MODE_NONE;
         ok && candidate <= CNA_DITHER_MODE_BAYER_8X8;
         ++candidate) {
        ok = cna_depth_effect_set_dither_mode(effect, candidate) == CNA_RESULT_SUCCESS &&
            cna_depth_effect_get_dither_mode(effect, &dither) == CNA_RESULT_SUCCESS &&
            dither == candidate;
    }

    if (ok) {
        float value = 0.0F;
        ok = cna_depth_effect_set_mode(effect, UINT32_C(9)) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_depth_effect_set_dither_mode(effect, UINT32_C(9)) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_depth_effect_get_mode(effect, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_depth_effect_get_dither_mode(effect, 0) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_crt_effect_get_curvature(effect, &value) == CNA_RESULT_INVALID_HANDLE;
    }

    return cna_effect_destroy(effect) == CNA_RESULT_SUCCESS && ok;
}

static int validate_ascii_effect(CNA_Handle graphics_device)
{
    CNA_AsciiPostProcessEffectHandle ascii = CNA_INVALID_HANDLE;
    const CNA_Result created = cna_ascii_post_process_effect_create(graphics_device, &ascii);
    if (created == CNA_RESULT_NOT_SUPPORTED) {
        return ascii == CNA_INVALID_HANDLE;
    }
    if (created != CNA_RESULT_SUCCESS) {
        return 0;
    }

    int32_t width = 0;
    int32_t height = 0;
    CNA_AsciiQuantizeMode mode = UINT32_MAX;
    int ok = cna_ascii_post_process_effect_get_cell_size(ascii, &width, &height) ==
            CNA_RESULT_SUCCESS &&
        width == 8 && height == 8 &&
        cna_ascii_post_process_effect_get_quantize_mode(ascii, &mode) == CNA_RESULT_SUCCESS &&
        (mode == CNA_ASCII_QUANTIZE_MODE_BLACK_WHITE || mode == CNA_ASCII_QUANTIZE_MODE_COLOR);

    if (ok) {
        ok = cna_ascii_post_process_effect_set_cell_size(ascii, 4, 2) == CNA_RESULT_SUCCESS &&
            cna_ascii_post_process_effect_get_cell_size(ascii, &width, &height) ==
                CNA_RESULT_SUCCESS &&
            width == 4 && height == 2 &&
            cna_ascii_post_process_effect_set_cell_size(ascii, 0, 8) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_ascii_post_process_effect_set_cell_size(ascii, 8, -1) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_ascii_post_process_effect_get_cell_size(ascii, 0, &height) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_ascii_post_process_effect_set_quantize_mode(
                ascii, CNA_ASCII_QUANTIZE_MODE_COLOR) == CNA_RESULT_SUCCESS &&
            cna_ascii_post_process_effect_get_quantize_mode(ascii, &mode) ==
                CNA_RESULT_SUCCESS &&
            mode == CNA_ASCII_QUANTIZE_MODE_COLOR &&
            cna_ascii_post_process_effect_set_quantize_mode(ascii, UINT32_C(9)) ==
                CNA_RESULT_INVALID_ARGUMENT;
    }

    /* Grid dimensions are zero until the first draw. */
    int32_t columns = -1;
    int32_t rows = -1;
    if (ok) {
        ok = cna_ascii_post_process_effect_get_last_grid_dimensions(ascii, &columns, &rows) ==
                CNA_RESULT_SUCCESS &&
            columns == 0 && rows == 0;
    }

    CNA_Handle source = CNA_INVALID_HANDLE;
    if (ok) {
        const CNA_Texture2DCreateInfo info = {
            sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 16U, 16U, CNA_FALSE, {0U, 0U, 0U},
            CNA_SURFACE_FORMAT_COLOR};
        ok = cna_texture2d_create(graphics_device, &info, &source) == CNA_RESULT_SUCCESS;
    }
    if (ok) {
        const CNA_Rectangle destination = {0, 0, 32, 32};
        const CNA_Result drew = cna_ascii_post_process_effect_draw(ascii, source, &destination);
        ok = is_supported(drew) &&
            cna_ascii_post_process_effect_draw(ascii, source, 0) == drew &&
            cna_ascii_post_process_effect_draw(ascii, CNA_INVALID_HANDLE, 0) ==
                CNA_RESULT_INVALID_HANDLE;
        if (ok && drew == CNA_RESULT_SUCCESS) {
            ok = cna_ascii_post_process_effect_get_last_grid_dimensions(
                     ascii, &columns, &rows) == CNA_RESULT_SUCCESS &&
                columns > 0 && rows > 0;
        }
    }
    if (source != CNA_INVALID_HANDLE) {
        ok = cna_texture2d_destroy(source) == CNA_RESULT_SUCCESS && ok;
    }

    return cna_ascii_post_process_effect_destroy(ascii) == CNA_RESULT_SUCCESS &&
        cna_ascii_post_process_effect_destroy(ascii) == CNA_RESULT_INVALID_HANDLE && ok;
}

static CNA_Result on_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    (void)out_error;
    ExtState* const state = (ExtState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        cna_graphics_ext_is_available(&state->available) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    const int ok = state->available == CNA_TRUE
        ? (validate_crt_effect(graphics_device) && validate_depth_effect(graphics_device) &&
           validate_ascii_effect(graphics_device))
        : validate_unavailable(graphics_device);
    if (!ok) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int validate_effects(void)
{
    ExtState state = {CNA_FALSE, 0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char title[] = "C API graphics extensions";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, sizeof(title) - 1U},
        &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    const int ran = cna_game_run_one_frame(game) == CNA_RESULT_SUCCESS && state.validated == 1;
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS && ran;
}

int main(void)
{
    if (!validate_identities()) {
        return 1;
    }
    if (!validate_values()) {
        return 2;
    }
    if (!validate_effects()) {
        return 3;
    }
    return 0;
}
