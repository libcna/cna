// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_DEVICE_H
#define CNA_C_GRAPHICS_DEVICE_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fixed-width bit set selecting the buffers a device clear touches.
 *
 * The native declaration provides `|`, `&`, `~`, `|=` and `&=` operators over its scoped
 * enumeration. This fixed-width integer alias needs no adapter for them: C's own bitwise
 * operators apply directly to a `CNA_ClearOptions` value and produce the identical bit pattern.
 */
typedef uint32_t CNA_ClearOptions;

/** @brief Selects the color buffer. */
#define CNA_CLEAR_OPTION_TARGET UINT32_C(1)
/** @brief Selects the depth buffer. */
#define CNA_CLEAR_OPTION_DEPTH_BUFFER UINT32_C(2)
/** @brief Selects the stencil buffer. */
#define CNA_CLEAR_OPTION_STENCIL UINT32_C(4)

/** @brief Fixed-width identity describing the lifecycle status of a graphics device. */
typedef uint32_t CNA_GraphicsDeviceStatus;

/** @brief The device operates normally. */
#define CNA_GRAPHICS_DEVICE_STATUS_NORMAL UINT32_C(0)
/** @brief The device has been lost and cannot process commands. */
#define CNA_GRAPHICS_DEVICE_STATUS_LOST UINT32_C(1)
/** @brief The device has not been reset after being lost. */
#define CNA_GRAPHICS_DEVICE_STATUS_NOT_RESET UINT32_C(2)

/**
 * @brief Fixed-width identity of the policy a 2D-only renderer applies to unsupported 3D calls.
 */
typedef uint32_t CNA_Unsupported3DGraphicsCallBehavior;

/** @brief Preserves the renderer's established failure behavior. */
#define CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW UINT32_C(0)
/** @brief Logs one warning per operation and substitutes a safe no-op or null-object stub. */
#define CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB UINT32_C(1)

/**
 * @brief Describes the view bounds for the render-target surface.
 *
 * The six fields are the complete public property set of the canonical `Viewport`; reading and
 * writing them directly is the C mapping of its getters and setters.
 */
typedef struct CNA_Viewport {
    /** @brief X coordinate of the upper-left corner in pixels. */
    int32_t x;

    /** @brief Y coordinate of the upper-left corner in pixels. */
    int32_t y;

    /** @brief Width of the viewport in pixels. */
    int32_t width;

    /** @brief Height of the viewport in pixels. */
    int32_t height;

    /** @brief Minimum depth of the clip volume. */
    float min_depth;

    /** @brief Maximum depth of the clip volume. */
    float max_depth;
} CNA_Viewport;

/**
 * @brief Initializes a viewport exactly as the canonical default constructor does.
 *
 * @param out_value Receives a viewport with zero position and size, `min_depth` 0 and
 * `max_depth` 1.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_init(CNA_Viewport* out_value);

/**
 * @brief Initializes a viewport from a position and size.
 *
 * @param x X coordinate of the upper-left corner in pixels.
 * @param y Y coordinate of the upper-left corner in pixels.
 * @param width Width of the viewport in pixels.
 * @param height Height of the viewport in pixels.
 * @param out_value Receives the viewport with `min_depth` 0 and `max_depth` 1.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_init_bounds(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    CNA_Viewport* out_value);

/**
 * @brief Initializes a viewport from a rectangle.
 *
 * @param bounds Rectangle that defines the location and size of the viewport.
 * @param out_value Receives the viewport with `min_depth` 0 and `max_depth` 1.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_init_from_rectangle(
    CNA_Rectangle bounds,
    CNA_Viewport* out_value);

/**
 * @brief Gets the aspect ratio of a viewport.
 *
 * @param value Viewport to measure.
 * @param out_aspect_ratio Receives width divided by height, or zero when either is zero.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_aspect_ratio(
    CNA_Viewport value,
    float* out_aspect_ratio);

/**
 * @brief Gets the viewport area as a rectangle.
 *
 * @param value Viewport to convert.
 * @param out_bounds Receives the position and size of @p value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_bounds(
    CNA_Viewport value,
    CNA_Rectangle* out_bounds);

/**
 * @brief Sets the viewport position and size from a rectangle.
 *
 * @param viewport Viewport updated in place; depth range is preserved.
 * @param bounds Rectangle that defines the new viewport position and size.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null viewport.
 */
CNA_C_API CNA_Result cna_viewport_set_bounds(
    CNA_Viewport* viewport,
    CNA_Rectangle bounds);

/**
 * @brief Gets the subset of the viewport guaranteed to be visible on lower-quality displays.
 *
 * @param value Viewport to measure.
 * @param out_area Receives the title-safe rectangle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_title_safe_area(
    CNA_Viewport value,
    CNA_Rectangle* out_area);

/**
 * @brief Projects a world-space point into screen space.
 *
 * @param value Viewport that defines the screen rectangle and depth range.
 * @param source World-space point to project.
 * @param projection Projection matrix.
 * @param view View matrix.
 * @param world World matrix.
 * @param out_value Receives the projected screen-space point.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_project(
    CNA_Viewport value,
    CNA_Vector3 source,
    CNA_Matrix projection,
    CNA_Matrix view,
    CNA_Matrix world,
    CNA_Vector3* out_value);

/**
 * @brief Unprojects a screen-space point back into world space.
 *
 * @param value Viewport that defines the screen rectangle and depth range.
 * @param source Screen-space point to unproject.
 * @param projection Projection matrix.
 * @param view View matrix.
 * @param world World matrix.
 * @param out_value Receives the unprojected world-space point.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_unproject(
    CNA_Viewport value,
    CNA_Vector3 source,
    CNA_Matrix projection,
    CNA_Matrix view,
    CNA_Matrix world,
    CNA_Vector3* out_value);

/**
 * @brief Gets the UTF-8 byte count of the canonical viewport string.
 *
 * @param value Viewport to format.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_string_size(
    CNA_Viewport value,
    uint64_t* out_bytes);

/**
 * @brief Copies the canonical viewport string as UTF-8 bytes without a terminator.
 *
 * @param value Viewport to format.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or an argument error. No partial
 * string is written.
 */
CNA_C_API CNA_Result cna_viewport_copy_string(
    CNA_Viewport value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
