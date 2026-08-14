// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_RENDER_TARGET_H
#define CNA_C_RENDER_TARGET_H

#include "CNA/C/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fully qualified native type name represented by a 2D render-target handle. */
#define CNA_RENDER_TARGET_2D_TYPE_NAME "Microsoft.Xna.Framework.Graphics.RenderTarget2D"
/** @brief Fully qualified native type name represented by a cube render-target handle. */
#define CNA_RENDER_TARGET_CUBE_TYPE_NAME "Microsoft.Xna.Framework.Graphics.RenderTargetCube"

/** @brief Fixed-width depth/stencil format identity. */
typedef uint32_t CNA_DepthFormat;
/** @brief Creates no depth/stencil attachment. */
#define CNA_DEPTH_FORMAT_NONE UINT32_C(0)
/** @brief Creates a 16-bit depth attachment. */
#define CNA_DEPTH_FORMAT_DEPTH16 UINT32_C(1)
/** @brief Creates a 24-bit depth attachment. */
#define CNA_DEPTH_FORMAT_DEPTH24 UINT32_C(2)
/** @brief Creates a 24-bit depth plus 8-bit stencil attachment. */
#define CNA_DEPTH_FORMAT_DEPTH24_STENCIL8 UINT32_C(3)

/** @brief Fixed-width render-target content-usage identity. */
typedef uint32_t CNA_RenderTargetUsage;
/** @brief Clears/discards previous contents when binding. */
#define CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS UINT32_C(0)
/** @brief Preserves previous contents when binding. */
#define CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS UINT32_C(1)
/** @brief Preserves contents when the platform permits. */
#define CNA_RENDER_TARGET_USAGE_PLATFORM_CONTENTS UINT32_C(2)

/** @brief Fixed-width cube-map face identity. */
typedef uint32_t CNA_CubeMapFace;
/** @brief Positive X cube face. */
#define CNA_CUBE_MAP_FACE_POSITIVE_X UINT32_C(0)
/** @brief Negative X cube face. */
#define CNA_CUBE_MAP_FACE_NEGATIVE_X UINT32_C(1)
/** @brief Positive Y cube face. */
#define CNA_CUBE_MAP_FACE_POSITIVE_Y UINT32_C(2)
/** @brief Negative Y cube face. */
#define CNA_CUBE_MAP_FACE_NEGATIVE_Y UINT32_C(3)
/** @brief Positive Z cube face. */
#define CNA_CUBE_MAP_FACE_POSITIVE_Z UINT32_C(4)
/** @brief Negative Z cube face. */
#define CNA_CUBE_MAP_FACE_NEGATIVE_Z UINT32_C(5)

/** @brief Fixed-width concrete render-target kind. */
typedef uint32_t CNA_RenderTargetKind;
/** @brief Two-dimensional render target. */
#define CNA_RENDER_TARGET_KIND_2D UINT32_C(1)
/** @brief Cube-map render target. */
#define CNA_RENDER_TARGET_KIND_CUBE UINT32_C(2)

/** @brief Configures an owned two-dimensional render target. */
typedef struct CNA_RenderTarget2DCreateInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Width in pixels; must be greater than zero. */
    uint32_t width;
    /** @brief Height in pixels; must be greater than zero. */
    uint32_t height;
    /** @brief Whether to allocate a complete mip chain. */
    CNA_Bool mip_map;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved0[3];
    /** @brief Requested color surface format. */
    CNA_SurfaceFormat format;
    /** @brief Requested depth/stencil format. */
    CNA_DepthFormat depth_format;
    /** @brief Requested multisample count; zero disables multisampling. */
    int32_t multi_sample_count;
    /** @brief Content preservation policy. */
    CNA_RenderTargetUsage usage;
    /** @brief Reserved for future use; must be zero. */
    uint32_t reserved1;
} CNA_RenderTarget2DCreateInfo;

/** @brief Configures an owned cube-map render target. */
typedef struct CNA_RenderTargetCubeCreateInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Width and height of every face; must be greater than zero. */
    uint32_t size;
    /** @brief Whether to allocate a complete mip chain. */
    CNA_Bool mip_map;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved[3];
    /** @brief Requested color surface format. */
    CNA_SurfaceFormat format;
    /** @brief Requested depth/stencil format. */
    CNA_DepthFormat depth_format;
    /** @brief Requested multisample count; zero disables multisampling. */
    int32_t multi_sample_count;
    /** @brief Content preservation policy. */
    CNA_RenderTargetUsage usage;
} CNA_RenderTargetCubeCreateInfo;

