// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef struct GraphicsSurfaceState {
    CNA_Handle texture;
    CNA_Handle sprite_font;
    CNA_Handle render_target_2d;
    CNA_Handle render_target_cube;
    int validated;
} GraphicsSurfaceState;

typedef struct WrongThreadState {
    CNA_Handle sprite_font;
    CNA_Result result;
} WrongThreadState;

static int copy_adapter_text(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    uint64_t byte_count,
    int description)
{
    char* bytes = 0;
    uint64_t copied = 0U;
    if (byte_count != 0U) {
        bytes = (char*)malloc((size_t)byte_count);
        if (bytes == 0) {
            return 0;
        }
    }
    const CNA_Result result = description
        ? cna_graphics_adapter_copy_description(
            graphics_device, adapter_index, bytes, byte_count, &copied)
        : cna_graphics_adapter_copy_device_name(
            graphics_device, adapter_index, bytes, byte_count, &copied);
    free(bytes);
    return result == CNA_RESULT_SUCCESS && copied == byte_count;
}

static CNA_Result validate_states(CNA_Handle graphics_device)
{
    CNA_BlendState blend;
    CNA_DepthStencilState depth;
    CNA_RasterizerState rasterizer;
    CNA_SamplerState sampler;
    if (cna_blend_state_init(CNA_BLEND_STATE_PRESET_ADDITIVE, &blend) != CNA_RESULT_SUCCESS ||
        blend.color_source_blend != CNA_BLEND_SOURCE_ALPHA ||
        blend.color_destination_blend != CNA_BLEND_ONE ||
        blend.blend_factor.r != UINT8_C(255) || blend.multi_sample_mask != -1 ||
        cna_depth_stencil_state_init(
            CNA_DEPTH_STENCIL_STATE_PRESET_NONE, &depth) != CNA_RESULT_SUCCESS ||
        depth.depth_buffer_enable != CNA_FALSE || depth.depth_buffer_write_enable != CNA_FALSE ||
        depth.depth_buffer_function != CNA_COMPARE_LESS_EQUAL ||
        cna_rasterizer_state_init(
            CNA_RASTERIZER_STATE_PRESET_CULL_NONE, &rasterizer) != CNA_RESULT_SUCCESS ||
        rasterizer.cull_mode != CNA_CULL_NONE || rasterizer.fill_mode != CNA_FILL_SOLID ||
        cna_sampler_state_init(
            CNA_SAMPLER_STATE_PRESET_POINT_CLAMP, &sampler) != CNA_RESULT_SUCCESS ||
        sampler.filter != CNA_TEXTURE_FILTER_POINT ||
        sampler.address_u != CNA_TEXTURE_ADDRESS_CLAMP ||
        cna_blend_state_init(UINT32_MAX, &blend) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_blend_state_init(CNA_BLEND_STATE_PRESET_OPAQUE, &blend) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (cna_graphics_device_set_blend_state(graphics_device, &blend) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_BlendState blend_copy = {0};
    blend_copy.struct_size = sizeof(CNA_BlendState);
    blend_copy.struct_version = UINT32_C(1);
    if (cna_graphics_device_get_blend_state(graphics_device, &blend_copy) != CNA_RESULT_SUCCESS ||
        blend_copy.color_source_blend != blend.color_source_blend ||
        blend_copy.color_destination_blend != blend.color_destination_blend) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Result depth_result = cna_graphics_device_set_depth_stencil_state(graphics_device, &depth);
    if (depth_result == CNA_RESULT_SUCCESS) {
        CNA_DepthStencilState depth_copy = {
            sizeof(CNA_DepthStencilState), UINT32_C(1),
            CNA_FALSE, CNA_FALSE, CNA_FALSE, CNA_FALSE,
            0U, 0U, 0, 0, 0, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
        };
        if (cna_graphics_device_get_depth_stencil_state(
                graphics_device, &depth_copy) != CNA_RESULT_SUCCESS ||
            depth_copy.depth_buffer_enable != CNA_FALSE) {
            return CNA_RESULT_INVALID_STATE;
        }
    } else if (depth_result != CNA_RESULT_NOT_SUPPORTED) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (cna_graphics_device_set_rasterizer_state(
            graphics_device, &rasterizer) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_RasterizerState rasterizer_copy = {0};
    rasterizer_copy.struct_size = sizeof(CNA_RasterizerState);
    rasterizer_copy.struct_version = UINT32_C(1);
    if (cna_graphics_device_get_rasterizer_state(
            graphics_device, &rasterizer_copy) != CNA_RESULT_SUCCESS ||
        rasterizer_copy.cull_mode != CNA_CULL_NONE) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (cna_graphics_device_set_sampler_state(
            graphics_device,
            CNA_SHADER_STAGE_PIXEL,
            UINT32_C(3),
            &sampler) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_SamplerState sampler_copy = {0};
    sampler_copy.struct_size = sizeof(CNA_SamplerState);
    sampler_copy.struct_version = UINT32_C(1);
    if (cna_graphics_device_get_sampler_state(
            graphics_device,
            CNA_SHADER_STAGE_PIXEL,
            UINT32_C(3),
            &sampler_copy) != CNA_RESULT_SUCCESS ||
        sampler_copy.filter != CNA_TEXTURE_FILTER_POINT ||
        cna_graphics_device_get_sampler_state(
            graphics_device,
            CNA_SHADER_STAGE_PIXEL,
            CNA_MAX_SAMPLERS,
            &sampler_copy) != CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Handle sprite_batch = CNA_INVALID_HANDLE;
    if (cna_sprite_batch_create(graphics_device, &sprite_batch) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    const CNA_Result begin_result = cna_sprite_batch_begin_with_states(
        sprite_batch,
        CNA_SPRITE_SORT_MODE_DEFERRED,
        &blend,
        &sampler,
        &depth,
        &rasterizer);
    if ((begin_result == CNA_RESULT_SUCCESS &&
         cna_sprite_batch_end(sprite_batch) != CNA_RESULT_SUCCESS) ||
        (begin_result != CNA_RESULT_SUCCESS && begin_result != CNA_RESULT_NOT_SUPPORTED)) {
        return CNA_RESULT_INVALID_STATE;
    }

    /* CBIND-044A: the two remaining canonical Begin overloads, expressed as one route. A null
       transform is the identity the effect-only overload uses, and CNA_INVALID_HANDLE selects the
       default sprite effect exactly as a null Effect* does for the canonical call. */
    {
        CNA_Matrix transform;
        CNA_Result effect_begin = CNA_RESULT_SUCCESS;
        if (cna_matrix_create_scale_scalar(2.0f, &transform) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
        effect_begin = cna_sprite_batch_begin_with_effect(
            sprite_batch,
            CNA_SPRITE_SORT_MODE_DEFERRED,
            &blend,
            &sampler,
            &depth,
            &rasterizer,
            CNA_INVALID_HANDLE,
            0);
        if ((effect_begin == CNA_RESULT_SUCCESS &&
             cna_sprite_batch_end(sprite_batch) != CNA_RESULT_SUCCESS) ||
            (effect_begin != CNA_RESULT_SUCCESS && effect_begin != CNA_RESULT_NOT_SUPPORTED)) {
            return CNA_RESULT_INVALID_STATE;
        }
        effect_begin = cna_sprite_batch_begin_with_effect(
            sprite_batch,
            CNA_SPRITE_SORT_MODE_DEFERRED,
            &blend,
            &sampler,
            &depth,
            &rasterizer,
            CNA_INVALID_HANDLE,
            &transform);
        if ((effect_begin == CNA_RESULT_SUCCESS &&
             cna_sprite_batch_end(sprite_batch) != CNA_RESULT_SUCCESS) ||
            (effect_begin != CNA_RESULT_SUCCESS && effect_begin != CNA_RESULT_NOT_SUPPORTED)) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* A non-finite component is refused before anything is begun, so the batch stays usable. */
        transform.m11 = 1.0f / 0.0f;
        if (cna_sprite_batch_begin_with_effect(
                sprite_batch,
                CNA_SPRITE_SORT_MODE_DEFERRED,
                &blend, &sampler, &depth, &rasterizer,
                CNA_INVALID_HANDLE,
                &transform) != CNA_RESULT_INVALID_ARGUMENT) {
            return CNA_RESULT_INVALID_STATE;
        }
        transform.m11 = 2.0f;
        /* An undefined sort mode and a handle of the wrong family are refused the same way here as
           everywhere else. */
        if (cna_sprite_batch_begin_with_effect(
                sprite_batch, UINT32_MAX, &blend, &sampler, &depth, &rasterizer,
                CNA_INVALID_HANDLE, &transform) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_sprite_batch_begin_with_effect(
                sprite_batch, CNA_SPRITE_SORT_MODE_DEFERRED, &blend, &sampler, &depth, &rasterizer,
                sprite_batch, &transform) != CNA_RESULT_INVALID_HANDLE) {
            return CNA_RESULT_INVALID_STATE;
        }
    }

    if (cna_sprite_batch_destroy(sprite_batch) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result validate_display(CNA_Handle graphics_device)
{
    CNA_DisplayMode first;
    CNA_DisplayMode same;
    CNA_Bool equal = CNA_FALSE;
    if (cna_display_mode_init(1920, 1080, CNA_SURFACE_FORMAT_COLOR, &first) !=
            CNA_RESULT_SUCCESS ||
        first.aspect_ratio <= 1.7f || first.aspect_ratio >= 1.8f ||
        cna_display_mode_init(1920, 1080, CNA_SURFACE_FORMAT_COLOR, &same) !=
            CNA_RESULT_SUCCESS ||
        cna_display_mode_equals(&first, &same, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_PresentationParameters parameters;
    CNA_PresentationParameters clone;
    CNA_Rectangle bounds;
    if (cna_presentation_parameters_init(&parameters) != CNA_RESULT_SUCCESS ||
        parameters.back_buffer_width != 800 || parameters.back_buffer_height != 480 ||
        parameters.back_buffer_format != CNA_SURFACE_FORMAT_COLOR ||
        cna_presentation_parameters_clone(&parameters, &clone) != CNA_RESULT_SUCCESS ||
        memcmp(&parameters, &clone, sizeof(parameters)) != 0 ||
        cna_presentation_parameters_get_bounds(&parameters, &bounds) != CNA_RESULT_SUCCESS ||
        bounds.x != 0 || bounds.y != 0 || bounds.width != 800 || bounds.height != 480) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_PresentationParameters applied = {0};
    applied.struct_size = sizeof(CNA_PresentationParameters);
    applied.struct_version = UINT32_C(1);
    CNA_DisplayMode device_mode = {sizeof(CNA_DisplayMode), UINT32_C(1), 0, 0, 0.0f, 0U};
    if (cna_graphics_device_get_presentation_parameters(
            graphics_device, &applied) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_set_presentation_parameters(
            graphics_device, &applied) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_display_mode(graphics_device, &device_mode) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    uint64_t adapter_count = 0U;
    CNA_GraphicsAdapterInfo adapter_info = {
        sizeof(CNA_GraphicsAdapterInfo), UINT32_C(1), 0U,
        CNA_FALSE, CNA_FALSE, CNA_FALSE, CNA_FALSE,
        0, 0, 0, 0, 0U, 0U
    };
    if (cna_graphics_adapter_get_count(graphics_device, &adapter_count) != CNA_RESULT_SUCCESS ||
        adapter_count == 0U ||
        cna_graphics_adapter_get_info(graphics_device, 0U, &adapter_info) != CNA_RESULT_SUCCESS ||
        adapter_info.adapter_index != 0U || adapter_info.is_default_adapter != CNA_TRUE ||
        !copy_adapter_text(
            graphics_device, 0U, adapter_info.description_byte_length, 1) ||
        !copy_adapter_text(
            graphics_device, 0U, adapter_info.device_name_byte_length, 0)) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_DisplayMode current = {sizeof(CNA_DisplayMode), UINT32_C(1), 0, 0, 0.0f, 0U};
    uint64_t mode_count = 0U;
    if (cna_graphics_adapter_get_current_display_mode(
            graphics_device, 0U, &current) != CNA_RESULT_SUCCESS ||
        cna_graphics_adapter_get_display_mode_count(
            graphics_device,
            0U,
            CNA_FALSE,
            CNA_SURFACE_FORMAT_COLOR,
            &mode_count) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_DisplayMode* modes = mode_count == 0U
        ? 0 : (CNA_DisplayMode*)malloc((size_t)mode_count * sizeof(CNA_DisplayMode));
    if (mode_count != 0U && modes == 0) {
        return CNA_RESULT_OUT_OF_MEMORY;
    }
    uint64_t copied_modes = 0U;
    const CNA_Result copy_result = cna_graphics_adapter_copy_display_modes(
        graphics_device,
        0U,
        CNA_FALSE,
        CNA_SURFACE_FORMAT_COLOR,
        modes,
        mode_count,
        &copied_modes);
    free(modes);
    if (copy_result != CNA_RESULT_SUCCESS || copied_modes != mode_count) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Bool profile_supported = CNA_FALSE;
    CNA_GraphicsFormatSelection selection = {
        sizeof(CNA_GraphicsFormatSelection), UINT32_C(1), CNA_FALSE,
        {0U, 0U, 0U}, 0U, 0U, 0
    };
    if (cna_graphics_adapter_is_profile_supported(
            graphics_device,
            0U,
            CNA_GRAPHICS_PROFILE_REACH,
            &profile_supported) != CNA_RESULT_SUCCESS ||
        profile_supported != CNA_TRUE ||
        cna_graphics_adapter_query_render_target_format(
            graphics_device,
            0U,
            CNA_GRAPHICS_PROFILE_REACH,
            CNA_SURFACE_FORMAT_COLOR,
            CNA_DEPTH_FORMAT_NONE,
            0,
            &selection) != CNA_RESULT_SUCCESS ||
        selection.format != CNA_SURFACE_FORMAT_COLOR ||
        cna_graphics_adapter_query_backbuffer_format(
            graphics_device,
            0U,
            CNA_GRAPHICS_PROFILE_REACH,
            CNA_SURFACE_FORMAT_COLOR,
            CNA_DEPTH_FORMAT_NONE,
            0,
            &selection) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (cna_graphics_adapter_set_device_preferences(
            graphics_device, 0U, CNA_TRUE, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_graphics_adapter_get_info(
            graphics_device, 0U, &adapter_info) != CNA_RESULT_SUCCESS ||
        adapter_info.use_null_device != CNA_TRUE ||
        cna_graphics_adapter_set_device_preferences(
            graphics_device, 0U, CNA_FALSE, CNA_FALSE) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_NativeHandleValue native_value = UINT64_C(55);
    if (cna_graphics_adapter_get_native_monitor_handle(
            graphics_device, 0U, &native_value) != CNA_RESULT_NOT_SUPPORTED ||
        native_value != UINT64_C(0) ||
        cna_graphics_device_get_native_window_handle(
            graphics_device, &native_value) != CNA_RESULT_NOT_SUPPORTED ||
        native_value != UINT64_C(0) ||
        cna_graphics_adapters_refresh(graphics_device) != CNA_RESULT_NOT_SUPPORTED) {
        return CNA_RESULT_INVALID_STATE;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result create_font(
    CNA_Handle graphics_device,
    GraphicsSurfaceState* state)
{
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 8U, 8U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR
    };
    if (cna_texture2d_create(
            graphics_device, &texture_info, &state->texture) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    const CNA_SpriteFontGlyph glyphs[] = {
        {sizeof(CNA_SpriteFontGlyph), UINT32_C(1), {0, 0, 2, 8}, {0, 0, 2, 8},
         (CNA_Char16)'A', 0U, {0.0f, 5.0f, 0.0f}},
        {sizeof(CNA_SpriteFontGlyph), UINT32_C(1), {2, 0, 2, 8}, {0, 0, 2, 8},
         (CNA_Char16)'B', 0U, {0.0f, 5.0f, 0.0f}},
        {sizeof(CNA_SpriteFontGlyph), UINT32_C(1), {4, 0, 2, 8}, {0, 0, 2, 8},
         (CNA_Char16)'?', 0U, {0.0f, 5.0f, 0.0f}}
    };
    const CNA_SpriteFontCreateInfo font_info = {
        sizeof(CNA_SpriteFontCreateInfo),
        UINT32_C(1),
        state->texture,
        glyphs,
        sizeof(glyphs) / sizeof(glyphs[0]),
        10,
        1.0f,
        (CNA_Char16)'?',
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U}
    };
    if (cna_sprite_font_create(&font_info, &state->sprite_font) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(state->texture) != CNA_RESULT_INVALID_STATE) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_SpriteFontInfo info = {
        sizeof(CNA_SpriteFontInfo), UINT32_C(1), 0U, 0, 0.0f, 0U,
        CNA_FALSE, {0U, 0U, 0U, 0U, 0U}
    };
    CNA_Char16 characters[3] = {0U, 0U, 0U};
    uint64_t character_count = 0U;
    CNA_Vector2 measured = {0.0f, 0.0f};
    if (cna_sprite_font_get_info(state->sprite_font, &info) != CNA_RESULT_SUCCESS ||
        info.character_count != 3U || info.line_spacing != 10 || info.spacing != 1.0f ||
        info.has_default_character != CNA_TRUE || info.default_character != (CNA_Char16)'?' ||
        cna_sprite_font_copy_characters(
            state->sprite_font, characters, 2U, &character_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        character_count != 3U ||
        cna_sprite_font_copy_characters(
            state->sprite_font, characters, 3U, &character_count) != CNA_RESULT_SUCCESS ||
        characters[0] != (CNA_Char16)'A' || characters[1] != (CNA_Char16)'B' ||
        characters[2] != (CNA_Char16)'?' ||
        cna_sprite_font_measure_utf8(
            state->sprite_font,
            (CNA_StringView){"AB\nA", UINT64_C(4)},
            &measured) != CNA_RESULT_SUCCESS ||
        measured.x != 11.0f || measured.y != 20.0f ||
        cna_sprite_font_set_default_character(
            state->sprite_font, CNA_TRUE, (CNA_Char16)'Z') != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sprite_font_set_default_character(
            state->sprite_font, CNA_TRUE, (CNA_Char16)'?') != CNA_RESULT_SUCCESS ||
        cna_sprite_font_set_line_spacing(state->sprite_font, 12) != CNA_RESULT_SUCCESS ||
        cna_sprite_font_set_spacing(state->sprite_font, 2.0f) != CNA_RESULT_SUCCESS ||
        cna_sprite_font_measure_utf8(
            state->sprite_font,
            (CNA_StringView){"AZ", UINT64_C(2)},
            &measured) != CNA_RESULT_SUCCESS ||
        measured.x != 12.0f || measured.y != 12.0f) {
        return CNA_RESULT_INVALID_STATE;
    }
    /* CBIND-055: the glyph table reads back exactly as it went in, which is what lets a caller
       hold one font instead of a native one it can measure and a private copy it can draw from.
       Measuring answers the size of a whole string; placing a glyph needs its atlas rectangle,
       its cropping offset and its three kerning values, and none of those were reachable. */
    CNA_SpriteFontGlyph read_back[3];
    uint64_t glyph_count = 0U;
    memset(read_back, 0, sizeof(read_back));
    if (cna_sprite_font_copy_glyphs(state->sprite_font, read_back, 2U, &glyph_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        glyph_count != 3U ||
        cna_sprite_font_copy_glyphs(state->sprite_font, 0, 0U, &glyph_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        glyph_count != 3U ||
        cna_sprite_font_copy_glyphs(state->sprite_font, read_back, 3U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_sprite_font_copy_glyphs(state->sprite_font, read_back, 3U, &glyph_count) !=
            CNA_RESULT_SUCCESS ||
        glyph_count != 3U) {
        return CNA_RESULT_INVALID_STATE;
    }
    for (uint64_t index = 0U; index < glyph_count; ++index) {
        const CNA_SpriteFontGlyph* const source = &glyphs[index];
        const CNA_SpriteFontGlyph* const copy = &read_back[index];
        if (copy->struct_size != (uint32_t)sizeof(CNA_SpriteFontGlyph) ||
            copy->struct_version != UINT32_C(1) || copy->reserved != 0U ||
            copy->character != source->character ||
            copy->glyph_bounds.x != source->glyph_bounds.x ||
            copy->glyph_bounds.y != source->glyph_bounds.y ||
            copy->glyph_bounds.width != source->glyph_bounds.width ||
            copy->glyph_bounds.height != source->glyph_bounds.height ||
            copy->cropping.x != source->cropping.x ||
            copy->cropping.y != source->cropping.y ||
            copy->cropping.width != source->cropping.width ||
            copy->cropping.height != source->cropping.height ||
            copy->kerning.x != source->kerning.x || copy->kerning.y != source->kerning.y ||
            copy->kerning.z != source->kerning.z) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* Glyph i describes character i, which is the ordering both routes promise. */
        if (copy->character != characters[index]) {
            return CNA_RESULT_INVALID_STATE;
        }
    }

    const char invalid_utf8[] = {(char)0xC3, '('};
    if (cna_sprite_font_measure_utf8(
            state->sprite_font,
            (CNA_StringView){invalid_utf8, UINT64_C(2)},
            &measured) != CNA_RESULT_ENCODING) {
        return CNA_RESULT_INVALID_STATE;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result create_render_targets(
    CNA_Handle graphics_device,
    GraphicsSurfaceState* state)
{
    CNA_Bool preserves = CNA_FALSE;
    if (cna_render_target_usage_preserves_contents(
            CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS, &preserves) != CNA_RESULT_SUCCESS ||
        preserves != CNA_FALSE ||
        cna_render_target_usage_preserves_contents(
            CNA_RENDER_TARGET_USAGE_PLATFORM_CONTENTS, &preserves) != CNA_RESULT_SUCCESS ||
        preserves != CNA_TRUE) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_RenderTarget2DCreateInfo create_2d = {
        sizeof(CNA_RenderTarget2DCreateInfo), UINT32_C(1), 4U, 4U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR,
        CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS, 0U
    };
    const CNA_RenderTargetCubeCreateInfo create_cube = {
        sizeof(CNA_RenderTargetCubeCreateInfo), UINT32_C(1), 4U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR,
        CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS
    };
    if (cna_render_target2d_create(
            graphics_device, &create_2d, &state->render_target_2d) != CNA_RESULT_SUCCESS ||
        cna_render_target_cube_create(
            graphics_device, &create_cube, &state->render_target_cube) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_RenderTargetInfo info_2d = {0};
    CNA_RenderTargetInfo info_cube = {0};
    info_2d.struct_size = sizeof(CNA_RenderTargetInfo);
    info_2d.struct_version = UINT32_C(1);
    info_cube.struct_size = sizeof(CNA_RenderTargetInfo);
    info_cube.struct_version = UINT32_C(1);
    CNA_Texture2DInfo texture_info = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    if (cna_render_target_get_info(state->render_target_2d, &info_2d) != CNA_RESULT_SUCCESS ||
        info_2d.kind != CNA_RENDER_TARGET_KIND_2D || info_2d.width != 4U ||
        info_2d.height != 4U || info_2d.usage != CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS ||
        info_2d.is_content_lost != CNA_FALSE ||
        cna_texture2d_get_info(state->render_target_2d, &texture_info) != CNA_RESULT_SUCCESS ||
        texture_info.width != 4U ||
        cna_render_target_get_info(state->render_target_cube, &info_cube) != CNA_RESULT_SUCCESS ||
        info_cube.kind != CNA_RENDER_TARGET_KIND_CUBE || info_cube.width != 4U ||
        info_cube.height != 4U || info_cube.is_content_lost != CNA_FALSE) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_RenderTargetBinding binding = {
        sizeof(CNA_RenderTargetBinding), UINT32_C(1), state->render_target_2d,
        0, CNA_CUBE_MAP_FACE_POSITIVE_X
    };
    const CNA_Result set_2d = cna_graphics_device_set_render_targets(
        graphics_device, &binding, 1U);
    uint64_t binding_count = 0U;
    if (info_2d.renderer_available == CNA_TRUE) {
        CNA_RenderTargetBinding copied = {0U, 0U, CNA_INVALID_HANDLE, 0, 0U};
        if (set_2d != CNA_RESULT_SUCCESS ||
            cna_graphics_device_get_render_target_count(
                graphics_device, &binding_count) != CNA_RESULT_SUCCESS ||
            binding_count != 1U ||
            cna_graphics_device_copy_render_targets(
                graphics_device, &copied, 1U, &binding_count) != CNA_RESULT_SUCCESS ||
            copied.render_target != state->render_target_2d ||
            cna_render_target_destroy(state->render_target_2d) != CNA_RESULT_INVALID_STATE ||
            cna_graphics_device_set_render_target2d(
                graphics_device, CNA_INVALID_HANDLE) != CNA_RESULT_SUCCESS ||
            cna_graphics_device_set_render_target2d(
                graphics_device, state->render_target_2d) != CNA_RESULT_SUCCESS ||
            cna_graphics_device_set_render_target2d(
                graphics_device, CNA_INVALID_HANDLE) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
    } else if (set_2d != CNA_RESULT_NOT_SUPPORTED ||
               cna_graphics_device_get_render_target_count(
                   graphics_device, &binding_count) != CNA_RESULT_SUCCESS ||
               binding_count != 0U) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_Result set_cube = cna_graphics_device_set_render_target_cube(
        graphics_device, state->render_target_cube, CNA_CUBE_MAP_FACE_NEGATIVE_Z);
    if (info_cube.renderer_available == CNA_TRUE) {
        if (set_cube != CNA_RESULT_SUCCESS ||
            cna_graphics_device_set_render_target_cube(
                graphics_device,
                CNA_INVALID_HANDLE,
                CNA_CUBE_MAP_FACE_POSITIVE_X) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
    } else if (set_cube != CNA_RESULT_NOT_SUPPORTED) {
        return CNA_RESULT_INVALID_STATE;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    (void)out_error;
    GraphicsSurfaceState* const state = (GraphicsSurfaceState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        validate_states(graphics_device) != CNA_RESULT_SUCCESS ||
        validate_display(graphics_device) != CNA_RESULT_SUCCESS ||
        create_font(graphics_device, state) != CNA_RESULT_SUCCESS ||
        create_render_targets(graphics_device, state) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_font_on_wrong_thread(void* context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_SpriteFontInfo info = {
        sizeof(CNA_SpriteFontInfo), UINT32_C(1), 0U, 0, 0.0f, 0U,
        CNA_FALSE, {0U, 0U, 0U, 0U, 0U}
    };
    state->result = cna_sprite_font_get_info(state->sprite_font, &info);
    return 0;
}

int main(void)
{
    GraphicsSurfaceState state = {
        CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        0
    };
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    static const char title[] = "C API graphics surface";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, sizeof(title) - 1U},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.validated != 1 ||
        state.texture == CNA_INVALID_HANDLE || state.sprite_font == CNA_INVALID_HANDLE ||
        state.render_target_2d == CNA_INVALID_HANDLE ||
        state.render_target_cube == CNA_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE) {
        return 1;
    }

    WrongThreadState wrong_thread = {state.sprite_font, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_font_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return 2;
    }

    if (cna_sprite_font_destroy(state.sprite_font) != CNA_RESULT_SUCCESS ||
        cna_sprite_font_destroy(state.sprite_font) != CNA_RESULT_INVALID_HANDLE ||
        cna_texture2d_destroy(state.texture) != CNA_RESULT_SUCCESS ||
        cna_render_target_destroy(state.render_target_2d) != CNA_RESULT_SUCCESS ||
        cna_render_target_destroy(state.render_target_cube) != CNA_RESULT_SUCCESS ||
        cna_render_target_destroy(state.render_target_cube) != CNA_RESULT_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 3;
    }
    return 0;
}
