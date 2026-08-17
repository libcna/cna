// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_EXT_H
#define CNA_C_GRAPHICS_EXT_H

#include "CNA/C/effects.h"
#include "CNA/C/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reports whether this build contains CNA's extended graphics layer.
 *
 * @param out_available Receives `CNA_TRUE` when the extension layer is present.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The extended layer is an opt-in CNA build option. Its C declarations exist in every build so
 * that the exported ABI never changes shape, but the routes that need a native extension object
 * return `CNA_RESULT_NOT_SUPPORTED` when the layer is absent. The pure value operations below —
 * identities, `CNA_PbrMaterial` and `CNA_RenderPipelineSettings` — work in either build.
 */
CNA_C_API CNA_Result cna_graphics_ext_is_available(CNA_Bool* out_available);

/** @brief Fixed-width identity of an ASCII post-process quantization mode. */
typedef uint32_t CNA_AsciiQuantizeMode;

/** @brief Luminance-ranked glyphs with a fixed white foreground and no background fill. */
#define CNA_ASCII_QUANTIZE_MODE_BLACK_WHITE UINT32_C(0)
/** @brief Luminance-ranked glyphs tinted by the cell's own averaged color. */
#define CNA_ASCII_QUANTIZE_MODE_COLOR UINT32_C(1)

/** @brief Fixed-width identity of a CRT sub-pixel mask pattern. */
typedef uint32_t CNA_CRTMaskType;

/** @brief No sub-pixel mask. */
#define CNA_CRT_MASK_TYPE_NONE UINT32_C(0)
/** @brief Vertical RGB stripe pattern. */
#define CNA_CRT_MASK_TYPE_APERTURE_GRILLE UINT32_C(1)
/** @brief Row-offset RGB dot pattern. */
#define CNA_CRT_MASK_TYPE_SHADOW_MASK UINT32_C(2)

/** @brief Fixed-width identity of an ordered-dithering pattern. */
typedef uint32_t CNA_DitherMode;

/** @brief No dithering. */
#define CNA_DITHER_MODE_NONE UINT32_C(0)
/** @brief 4x4 ordered Bayer dithering. */
#define CNA_DITHER_MODE_BAYER_4X4 UINT32_C(1)
/** @brief 8x8 ordered Bayer dithering. */
#define CNA_DITHER_MODE_BAYER_8X8 UINT32_C(2)

/** @brief Fixed-width identity of an overall render-quality preset. */
typedef uint32_t CNA_RenderQuality;

/** @brief Minimum quality. */
#define CNA_RENDER_QUALITY_LOW UINT32_C(0)
/** @brief Balanced quality and performance. */
#define CNA_RENDER_QUALITY_MEDIUM UINT32_C(1)
/** @brief High quality with minor performance cost. */
#define CNA_RENDER_QUALITY_HIGH UINT32_C(2)
/** @brief Maximum quality regardless of cost. */
#define CNA_RENDER_QUALITY_ULTRA UINT32_C(3)

/** @brief Fixed-width identity of a shadow-map quality preset. */
typedef uint32_t CNA_ShadowQuality;

/** @brief Shadows disabled. */
#define CNA_SHADOW_QUALITY_DISABLED UINT32_C(0)
/** @brief 512x512 shadow map with no filtering. */
#define CNA_SHADOW_QUALITY_LOW UINT32_C(1)
/** @brief 1024x1024 shadow map with 2x2 percentage-closer filtering. */
#define CNA_SHADOW_QUALITY_MEDIUM UINT32_C(2)
/** @brief 2048x2048 shadow map with 3x3 percentage-closer filtering. */
#define CNA_SHADOW_QUALITY_HIGH UINT32_C(3)
/** @brief 4096x4096 shadow map with 5x5 percentage-closer filtering. */
#define CNA_SHADOW_QUALITY_ULTRA UINT32_C(4)

/** @brief Fixed-width identity of a tonemapping operator. */
typedef uint32_t CNA_TonemappingMode;

/** @brief No tonemapping; values are clamped. */
#define CNA_TONEMAPPING_MODE_NONE UINT32_C(0)
/** @brief Reinhard tonemapping. */
#define CNA_TONEMAPPING_MODE_REINHARD UINT32_C(1)
/** @brief Filmic tonemapping. */
#define CNA_TONEMAPPING_MODE_FILMIC UINT32_C(2)
/** @brief ACES filmic tonemapping. */
#define CNA_TONEMAPPING_MODE_ACES UINT32_C(3)

