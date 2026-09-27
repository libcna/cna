// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct ExtState {
  CNA_Bool available;
  int validated;
} ExtState;

static int near_float(const float left, const float right) {
  return fabsf(left - right) <= 0.0001F;
}

static int is_supported(const CNA_Result result) {
  return result == CNA_RESULT_SUCCESS || result == CNA_RESULT_NOT_SUPPORTED;
}

static int validate_identities(void) {
  return CNA_ASCII_QUANTIZE_MODE_BLACK_WHITE == UINT32_C(0) &&
         CNA_ASCII_QUANTIZE_MODE_COLOR == UINT32_C(1) &&
         CNA_CRT_MASK_TYPE_NONE == UINT32_C(0) &&
         CNA_CRT_MASK_TYPE_APERTURE_GRILLE == UINT32_C(1) &&
         CNA_CRT_MASK_TYPE_SHADOW_MASK == UINT32_C(2) &&
         CNA_DITHER_MODE_NONE == UINT32_C(0) &&
         CNA_DITHER_MODE_BAYER_4X4 == UINT32_C(1) &&
         CNA_DITHER_MODE_BAYER_8X8 == UINT32_C(2) &&
         CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT == UINT32_C(0) &&
         CNA_DEPTH_EFFECT_MODE_COLOR_8_BIT == UINT32_C(1) &&
         CNA_DEPTH_EFFECT_MODE_GRAYSCALE_4_BIT == UINT32_C(2) &&
         CNA_DEPTH_EFFECT_MODE_GRAYSCALE_2_BIT == UINT32_C(3) &&
         CNA_DEPTH_EFFECT_MODE_GRAYSCALE_1_BIT == UINT32_C(4) &&
         CNA_DEPTH_EFFECT_MODE_PALETTE_256 == UINT32_C(5) &&
         CNA_DEPTH_EFFECT_MODE_PALETTE_16 == UINT32_C(6);
}

static int validate_unavailable(CNA_Handle graphics_device) {
  CNA_EffectHandle effect = UINT64_C(7);
  CNA_AsciiPostProcessEffectHandle ascii = UINT64_C(7);
  float value = 0.0F;
  CNA_CRTMaskType mask = UINT32_MAX;
  CNA_DepthEffectMode mode = UINT32_MAX;
  CNA_DitherMode dither = UINT32_MAX;
  int32_t width = 0;
  int32_t height = 0;

  CNA_DebugDrawHandle debug = CNA_INVALID_HANDLE;
  return cna_debug_draw_create(graphics_device, &debug) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_create(graphics_device, &effect) ==
             CNA_RESULT_NOT_SUPPORTED &&
         effect == CNA_INVALID_HANDLE &&
         cna_crt_effect_get_scanline_intensity(effect, &value) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_set_scanline_intensity(effect, 0.5F) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_get_curvature(effect, &value) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_set_curvature(effect, 0.5F) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_get_vignette_intensity(effect, &value) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_set_vignette_intensity(effect, 0.5F) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_get_mask_intensity(effect, &value) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_set_mask_intensity(effect, 0.5F) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_get_mask_type(effect, &mask) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_crt_effect_set_mask_type(effect, CNA_CRT_MASK_TYPE_NONE) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_depth_effect_create(graphics_device, &effect) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_NOT_SUPPORTED &&
         cna_depth_effect_set_mode(effect, CNA_DEPTH_EFFECT_MODE_COLOR_8_BIT) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_depth_effect_get_dither_mode(effect, &dither) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_depth_effect_set_dither_mode(effect, CNA_DITHER_MODE_BAYER_4X4) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_ascii_post_process_effect_create(graphics_device, &ascii) ==
             CNA_RESULT_NOT_SUPPORTED &&
         ascii == CNA_INVALID_HANDLE &&
         cna_ascii_post_process_effect_get_cell_size(ascii, &width, &height) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_ascii_post_process_effect_set_cell_size(ascii, 4, 4) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_ascii_post_process_effect_get_quantize_mode(ascii, &mask) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_ascii_post_process_effect_set_quantize_mode(
             ascii, CNA_ASCII_QUANTIZE_MODE_COLOR) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_ascii_post_process_effect_draw(ascii, CNA_INVALID_HANDLE, 0) ==
             CNA_RESULT_NOT_SUPPORTED &&
         cna_ascii_post_process_effect_get_last_grid_dimensions(
             ascii, &width, &height) == CNA_RESULT_NOT_SUPPORTED &&
         cna_ascii_post_process_effect_destroy(ascii) ==
             CNA_RESULT_NOT_SUPPORTED;
}

