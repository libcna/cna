// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_EXT_H
#define CNA_C_GRAPHICS_EXT_H

#include "CNA/C/effects.h"
#include "CNA/C/graphics.h"
#include "CNA/C/math_values.h"
#include "CNA/C/vertex_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reports whether standalone CNA graphics extensions are enabled.
 *
 * @param out_available Receives `CNA_TRUE` when the extension layer is present.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The standalone retro effects and DebugDraw require the opt-in `CNA_CNAEXT` build option.
 * Core renderer and PBR value routes below work in either build.
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

/** @brief Owned handle for an ASCII post-process effect. */
typedef CNA_Handle CNA_AsciiPostProcessEffectHandle;

/**
 * @brief One image-based light: the three textures a PBR shader needs, and how bright they are.
 *
 * A caller fills this and hands it to an effect. The textures are **borrowed**; the structure
 * records them and never owns them.
 */
typedef struct CNA_ImageBasedLightEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief The irradiance cube, or `CNA_INVALID_HANDLE`. */
    CNA_Handle irradiance;
    /** @brief The prefiltered specular cube, or `CNA_INVALID_HANDLE`. */
    CNA_Handle prefiltered_specular;
    /** @brief The BRDF lookup texture, or `CNA_INVALID_HANDLE`. */
    CNA_Handle brdf_lut;
    /** @brief How many mip levels the prefiltered cube has; at least one. */
    int32_t prefiltered_mip_count;
    /** @brief Scalar multiplier on the light. */
    float intensity;
} CNA_ImageBasedLightEXT;

/**
 * @brief Fills an image-based light with its canonical defaults.
 *
 * @param out_light Receives the defaults along with `struct_size` and `struct_version`.
 * @return `CNA_RESULT_SUCCESS` in either build, or an argument/native error.
 */
CNA_C_API CNA_Result cna_image_based_light_ext_init(CNA_ImageBasedLightEXT* out_light);

/**
 * @brief Reports whether the light is complete enough to shade with.
 *
 * All three textures must be present and the mip count at least one. A light that is *nearly*
 * complete is the failure this answers: it does not look like a mismatch, it looks like a scene
 * lit slightly wrong.
 *
 * @param light The light.
 * @param out_valid Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * or another native error. This core PBR value route works in either build.
 */
CNA_C_API CNA_Result cna_image_based_light_ext_is_valid(
    const CNA_ImageBasedLightEXT* light, CNA_Bool* out_valid);


/**
 * @brief The arguments of an indirect draw, in the exact layout the GPU reads.
 *
 * **Sixteen bytes, four 32-bit words, and the layout is the contract** -- the GPU reads this
 * verbatim, so it is not a structure the ABI may version or pad. It therefore has no `struct_size`
 * or `struct_version`, unlike every other POD in this header, and a compile-time assertion pins its
 * size on both sides of the boundary.
 */
typedef struct CNA_IndirectDrawArguments {
    /** @brief How many vertices to fetch. */
    uint32_t vertex_count;
    /** @brief How many instances to draw; one for an ordinary draw, zero to draw nothing. */
    uint32_t instance_count;
    /** @brief The first vertex, in elements of the bound stream. */
    uint32_t first_vertex;
    /**
     * @brief The first instance.
     *
     * **Must be zero on GL ES.** ES 3.1 has no base-instance parameter and the word is required to
     * be zero; a non-zero value there is undefined rather than diagnosed, and cannot be checked
     * anywhere -- by the time the draw runs the value lives in GPU memory.
     */
    uint32_t base_instance;
} CNA_IndirectDrawArguments;

/**
 * @brief The arguments of an indexed indirect draw, in the exact layout the GPU reads.
 *
 * Same contract as @ref CNA_IndirectDrawArguments, one word longer: **twenty bytes, five words.**
 */
