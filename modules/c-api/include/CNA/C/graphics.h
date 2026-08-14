// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_H
#define CNA_C_GRAPHICS_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity of the graphics renderer compiled into CNA. */
typedef uint32_t CNA_GraphicsRendererType;

/** @brief Indicates an unknown renderer identity. */
#define CNA_GRAPHICS_RENDERER_UNKNOWN UINT32_C(0)
/** @brief Identifies the SDL_Renderer backend. */
#define CNA_GRAPHICS_RENDERER_SDL_RENDERER UINT32_C(1)
/** @brief Identifies the OpenGL ES 2 backend. */
#define CNA_GRAPHICS_RENDERER_OPENGLES2 UINT32_C(2)
/** @brief Identifies the OpenGL ES 3 backend. */
#define CNA_GRAPHICS_RENDERER_OPENGLES3 UINT32_C(3)
/** @brief Identifies the desktop OpenGL 3.3 backend. */
#define CNA_GRAPHICS_RENDERER_OPENGL33 UINT32_C(4)
/** @brief Identifies the WebGL 1 backend. */
#define CNA_GRAPHICS_RENDERER_WEBGL1 UINT32_C(5)
/** @brief Identifies the WebGL 2 backend. */
#define CNA_GRAPHICS_RENDERER_WEBGL2 UINT32_C(6)
/** @brief Identifies the Bgfx backend. */
#define CNA_GRAPHICS_RENDERER_BGFX UINT32_C(7)
/** @brief Identifies the Vulkan backend. */
#define CNA_GRAPHICS_RENDERER_VULKAN UINT32_C(8)
/** @brief Identifies the WebGPU backend. */
#define CNA_GRAPHICS_RENDERER_WEBGPU UINT32_C(9)
/** @brief Identifies the Magnum backend. */
#define CNA_GRAPHICS_RENDERER_MAGNUM UINT32_C(10)
/** @brief Identifies the no-window HEADLESS backend. */
#define CNA_GRAPHICS_RENDERER_HEADLESS UINT32_C(11)
/** @brief Identifies the CPU SOFTWARE backend. */
#define CNA_GRAPHICS_RENDERER_SOFTWARE UINT32_C(12)
/** @brief Identifies the no-op STUB backend. */
#define CNA_GRAPHICS_RENDERER_STUB UINT32_C(13)
/** @brief Identifies the Direct3D 11 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX11 UINT32_C(14)
/** @brief Identifies the Direct3D 12 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX12 UINT32_C(15)
/** @brief Identifies the Direct2D backend. */
#define CNA_GRAPHICS_RENDERER_DIRECT2D UINT32_C(16)
/** @brief Identifies the HTML Canvas backend. */
#define CNA_GRAPHICS_RENDERER_CANVAS UINT32_C(17)
/** @brief Identifies the HTML DOM backend. */
#define CNA_GRAPHICS_RENDERER_HTML_DOM UINT32_C(18)
/** @brief Identifies the Skia backend. */
#define CNA_GRAPHICS_RENDERER_SKIA UINT32_C(19)
/** @brief Identifies the Blend2D backend. */
#define CNA_GRAPHICS_RENDERER_BLEND2D UINT32_C(20)
/** @brief Identifies the FreeDirect backend. */
#define CNA_GRAPHICS_RENDERER_FREEDIRECT UINT32_C(21)
/** @brief Identifies the Direct3D 9 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX9 UINT32_C(22)
/** @brief Identifies the DirectX 1 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX1 UINT32_C(23)
/** @brief Identifies the DirectX 2 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX2 UINT32_C(24)
/** @brief Identifies the DirectX 3 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX3 UINT32_C(25)
/** @brief Identifies the DirectX 5 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX5 UINT32_C(26)
/** @brief Identifies the DirectX 6 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX6 UINT32_C(27)
/** @brief Identifies the DirectX 7 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX7 UINT32_C(28)
/** @brief Identifies the DirectX 8 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX8 UINT32_C(29)
/** @brief Identifies the Direct3D 10 backend. */
#define CNA_GRAPHICS_RENDERER_DIRECTX10 UINT32_C(30)
/** @brief Identifies the SDL_GPU backend. */
#define CNA_GRAPHICS_RENDERER_SDL_GPU UINT32_C(31)
/** @brief Identifies the OpenGL ES 1 backend. */
#define CNA_GRAPHICS_RENDERER_OPENGLES1 UINT32_C(32)
/** @brief Identifies the desktop OpenGL 4 backend. */
#define CNA_GRAPHICS_RENDERER_OPENGL4 UINT32_C(33)
/** @brief Identifies the legacy desktop OpenGL 1 backend. */
#define CNA_GRAPHICS_RENDERER_OPENGL1 UINT32_C(34)
/** @brief Identifies the desktop OpenGL 2 backend. */
#define CNA_GRAPHICS_RENDERER_OPENGL2 UINT32_C(35)
/** @brief Identifies the Wicked Engine backend. */
#define CNA_GRAPHICS_RENDERER_WICKED UINT32_C(36)
/** @brief Identifies the sokol_gfx backend. */
#define CNA_GRAPHICS_RENDERER_SOKOL UINT32_C(37)
/** @brief Identifies the Diligent Engine backend. */
#define CNA_GRAPHICS_RENDERER_DILIGENT UINT32_C(38)
/** @brief Identifies the 3dfx Glide backend. */
#define CNA_GRAPHICS_RENDERER_GLIDE UINT32_C(39)
/** @brief Identifies the Win32 GDI backend. */
#define CNA_GRAPHICS_RENDERER_GDI UINT32_C(40)
/** @brief Identifies the LLGL backend. */
#define CNA_GRAPHICS_RENDERER_LLGL UINT32_C(41)
/** @brief Identifies the Apple Metal backend. */
#define CNA_GRAPHICS_RENDERER_METAL UINT32_C(42)
/** @brief Identifies the FNA3D backend. */
#define CNA_GRAPHICS_RENDERER_FNA3D UINT32_C(43)
/** @brief Identifies the SVG DOM backend. */
#define CNA_GRAPHICS_RENDERER_SVG_DOM UINT32_C(44)
/** @brief Identifies the OpenVG backend. */
#define CNA_GRAPHICS_RENDERER_OPENVG UINT32_C(45)
/** @brief Identifies the PortableGL backend. */
#define CNA_GRAPHICS_RENDERER_PORTABLEGL UINT32_C(46)