/** @brief Fixed-width identity of a target color depth for the depth-reduction effect. */
typedef uint32_t CNA_DepthEffectMode;

/** @brief 16-bit RGB565 color. */
#define CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT UINT32_C(0)
/** @brief 8-bit RGB332 color. */
#define CNA_DEPTH_EFFECT_MODE_COLOR_8_BIT UINT32_C(1)
/** @brief Four-bit greyscale. */
#define CNA_DEPTH_EFFECT_MODE_GRAYSCALE_4_BIT UINT32_C(2)
/** @brief Two-bit greyscale. */
#define CNA_DEPTH_EFFECT_MODE_GRAYSCALE_2_BIT UINT32_C(3)
/** @brief One-bit greyscale. */
#define CNA_DEPTH_EFFECT_MODE_GRAYSCALE_1_BIT UINT32_C(4)
/** @brief Nearest match against the fixed 216-color web-safe palette. */
#define CNA_DEPTH_EFFECT_MODE_PALETTE_256 UINT32_C(5)
/** @brief Nearest match against the classic 16-color EGA/CGA palette. */
#define CNA_DEPTH_EFFECT_MODE_PALETTE_16 UINT32_C(6)

/**
 * @brief Physically based material definition following the glTF 2.0 metallic-roughness model.
 *
 * The canonical type is a settings bag whose accessors assign without clamping, so this fixed
 * layout is the whole of it: a C caller reads and writes the fields directly, exactly as the
 * canonical getters and setters do. Texture slots are non-owning: a handle stored here does not
 * keep its texture alive, matching the canonical non-owning `Texture2D*` slots.
 */
typedef struct CNA_PbrMaterial {
    /** @brief Albedo (base color) texture handle, or `CNA_INVALID_HANDLE`. */
    CNA_Handle albedo_texture;

    /** @brief Tangent-space normal map handle, or `CNA_INVALID_HANDLE`. */
    CNA_Handle normal_texture;

    /** @brief Metallic-roughness texture handle (B = metallic, G = roughness). */
    CNA_Handle metallic_roughness_texture;

    /** @brief Ambient-occlusion texture handle (R channel), or `CNA_INVALID_HANDLE`. */
    CNA_Handle ambient_occlusion_texture;

    /** @brief Emissive texture handle, or `CNA_INVALID_HANDLE`. */
    CNA_Handle emissive_texture;

    /** @brief Albedo color factor multiplied with the albedo texture. */
    CNA_Color albedo_color;

    /** @brief Emissive color factor. */
    CNA_Color emissive_color;

    /** @brief Metallic factor; the canonical range is 0 through 1. */
    float metallic_factor;

    /** @brief Roughness factor; the canonical range is 0 through 1. */
    float roughness_factor;

    /** @brief Normal-map intensity scale, where 1 is full strength. */
    float normal_scale;

    /** @brief Ambient-occlusion strength; the canonical range is 0 through 1. */
    float occlusion_strength;

    /** @brief Alpha cutoff threshold used by alpha-test rendering. */
    float alpha_cutoff;

    /** @brief `CNA_TRUE` to render this material with alpha blending. */
    CNA_Bool alpha_blend_enabled;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[3];
} CNA_PbrMaterial;

/**
 * @brief Configuration for CNA's extended render pipeline.
 *
 * Like @ref CNA_PbrMaterial this mirrors a canonical settings bag whose accessors assign without
 * clamping, so the fields are written directly.
 */
typedef struct CNA_RenderPipelineSettings {
    /** @brief Scene exposure multiplier applied when HDR is enabled. */
    float exposure;

    /** @brief Display gamma; the canonical default is 2.2. */
    float gamma;

    /** @brief Bloom intensity multiplier. */
    float bloom_intensity;

    /** @brief Active tonemapping operator. */
    CNA_TonemappingMode tonemapping_mode;

    /** @brief Overall render-quality preset. */
    CNA_RenderQuality render_quality;

    /** @brief Shadow-map quality preset. */
    CNA_ShadowQuality shadow_quality;

    /** @brief `CNA_TRUE` when HDR rendering is enabled. */
    CNA_Bool hdr_enabled;

    /** @brief `CNA_TRUE` when the bloom post-process pass is enabled. */
    CNA_Bool bloom_enabled;

    /** @brief `CNA_TRUE` when the SSAO post-process pass is enabled. */
    CNA_Bool ssao_enabled;

    /** @brief `CNA_TRUE` when shadow rendering is enabled. */
    CNA_Bool shadows_enabled;
} CNA_RenderPipelineSettings;