static int validate_debug_draw(CNA_Handle graphics_device) {
  CNA_DebugDrawHandle debug = CNA_INVALID_HANDLE;
  if (cna_debug_draw_create(graphics_device, &debug) != CNA_RESULT_SUCCESS) {
    return 0;
  }
  CNA_Matrix identity = {0};
  identity.m11 = identity.m22 = identity.m33 = identity.m44 = 1.0F;
  const CNA_Vector3 from = {0.0F, 0.0F, 0.0F};
  const CNA_Vector3 to = {1.0F, 0.0F, 0.0F};
  const CNA_Color color = {255U, 0U, 0U, 255U};
  const CNA_BoundingBox box = {from, to};
  const CNA_BoundingSphere sphere = {from, 1.0F};
  const CNA_BoundingFrustum frustum = {identity};
  CNA_Bool depth_tested = CNA_FALSE;
  int32_t lines = -1;
  uint64_t count = 0;
  CNA_VertexPositionColor vertices[2];
  const int ok =
      cna_debug_draw_begin(debug, &identity, &identity) == CNA_RESULT_SUCCESS &&
      cna_debug_draw_add_line(debug, &from, &to, color) == CNA_RESULT_SUCCESS &&
      cna_debug_draw_get_line_count(debug, &lines) == CNA_RESULT_SUCCESS &&
      lines == 1 &&
      cna_debug_draw_copy_vertices(debug, CNA_TRUE, vertices, 2U, &count) ==
          CNA_RESULT_SUCCESS &&
      count == 2U && vertices[0].position.x == 0.0F &&
      vertices[1].position.x == 1.0F &&
      cna_debug_draw_add_box(debug, &box, color) == CNA_RESULT_SUCCESS &&
      cna_debug_draw_add_sphere(debug, &from, 1.0F, color, 8) ==
          CNA_RESULT_SUCCESS &&
      cna_debug_draw_add_bounding_sphere(debug, &sphere, color, 8) ==
          CNA_RESULT_SUCCESS &&
      cna_debug_draw_add_frustum(debug, frustum, color) == CNA_RESULT_SUCCESS &&
      cna_debug_draw_add_cross(debug, &from, 1.0F, color) ==
          CNA_RESULT_SUCCESS &&
      cna_debug_draw_is_depth_tested(debug, &depth_tested) ==
          CNA_RESULT_SUCCESS &&
      depth_tested == CNA_TRUE &&
      cna_debug_draw_end(debug) == CNA_RESULT_SUCCESS &&
      cna_debug_draw_clear(debug) == CNA_RESULT_SUCCESS &&
      cna_debug_draw_get_line_count(debug, &lines) == CNA_RESULT_SUCCESS &&
      lines == 0;
  return cna_debug_draw_destroy(debug) == CNA_RESULT_SUCCESS && ok;
}