/** @brief Fixed-width identifier for a renderer-dependent graphics capability. */
typedef uint32_t CNA_GraphicsCapability;

/** @brief Indicates support for the complete 3D graphics pipeline. */
#define CNA_GRAPHICS_CAPABILITY_THREE_D UINT32_C(0)

/** @brief Indicates support for a complete depth/stencil attachment. */
#define CNA_GRAPHICS_CAPABILITY_DEPTH_STENCIL_BUFFER UINT32_C(1)

/** @brief Indicates support for multi-sample anti-aliasing. */
#define CNA_GRAPHICS_CAPABILITY_MULTI_SAMPLE_ANTI_ALIASING UINT32_C(2)

/** @brief Indicates support for more than one simultaneous render target. */
#define CNA_GRAPHICS_CAPABILITY_MULTIPLE_RENDER_TARGETS UINT32_C(3)

/** @brief Indicates support for anisotropic texture filtering. */
#define CNA_GRAPHICS_CAPABILITY_ANISOTROPIC_FILTERING UINT32_C(4)

/** @brief Indicates support for wire-frame rasterization. */
#define CNA_GRAPHICS_CAPABILITY_WIRE_FRAME UINT32_C(5)

/** @brief Indicates support for real occlusion queries. */
#define CNA_GRAPHICS_CAPABILITY_OCCLUSION_QUERY UINT32_C(6)

/** @brief Indicates support for custom effects in sprite batches. */
#define CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS UINT32_C(7)

/** @brief Indicates support for real three-dimensional texture storage. */
#define CNA_GRAPHICS_CAPABILITY_TEXTURE_3D UINT32_C(8)

/** @brief Indicates support for multiple vertex input streams. */
#define CNA_GRAPHICS_CAPABILITY_MULTI_STREAM_VERTEX_INPUT UINT32_C(9)

/** @brief Indicates support for hardware-instanced drawing. */
#define CNA_GRAPHICS_CAPABILITY_INSTANCING UINT32_C(10)

/** @brief Indicates support for a stencil plane independently of depth. */
#define CNA_GRAPHICS_CAPABILITY_STENCIL_BUFFER UINT32_C(11)