/** @brief Owned handle for an ASCII post-process effect. */
typedef CNA_Handle CNA_AsciiPostProcessEffectHandle;

/**
 * @brief Initializes a PBR material with the canonical defaults.
 *
 * @param out_material Receives white albedo, metallic 0, roughness 0.5, opaque black emissive,
 * unit normal scale and occlusion strength, no alpha blending, alpha cutoff 0.5 and no textures.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_pbr_material_init(CNA_PbrMaterial* out_material);

/**
 * @brief Initializes render-pipeline settings with the canonical defaults.
 *
 * @param out_settings Receives HDR off, exposure 1, gamma 2.2, no tonemapping, bloom off with
 * intensity 1, SSAO off, medium render quality, disabled shadow quality and shadows off.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_render_pipeline_settings_init(
    CNA_RenderPipelineSettings* out_settings);

/**
 * @brief Creates an owned CRT display-emulation effect.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_effect Receives an owned effect handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the extension layer or the
 * renderer's shader support is absent, or a documented argument/handle/thread/native failure.
 *
 * The result is an ordinary `CNA_EffectHandle`: every `cna_effect_*` operation — clone, dispose,
 * apply, type name, parameters — accepts it.
 */
CNA_C_API CNA_Result cna_crt_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/**
 * @brief Gets the CRT scanline darkening strength.
 *
 * @param effect Owned CRT effect handle.
 * @param out_value Receives the strength in the canonical range 0 through 1.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` when the effect is not a CRT effect,
 * or a documented argument/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_get_scanline_intensity(
    CNA_EffectHandle effect,
    float* out_value);

/**
 * @brief Sets the CRT scanline darkening strength.
 *
 * @param effect Owned CRT effect handle.
 * @param value Strength; the canonical implementation clamps it to 0 through 1.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-finite value,
 * `CNA_RESULT_INVALID_HANDLE` when the effect is not a CRT effect, or another documented failure.
 */