typedef struct CNA_IndirectDrawIndexedArguments {
    /** @brief How many indices to fetch. */
    uint32_t index_count;
    /** @brief How many instances to draw. */
    uint32_t instance_count;
    /** @brief The first index, in index elements. */
    uint32_t first_index;
    /** @brief Added to every decoded index, in vertex elements; signed, as the API is. */
    int32_t base_vertex;
    /** @brief The first instance; must be zero on GL ES, for the reason above. */
    uint32_t base_instance;
} CNA_IndirectDrawIndexedArguments;

/**
 * @brief Fills indirect draw arguments with the canonical defaults.
 *
 * Works in **every** build, because the canonical struct is part of the always-compiled graphics module and uses
 * the GPU's own command format and lives in the always-compiled graphics module.
 *
 * @param out_arguments Receives all-zero arguments, which draw nothing.
 * @return `CNA_RESULT_SUCCESS` in every build, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_indirect_draw_arguments_init(CNA_IndirectDrawArguments* out_arguments);

/**
 * @brief Fills indexed indirect draw arguments with the canonical defaults.
 *
 * @param out_arguments Receives all-zero arguments, which draw nothing.
 * @return `CNA_RESULT_SUCCESS` in every build, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_indirect_draw_indexed_arguments_init(
    CNA_IndirectDrawIndexedArguments* out_arguments);


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

/** @brief The fewest segments a debug sphere or cone is drawn with. */
#define CNA_DEBUG_DRAW_MIN_SEGMENTS 4

/** @brief The most segments a debug sphere or cone is drawn with. */
#define CNA_DEBUG_DRAW_MAX_SEGMENTS 128

/**
 * @brief Owned handle for the debug line renderer.
 *
 * Two line lists, drawn in two passes: the depth-tested one first, then the overlay, **so an
 * overlay line crossing a depth-tested one wins** -- which is the point of asking for an overlay.
 * @ref cna_debug_draw_set_depth_tested chooses which list the following lines go into.
 *
 * **@ref cna_debug_draw_begin resets that choice to depth-tested and clears both lists.** Setting
 * it before `begin` is therefore lost; set it after. Nothing here refuses -- there are no throws in
 * the canonical class at all -- so the contracts worth knowing are about *when* state is reset
 * rather than about what is rejected.
 */
typedef CNA_Handle CNA_DebugDrawHandle;