static int validate_crt_effect(CNA_Handle graphics_device) {
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
  int ok =
      cna_crt_effect_get_scanline_intensity(effect, &value) ==
          CNA_RESULT_SUCCESS &&
      near_float(value, 0.3F) &&
      cna_crt_effect_get_curvature(effect, &value) == CNA_RESULT_SUCCESS &&
      near_float(value, 0.08F) &&
      cna_crt_effect_get_vignette_intensity(effect, &value) ==
          CNA_RESULT_SUCCESS &&
      near_float(value, 0.25F) &&
      cna_crt_effect_get_mask_intensity(effect, &value) == CNA_RESULT_SUCCESS &&
      near_float(value, 0.35F) &&
      cna_crt_effect_get_mask_type(effect, &mask) == CNA_RESULT_SUCCESS &&
      mask == CNA_CRT_MASK_TYPE_APERTURE_GRILLE;

  if (ok) {
    /* The canonical setters clamp to 0..1; the C route only rejects non-finite
     * input. */
    ok =
        cna_crt_effect_set_scanline_intensity(effect, 0.75F) ==
            CNA_RESULT_SUCCESS &&
        cna_crt_effect_get_scanline_intensity(effect, &value) ==
            CNA_RESULT_SUCCESS &&
        near_float(value, 0.75F) &&
        cna_crt_effect_set_curvature(effect, 5.0F) == CNA_RESULT_SUCCESS &&
        cna_crt_effect_get_curvature(effect, &value) == CNA_RESULT_SUCCESS &&
        near_float(value, 1.0F) &&
        cna_crt_effect_set_vignette_intensity(effect, -3.0F) ==
            CNA_RESULT_SUCCESS &&
        cna_crt_effect_get_vignette_intensity(effect, &value) ==
            CNA_RESULT_SUCCESS &&
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
         cna_crt_effect_set_curvature(effect, INFINITY) ==
             CNA_RESULT_INVALID_ARGUMENT &&
         cna_crt_effect_set_mask_type(effect, UINT32_C(9)) ==
             CNA_RESULT_INVALID_ARGUMENT &&
         cna_crt_effect_get_scanline_intensity(effect, 0) ==
             CNA_RESULT_INVALID_ARGUMENT &&
         cna_crt_effect_get_mask_type(effect, 0) == CNA_RESULT_INVALID_ARGUMENT;
  }

  /* A CRT effect is an ordinary effect handle, and depth routes reject it by
   * type. */
  if (ok) {
    uint64_t type_bytes = 0U;
    CNA_DepthEffectMode mode = UINT32_MAX;
    ok = cna_effect_get_type_name_byte_count(effect, &type_bytes) ==
             CNA_RESULT_SUCCESS &&
         type_bytes != 0U &&
         cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_INVALID_HANDLE;
  }

  return cna_effect_destroy(effect) == CNA_RESULT_SUCCESS && ok;
}

static int validate_depth_effect(CNA_Handle graphics_device) {
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
  int ok =
      cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_SUCCESS &&
      mode == CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT &&
      cna_depth_effect_get_dither_mode(effect, &dither) == CNA_RESULT_SUCCESS &&
      dither == CNA_DITHER_MODE_NONE;

  for (CNA_DepthEffectMode candidate = CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT;
       ok && candidate <= CNA_DEPTH_EFFECT_MODE_PALETTE_16; ++candidate) {
    ok = cna_depth_effect_set_mode(effect, candidate) == CNA_RESULT_SUCCESS &&
         cna_depth_effect_get_mode(effect, &mode) == CNA_RESULT_SUCCESS &&
         mode == candidate;
  }
  for (CNA_DitherMode candidate = CNA_DITHER_MODE_NONE;
       ok && candidate <= CNA_DITHER_MODE_BAYER_8X8; ++candidate) {
    ok = cna_depth_effect_set_dither_mode(effect, candidate) ==
             CNA_RESULT_SUCCESS &&
         cna_depth_effect_get_dither_mode(effect, &dither) ==
             CNA_RESULT_SUCCESS &&
         dither == candidate;
  }

  if (ok) {
    float value = 0.0F;
    ok = cna_depth_effect_set_mode(effect, UINT32_C(9)) ==
             CNA_RESULT_INVALID_ARGUMENT &&
         cna_depth_effect_set_dither_mode(effect, UINT32_C(9)) ==
             CNA_RESULT_INVALID_ARGUMENT &&
         cna_depth_effect_get_mode(effect, 0) == CNA_RESULT_INVALID_ARGUMENT &&
         cna_depth_effect_get_dither_mode(effect, 0) ==
             CNA_RESULT_INVALID_ARGUMENT &&
         cna_crt_effect_get_curvature(effect, &value) ==
             CNA_RESULT_INVALID_HANDLE;
  }

  return cna_effect_destroy(effect) == CNA_RESULT_SUCCESS && ok;
}