/** @brief Describes either concrete native render-target type. */
typedef struct CNA_RenderTargetInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief `CNA_RENDER_TARGET_KIND_2D` or `CNA_RENDER_TARGET_KIND_CUBE`. */
    CNA_RenderTargetKind kind;
    /** @brief Width in pixels. */
    uint32_t width;
    /** @brief Height in pixels. */
    uint32_t height;
    /** @brief Number of allocated mip levels. */
    uint32_t level_count;
    /** @brief Color surface format. */
    CNA_SurfaceFormat format;
    /** @brief Applied depth/stencil format. */
    CNA_DepthFormat depth_format;
    /** @brief Applied multisample count. */
    int32_t multi_sample_count;
    /** @brief Content preservation policy. */
    CNA_RenderTargetUsage usage;
    /** @brief Always false in current CNA; native ContentLost is never raised. */
    CNA_Bool is_content_lost;
    /** @brief Whether the active backend created real bindable render-target storage. */
    CNA_Bool renderer_available;
    /** @brief Reserved bytes; returned as zero. */
    uint8_t reserved[2];
} CNA_RenderTargetInfo;

/** @brief One render-target subresource binding in an MRT array. */
typedef struct CNA_RenderTargetBinding {
    /** @brief Size of this array element in bytes; version one requires exact current size. */
    uint32_t struct_size;
    /** @brief Version of this array element. */
    uint32_t struct_version;
    /** @brief Owned 2D or cube render-target handle. */
    CNA_Handle render_target;
    /** @brief Array slice for a 2D target; version one supports only zero. */
    int32_t array_slice;
    /** @brief Face for a cube target; ignored for a 2D target and must then be positive X. */
    CNA_CubeMapFace cube_map_face;
} CNA_RenderTargetBinding;

/**
 * @brief Returns whether a render-target usage preserves prior contents.
 *
 * @param usage One of the `CNA_RENDER_TARGET_USAGE_*` identities.
 * @param out_preserves Receives true for PreserveContents and PlatformContents.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_render_target_usage_preserves_contents(
    CNA_RenderTargetUsage usage,
    CNA_Bool* out_preserves);

/**
 * @brief Creates an owned game-child two-dimensional render target.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param create_info Versioned color/depth/multisample/usage configuration.
 * @param out_render_target Receives an owned render-target handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 *
 * CNA permits construction on a backend without real off-screen storage. In that case creation
 * succeeds, @ref CNA_RenderTargetInfo::renderer_available is false and binding reports
 * `CNA_RESULT_NOT_SUPPORTED`.
 */
CNA_C_API CNA_Result cna_render_target2d_create(
    CNA_Handle graphics_device,
    const CNA_RenderTarget2DCreateInfo* create_info,
    CNA_Handle* out_render_target);

/**
 * @brief Creates an owned game-child cube-map render target.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param create_info Versioned size/color/depth/multisample/usage configuration.
 * @param out_render_target Receives an owned render-target handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_render_target_cube_create(
    CNA_Handle graphics_device,
    const CNA_RenderTargetCubeCreateInfo* create_info,
    CNA_Handle* out_render_target);

/**
 * @brief Gets common interface and concrete render-target information.
 *
 * @param render_target Owned 2D or cube render-target handle.
 * @param out_info Caller-initialized versioned output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_render_target_get_info(
    CNA_Handle render_target,
    CNA_RenderTargetInfo* out_info);

/**
 * @brief Binds one two-dimensional target, or restores the backbuffer.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param render_target Owned 2D target, or `CNA_INVALID_HANDLE` to restore the backbuffer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_render_target2d(
    CNA_Handle graphics_device,
    CNA_Handle render_target);

/**
 * @brief Binds one cube-map face, or restores the backbuffer.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param render_target Owned cube target, or `CNA_INVALID_HANDLE` to restore the backbuffer.
 * @param cube_map_face Face selected when @p render_target is valid.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_render_target_cube(
    CNA_Handle graphics_device,
    CNA_Handle render_target,
    CNA_CubeMapFace cube_map_face);

/**
 * @brief Replaces the complete active render-target binding array.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param bindings Caller-owned exact-stride array, or null only when @p binding_count is zero.
 * @param binding_count Number of bindings; zero restores the backbuffer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 *
 * All handles, dimensions, sample counts and subresources are validated before native binding.
 * Every target must belong to the same game as @p graphics_device.
 */
CNA_C_API CNA_Result cna_graphics_device_set_render_targets(
    CNA_Handle graphics_device,
    const CNA_RenderTargetBinding* bindings,
    uint64_t binding_count);

/**
 * @brief Gets the number of currently active render-target bindings.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_count Receives zero for the backbuffer or the current MRT count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_render_target_count(
    CNA_Handle graphics_device,
    uint64_t* out_count);

/**
 * @brief Copies the complete current render-target binding array.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param destination Caller-owned output array, or null only when @p capacity is zero.
 * @param capacity Capacity in binding elements.
 * @param out_count Receives the exact required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No partial
 * array is written.
 */
CNA_C_API CNA_Result cna_graphics_device_copy_render_targets(
    CNA_Handle graphics_device,
    CNA_RenderTargetBinding* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Disposes and releases an owned 2D or cube render target.
 *
 * @param render_target Owned render-target handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while bound or retained by an active
 * SpriteBatch, or another documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_render_target_destroy(CNA_Handle render_target);

#ifdef __cplusplus
}
#endif

#endif