CNA_C_API CNA_Result cna_crt_effect_set_scanline_intensity(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Gets the CRT barrel-distortion curvature amount.
 *
 * @param effect Owned CRT effect handle.
 * @param out_value Receives the amount in the canonical range 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_get_curvature(CNA_EffectHandle effect, float* out_value);

/**
 * @brief Sets the CRT barrel-distortion curvature amount.
 *
 * @param effect Owned CRT effect handle.
 * @param value Amount; the canonical implementation clamps it to 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_set_curvature(CNA_EffectHandle effect, float value);

/**
 * @brief Gets the CRT corner vignette darkening strength.
 *
 * @param effect Owned CRT effect handle.
 * @param out_value Receives the strength in the canonical range 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_get_vignette_intensity(
    CNA_EffectHandle effect,
    float* out_value);

/**
 * @brief Sets the CRT corner vignette darkening strength.
 *
 * @param effect Owned CRT effect handle.
 * @param value Strength; the canonical implementation clamps it to 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_set_vignette_intensity(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Gets the CRT sub-pixel mask darkening strength.
 *
 * @param effect Owned CRT effect handle.
 * @param out_value Receives the strength in the canonical range 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_get_mask_intensity(
    CNA_EffectHandle effect,
    float* out_value);

/**
 * @brief Sets the CRT sub-pixel mask darkening strength.
 *
 * @param effect Owned CRT effect handle.
 * @param value Strength; the canonical implementation clamps it to 0 through 1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_set_mask_intensity(CNA_EffectHandle effect, float value);

/**
 * @brief Gets the active CRT sub-pixel mask pattern.
 *
 * @param effect Owned CRT effect handle.
 * @param out_mask_type Receives one of the `CNA_CRT_MASK_TYPE_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_get_mask_type(
    CNA_EffectHandle effect,
    CNA_CRTMaskType* out_mask_type);

/**
 * @brief Sets the CRT sub-pixel mask pattern.
 *
 * @param effect Owned CRT effect handle.
 * @param mask_type One of the `CNA_CRT_MASK_TYPE_*` identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_crt_effect_set_mask_type(
    CNA_EffectHandle effect,
    CNA_CRTMaskType mask_type);

/**
 * @brief Creates an owned color-depth-reduction effect.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_effect Receives an owned effect handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the extension layer or the
 * renderer's shader support is absent, or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_depth_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/**
 * @brief Gets the active color-depth mode.
 *
 * @param effect Owned depth effect handle.
 * @param out_mode Receives one of the `CNA_DEPTH_EFFECT_MODE_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_depth_effect_get_mode(
    CNA_EffectHandle effect,
    CNA_DepthEffectMode* out_mode);

/**
 * @brief Sets the color-depth mode applied on the next apply.
 *
 * @param effect Owned depth effect handle.
 * @param mode One of the `CNA_DEPTH_EFFECT_MODE_*` identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_depth_effect_set_mode(
    CNA_EffectHandle effect,
    CNA_DepthEffectMode mode);

/**
 * @brief Gets the active ordered-dithering pattern.
 *
 * @param effect Owned depth effect handle.
 * @param out_dither_mode Receives one of the `CNA_DITHER_MODE_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_depth_effect_get_dither_mode(
    CNA_EffectHandle effect,
    CNA_DitherMode* out_dither_mode);

/**
 * @brief Sets the ordered-dithering pattern applied on the next apply.
 *
 * @param effect Owned depth effect handle.
 * @param dither_mode One of the `CNA_DITHER_MODE_*` identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_depth_effect_set_dither_mode(
    CNA_EffectHandle effect,
    CNA_DitherMode dither_mode);

/**
 * @brief Creates an owned ASCII post-process effect.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_effect Receives an owned ASCII effect handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the extension layer is absent, or
 * a documented argument/handle/thread/native failure.
 *
 * This is not a shader `Effect` and is not accepted by the `cna_effect_*` routes: it performs its
 * own read-back, quantization and draw pass. It is a child of the active game and must be
 * destroyed before @ref cna_game_destroy.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_create(
    CNA_Handle graphics_device,
    CNA_AsciiPostProcessEffectHandle* out_effect);

/**
 * @brief Gets the source-pixel block size averaged into one glyph cell.
 *
 * @param effect Owned ASCII effect handle.
 * @param out_width Receives the cell width in source pixels.
 * @param out_height Receives the cell height in source pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_get_cell_size(
    CNA_AsciiPostProcessEffectHandle effect,
    int32_t* out_width,
    int32_t* out_height);

/**
 * @brief Sets the source-pixel block size averaged into one glyph cell.
 *
 * @param effect Owned ASCII effect handle.
 * @param width Cell width in source pixels; must be positive.
 * @param height Cell height in source pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive dimension, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_set_cell_size(
    CNA_AsciiPostProcessEffectHandle effect,
    int32_t width,
    int32_t height);

/**
 * @brief Gets the active quantization mode.
 *
 * @param effect Owned ASCII effect handle.
 * @param out_mode Receives one of the `CNA_ASCII_QUANTIZE_MODE_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_get_quantize_mode(
    CNA_AsciiPostProcessEffectHandle effect,
    CNA_AsciiQuantizeMode* out_mode);

/**
 * @brief Sets the quantization mode used by the next draw.
 *
 * @param effect Owned ASCII effect handle.
 * @param mode One of the `CNA_ASCII_QUANTIZE_MODE_*` identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_set_quantize_mode(
    CNA_AsciiPostProcessEffectHandle effect,
    CNA_AsciiQuantizeMode mode);

/**
 * @brief Quantizes a texture and draws the glyph grid into the current render target.
 *
 * @param effect Owned ASCII effect handle.
 * @param source Owned texture handle holding the already-rendered image.
 * @param destination_rectangle Destination in render-target pixels, or null to fill the viewport.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend cannot serve the
 * read-back or draw, or a documented argument/handle/thread/native failure.
 *
 * A null destination selects the canonical whole-viewport overload; a non-null one selects the
 * explicit-rectangle overload.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_draw(
    CNA_AsciiPostProcessEffectHandle effect,
    CNA_Handle source,
    const CNA_Rectangle* destination_rectangle);

/**
 * @brief Gets the glyph grid dimensions produced by the most recent draw.
 *
 * @param effect Owned ASCII effect handle.
 * @param out_columns Receives the column count, or zero before the first draw.
 * @param out_rows Receives the row count, or zero before the first draw.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_get_last_grid_dimensions(
    CNA_AsciiPostProcessEffectHandle effect,
    int32_t* out_columns,
    int32_t* out_rows);

/**
 * @brief Disposes and releases an owned ASCII post-process effect.
 *
 * @param effect Owned ASCII effect handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. A second destroy
 * returns `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_ascii_post_process_effect_destroy(
    CNA_AsciiPostProcessEffectHandle effect);

#ifdef __cplusplus
}
#endif

#endif