/**
 * @brief Creates a debug line renderer.
 *
 * @param graphics_device The device to draw with.
 * @param out_debug Receives the renderer; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid device or null
 * output, `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_create(
    CNA_Handle graphics_device, CNA_DebugDrawHandle* out_debug);

/**
 * @brief Releases a debug line renderer.
 *
 * @param debug The renderer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for an invalid handle,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_destroy(CNA_DebugDrawHandle debug);

/**
 * @brief Opens a frame, clearing both line lists and restoring depth testing.
 *
 * @param debug The renderer.
 * @param view The view matrix.
 * @param projection The projection matrix.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null matrix,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_begin(
    CNA_DebugDrawHandle debug, const CNA_Matrix* view, const CNA_Matrix* projection);

/**
 * @brief Draws both line lists and closes the frame.
 *
 * **Idempotent**: ending a frame that is not open does nothing and succeeds, so a caller does not
 * have to track whether it opened one. The device's depth-stencil state is restored afterwards.
 *
 * @param debug The renderer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_end(CNA_DebugDrawHandle debug);

/**
 * @brief Discards both line lists without drawing them.
 *
 * Leaves the frame open and the depth-test choice alone, unlike @ref cna_debug_draw_begin.
 *
 * @param debug The renderer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_clear(CNA_DebugDrawHandle debug);

/**
 * @brief Adds one line.
 *
 * @param debug The renderer.
 * @param from Where it starts.
 * @param to Where it ends.
 * @param colour Its colour.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_add_line(
    CNA_DebugDrawHandle debug,
    const CNA_Vector3* from,
    const CNA_Vector3* to,
    CNA_Color colour);

/**
 * @brief Adds the twelve edges of a box.
 *
 * @param debug The renderer.
 * @param bounds The box.
 * @param colour Its colour.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null box,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_add_box(
    CNA_DebugDrawHandle debug, const CNA_BoundingBox* bounds, CNA_Color colour);

/**
 * @brief Adds three rings approximating a sphere at a centre and radius.
 *
 * The segment count is **clamped to @ref CNA_DEBUG_DRAW_MIN_SEGMENTS through
 * @ref CNA_DEBUG_DRAW_MAX_SEGMENTS**, not refused: a debug shape drawn with too few or absurdly
 * many segments is still a debug shape, and refusing would turn a cosmetic argument into an error.
 *
 * @param debug The renderer.
 * @param centre The centre.
 * @param radius The radius.
 * @param colour Its colour.
 * @param segments Segments per ring; clamped.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null centre,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_add_sphere(
    CNA_DebugDrawHandle debug,
    const CNA_Vector3* centre,
    float radius,
    CNA_Color colour,
    int32_t segments);

/**
 * @brief Adds three rings approximating a bounding sphere.
 *
 * The same drawing as @ref cna_debug_draw_add_sphere, taking the shape as one value; bound
 * separately because C has no overloading.
 *
 * @param debug The renderer.
 * @param sphere The sphere.
 * @param colour Its colour.
 * @param segments Segments per ring; clamped.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null sphere,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_add_bounding_sphere(
    CNA_DebugDrawHandle debug,
    const CNA_BoundingSphere* sphere,
    CNA_Color colour,
    int32_t segments);

/**
 * @brief Adds the twelve edges of a frustum.
 *
 * @param debug The renderer.
 * @param frustum The frustum.
 * @param colour Its colour.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_add_frustum(
    CNA_DebugDrawHandle debug, CNA_BoundingFrustum frustum, CNA_Color colour);

/**
 * @brief Adds three axis-aligned segments crossing at a point.
 *
 * @param debug The renderer.
 * @param position Where they cross.
 * @param size Half the length of each segment.
 * @param colour Their colour.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null position,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_add_cross(
    CNA_DebugDrawHandle debug, const CNA_Vector3* position, float size, CNA_Color colour);

/**
 * @brief Reports which list the following lines go into.
 *
 * @param debug The renderer.
 * @param out_depth_tested Receives `CNA_TRUE` for the depth-tested list.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_is_depth_tested(
    CNA_DebugDrawHandle debug, CNA_Bool* out_depth_tested);

/**
 * @brief Chooses which list the following lines go into.
 *
 * **Reset to `CNA_TRUE` by @ref cna_debug_draw_begin**, so setting it before opening a frame is
 * lost. Lines already added stay in the list they were added to.
 *
 * @param debug The renderer.
 * @param depth_tested `CNA_TRUE` for the depth-tested list; a non-canonical byte is refused.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-canonical boolean,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_set_depth_tested(
    CNA_DebugDrawHandle debug, CNA_Bool depth_tested);

/**
 * @brief Returns how many lines are queued, across both lists.
 *
 * Lines, not vertices: two vertices make one line.
 *
 * @param debug The renderer.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_get_line_count(
    CNA_DebugDrawHandle debug, int32_t* out_count);

/**
 * @brief Copies one of the two vertex lists out, in submission order.
 *
 * @param debug The renderer.
 * @param depth_tested Which list to copy; a non-canonical byte is refused.
 * @param destination The array, or null to ask for the count.
 * @param capacity How many vertices it holds.
 * @param out_count Receives the number of vertices, which is twice the number of lines in that
 * list.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with the needed count in
 * `out_count`, `CNA_RESULT_INVALID_ARGUMENT` for a null count or a non-canonical boolean,
 * `CNA_RESULT_NOT_SUPPORTED` without CNA_CNAEXT, or an error.
 */
CNA_C_API CNA_Result cna_debug_draw_copy_vertices(
    CNA_DebugDrawHandle debug,
    CNA_Bool depth_tested,
    CNA_VertexPositionColor* destination,
    uint64_t capacity,
    uint64_t* out_count);


#ifdef __cplusplus
}
#endif

#endif