/** @brief Indicates faithful support for additive blending. */
#define CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING UINT32_C(12)

/** @brief Fixed-width bit set containing zero or more graphics capabilities. */
typedef uint64_t CNA_GraphicsCapabilityFlags;

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_THREE_D. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_THREE_D (UINT64_C(1) << 0)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_DEPTH_STENCIL_BUFFER. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_DEPTH_STENCIL_BUFFER (UINT64_C(1) << 1)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_MULTI_SAMPLE_ANTI_ALIASING. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_MULTI_SAMPLE_ANTI_ALIASING (UINT64_C(1) << 2)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_MULTIPLE_RENDER_TARGETS. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_MULTIPLE_RENDER_TARGETS (UINT64_C(1) << 3)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_ANISOTROPIC_FILTERING. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_ANISOTROPIC_FILTERING (UINT64_C(1) << 4)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_WIRE_FRAME. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_WIRE_FRAME (UINT64_C(1) << 5)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_OCCLUSION_QUERY. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_OCCLUSION_QUERY (UINT64_C(1) << 6)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_CUSTOM_EFFECTS (UINT64_C(1) << 7)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_TEXTURE_3D. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_TEXTURE_3D (UINT64_C(1) << 8)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_MULTI_STREAM_VERTEX_INPUT. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_MULTI_STREAM_VERTEX_INPUT (UINT64_C(1) << 9)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_INSTANCING. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_INSTANCING (UINT64_C(1) << 10)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_STENCIL_BUFFER. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_STENCIL_BUFFER (UINT64_C(1) << 11)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_ADDITIVE_BLENDING (UINT64_C(1) << 12)

/**
 * @brief Describes the active CNA graphics renderer and its current device capabilities.
 */
typedef struct CNA_RendererInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief UTF-8 byte count of the active renderer name without a terminator. */
    uint64_t renderer_name_byte_length;

    /** @brief Bit set of capabilities reported by the active native graphics device. */
    CNA_GraphicsCapabilityFlags capability_flags;

    /** @brief Stable identity of the graphics renderer compiled into CNA. */
    CNA_GraphicsRendererType renderer_type;

    /** @brief Maximum supported width or height of a two-dimensional texture in pixels. */
    uint32_t max_texture_dimension;
} CNA_RendererInfo;

/**
 * @brief Borrows the active graphics device during a game lifecycle callback.
 *
 * @param game Callback-borrowed game handle received by the active lifecycle callback.
 * @param out_graphics_device Receives a borrowed graphics-device handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` outside a lifecycle callback, or a
 * documented argument/handle/thread/native failure.
 *
 * The returned handle is valid only until the current callback returns. It must not be retained or
 * released by the caller.
 */
CNA_C_API CNA_Result cna_game_get_graphics_device(
    CNA_Handle game,
    CNA_Handle* out_graphics_device);

/**
 * @brief Gets renderer identity and capability information from a borrowed graphics device.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_info Caller-provided versioned structure to receive renderer information.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_renderer_info(
    CNA_Handle graphics_device,
    CNA_RendererInfo* out_info);

/**
 * @brief Gets the UTF-8 byte count of the active renderer name.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_bytes Receives the exact renderer-name byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_renderer_name_size(
    CNA_Handle graphics_device,
    uint64_t* out_bytes);

/**
 * @brief Copies the active renderer name as UTF-8 bytes without a terminator.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param destination Caller-owned destination bytes, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination in bytes.
 * @param out_bytes Receives the required renderer-name byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread/native failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_graphics_device_copy_renderer_name(
    CNA_Handle graphics_device,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Queries one capability from the active native graphics device.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param capability One of the `CNA_GRAPHICS_CAPABILITY_*` identifiers.
 * @param out_supported Receives `CNA_TRUE` when supported and `CNA_FALSE` otherwise.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A recognized but unavailable capability is a successful query that writes `CNA_FALSE`.
 * Operations requiring that capability return `CNA_RESULT_NOT_SUPPORTED` rather than silently
 * substituting another behavior.
 */
CNA_C_API CNA_Result cna_graphics_device_supports_capability(
    CNA_Handle graphics_device,
    CNA_GraphicsCapability capability,
    CNA_Bool* out_supported);

#ifdef __cplusplus
}
#endif

#endif