static int validate_ascii_effect(CNA_Handle graphics_device) {
  CNA_AsciiPostProcessEffectHandle ascii = CNA_INVALID_HANDLE;
  const CNA_Result created =
      cna_ascii_post_process_effect_create(graphics_device, &ascii);
  if (created == CNA_RESULT_NOT_SUPPORTED) {
    return ascii == CNA_INVALID_HANDLE;
  }
  if (created != CNA_RESULT_SUCCESS) {
    return 0;
  }

  int32_t width = 0;
  int32_t height = 0;
  CNA_AsciiQuantizeMode mode = UINT32_MAX;
  int ok = cna_ascii_post_process_effect_get_cell_size(
               ascii, &width, &height) == CNA_RESULT_SUCCESS &&
           width == 8 && height == 8 &&
           cna_ascii_post_process_effect_get_quantize_mode(ascii, &mode) ==
               CNA_RESULT_SUCCESS &&
           (mode == CNA_ASCII_QUANTIZE_MODE_BLACK_WHITE ||
            mode == CNA_ASCII_QUANTIZE_MODE_COLOR);

  if (ok) {
    ok = cna_ascii_post_process_effect_set_cell_size(ascii, 4, 2) ==
             CNA_RESULT_SUCCESS &&
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
    ok = cna_ascii_post_process_effect_get_last_grid_dimensions(
             ascii, &columns, &rows) == CNA_RESULT_SUCCESS &&
         columns == 0 && rows == 0;
  }

  CNA_Handle source = CNA_INVALID_HANDLE;
  if (ok) {
    const CNA_Texture2DCreateInfo info = {sizeof(CNA_Texture2DCreateInfo),
                                          UINT32_C(1),
                                          16U,
                                          16U,
                                          CNA_FALSE,
                                          {0U, 0U, 0U},
                                          CNA_SURFACE_FORMAT_COLOR};
    ok = cna_texture2d_create(graphics_device, &info, &source) ==
         CNA_RESULT_SUCCESS;
  }
  if (ok) {
    const CNA_Rectangle destination = {0, 0, 32, 32};
    const CNA_Result drew =
        cna_ascii_post_process_effect_draw(ascii, source, &destination);
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
         cna_ascii_post_process_effect_destroy(ascii) ==
             CNA_RESULT_INVALID_HANDLE &&
         ok;
}

static CNA_Result on_load(CNA_Handle game, const CNA_GameTime *game_time,
                          void *context, CNA_CallbackError *out_error) {
  (void)out_error;
  ExtState *const state = (ExtState *)context;
  CNA_Handle graphics_device = CNA_INVALID_HANDLE;
  if (game_time != 0 ||
      cna_game_get_graphics_device(game, &graphics_device) !=
          CNA_RESULT_SUCCESS ||
      cna_graphics_ext_is_available(&state->available) != CNA_RESULT_SUCCESS) {
    return CNA_RESULT_INVALID_STATE;
  }

  CNA_Bool supported = CNA_FALSE;
  uint32_t color_space = 0;
  int32_t limit = 0;
  if (cna_graphics_device_executes_shader_effect_source_ext(
          graphics_device, &supported) != CNA_RESULT_SUCCESS ||
      cna_graphics_device_supports_image_based_lighting_ext(
          graphics_device, &supported) != CNA_RESULT_SUCCESS ||
      cna_graphics_device_supports_surface_format_as_render_target_ext(
          graphics_device, CNA_SURFACE_FORMAT_COLOR, &supported) !=
          CNA_RESULT_SUCCESS ||
      cna_graphics_device_get_display_color_space_ext(
          graphics_device, &color_space) != CNA_RESULT_SUCCESS ||
      !is_supported(cna_graphics_device_supports_display_color_space_ext(
          graphics_device, color_space, &supported)) ||
      !is_supported(cna_graphics_device_set_display_color_space_ext(
          graphics_device, color_space, &supported)) ||
      cna_graphics_device_get_max_compute_work_group_count_ext(
          graphics_device, 0, &limit) != CNA_RESULT_SUCCESS ||
      cna_graphics_device_get_max_compute_work_group_size_ext(
          graphics_device, 0, &limit) != CNA_RESULT_SUCCESS ||
      cna_graphics_device_get_max_compute_work_group_invocations_ext(
          graphics_device, &limit) != CNA_RESULT_SUCCESS ||
      !is_supported(cna_graphics_device_notify_content_lost_resources_ext(
          graphics_device))) {
    return CNA_RESULT_INVALID_STATE;
  }

  const int ok = state->available == CNA_TRUE
                     ? (validate_crt_effect(graphics_device) &&
                        validate_depth_effect(graphics_device) &&
                        validate_ascii_effect(graphics_device) &&
                        validate_debug_draw(graphics_device))
                     : validate_unavailable(graphics_device);
  if (!ok) {
    return CNA_RESULT_INVALID_STATE;
  }
  state->validated = 1;
  return CNA_RESULT_SUCCESS;
}

static int validate_effects(void) {
  ExtState state = {CNA_FALSE, 0};
  CNA_GameCallbacks callbacks = {
      sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
  static const char title[] = "C API graphics extensions";
  const CNA_GameCreateInfo create_info = {sizeof(CNA_GameCreateInfo),
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
  const int ran = cna_game_run_one_frame(game) == CNA_RESULT_SUCCESS &&
                  state.validated == 1;
  return cna_game_destroy(game) == CNA_RESULT_SUCCESS && ran;
}

int main(void) {
  CNA_IndirectDrawArguments draw = {0};
  CNA_IndirectDrawIndexedArguments indexed = {0};
  CNA_ImageBasedLightEXT light = {0};
  CNA_Bool valid = CNA_TRUE;
  uint64_t title_bytes = 0;
  const char name[] = "graphics extensions";
  const CNA_StringView title = {name, sizeof(name) - 1U};
  const CNA_StringView uniform_name = {"u_test", 6U};
  const float uniform_values[16] = {0};
  if (cna_indirect_draw_arguments_init(&draw) != CNA_RESULT_SUCCESS ||
      cna_indirect_draw_indexed_arguments_init(&indexed) !=
          CNA_RESULT_SUCCESS ||
      draw.vertex_count != 0U || indexed.index_count != 0U ||
      cna_image_based_light_ext_init(&light) != CNA_RESULT_SUCCESS ||
      cna_image_based_light_ext_is_valid(&light, &valid) !=
          CNA_RESULT_SUCCESS ||
      valid != CNA_FALSE ||
      cna_assembly_set_title_ext(title) != CNA_RESULT_SUCCESS ||
      cna_assembly_copy_title_ext(0, 0U, &title_bytes) !=
          CNA_RESULT_BUFFER_TOO_SMALL ||
      title_bytes != sizeof(name) - 1U ||
      cna_shader_effect_copy_compile_error_ext(CNA_INVALID_HANDLE, 0, 0U,
                                               &title_bytes) !=
          CNA_RESULT_INVALID_HANDLE ||
      cna_shader_effect_set_uniform_vec3_array(CNA_INVALID_HANDLE, uniform_name,
                                               uniform_values, 1) !=
          CNA_RESULT_INVALID_HANDLE ||
      cna_shader_effect_set_uniform_mat4_array(CNA_INVALID_HANDLE, uniform_name,
                                               uniform_values, 1) !=
          CNA_RESULT_INVALID_HANDLE) {
    return CNA_TEST_FAIL(2);
  }
  if (!validate_identities()) {
    return CNA_TEST_FAIL(1);
  }
  if (!validate_effects()) {
    return CNA_TEST_FAIL(3);
  }
  return 0;
}
