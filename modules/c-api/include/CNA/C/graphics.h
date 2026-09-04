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
/** @brief Identifies SDL's own 2D renderer backend. */
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
/** @brief Identifies the 3dfx Glide backend. */
#define CNA_GRAPHICS_RENDERER_GLIDE UINT32_C(39)
/** @brief Identifies the Win32 GDI backend. */
#define CNA_GRAPHICS_RENDERER_GDI UINT32_C(40)
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
/** @brief Identifies the TinyGL backend. */
#define CNA_GRAPHICS_RENDERER_TINYGL UINT32_C(47)
/** @brief Identifies the IGL backend. */
#define CNA_GRAPHICS_RENDERER_IGL UINT32_C(48)
/** @brief Identifies the PixiJS backend. */
#define CNA_GRAPHICS_RENDERER_PIXIJS UINT32_C(49)
/** @brief Identifies the NanoVG backend. */
#define CNA_GRAPHICS_RENDERER_NANOVG UINT32_C(50)

/**
 * @brief Largest defined renderer identity.
 *
 * A removed renderer's numeric value is retired, not reused, so the range from
 * @ref CNA_GRAPHICS_RENDERER_UNKNOWN through this value may contain gaps where a former
 * identity used to be; a caller cannot assume every value in the range names a live backend.
 * Every value above @ref CNA_GRAPHICS_RENDERER_MAXIMUM is refused by every route that takes a
 * @ref CNA_GraphicsRendererType, as is any retired value within the range.
 */
#define CNA_GRAPHICS_RENDERER_MAXIMUM CNA_GRAPHICS_RENDERER_NANOVG

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

/**
 * @brief Indicates support for custom effects in sprite batches.
 *
 * This gates `cna_shader_effect_create` -- an effect built from shader **source**. It is a separate
 * capability from `CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS`, which gates a compiled `.xnb` effect
 * asset, and the two genuinely differ: the software renderer reports this one true and that one
 * false. Deciding whether a game can supply its own shaders by testing the compiled capability
 * reports it blocked when it is not.
 *
 * True promises the route works and returns an effect. It does not promise the renderer validates
 * the source; see `cna_shader_effect_create` and `cna_shader_effect_is_valid` for what a caller can
 * conclude afterwards.
 */
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

/**
 * @brief Indicates support for XNA/FNA Direct3D 9 Effect Framework bytecode.
 *
 * Separate from @ref CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS, which describes the
 * caller-supplied shader-source contract: a renderer may support either format independently.
 */
#define CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS UINT32_C(13)

/** @brief Indicates support for 32-bit floating-point colour render targets. */
#define CNA_GRAPHICS_CAPABILITY_FLOAT_RENDER_TARGETS UINT32_C(14)

/** @brief Indicates support for 16-bit floating-point colour render targets. */
#define CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_RENDER_TARGETS UINT32_C(15)

/** @brief Indicates support for linearly filtering half-float textures. */
#define CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_TEXTURE_LINEAR_FILTERING UINT32_C(16)

/** @brief Indicates support for compute shaders and storage buffers. */
#define CNA_GRAPHICS_CAPABILITY_COMPUTE_SHADERS UINT32_C(17)

/** @brief Indicates support for GPU-buffer-driven indirect draws. */
#define CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW UINT32_C(18)

/**
 * @brief Largest defined graphics capability identity.
 *
 * The capability identities occupy the closed range
 * @ref CNA_GRAPHICS_CAPABILITY_THREE_D through this value with no gaps, so a caller can query
 * every capability without naming each one. Every value above it is refused.
 */
#define CNA_GRAPHICS_CAPABILITY_MAXIMUM CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW

/** @brief Fixed-width identity of the shading dialect a custom effect's sources must use. */
typedef uint32_t CNA_ShaderDialect;

/** @brief The active renderer has not declared a dialect; do not guess one. */
#define CNA_SHADER_DIALECT_UNKNOWN UINT32_C(0)
/** @brief Desktop OpenGL GLSL (`#version 3xx core` / `4xx core`). */
#define CNA_SHADER_DIALECT_GLSL_DESKTOP UINT32_C(1)
/** @brief OpenGL ES / WebGL GLSL (`#version 100` / `300 es`). */
#define CNA_SHADER_DIALECT_GLSL_ES UINT32_C(2)
/** @brief GLSL compiled to SPIR-V, where `location`/`set`/`binding` are mandatory. */
#define CNA_SHADER_DIALECT_GLSL_VULKAN UINT32_C(3)
/** @brief Direct3D High Level Shader Language. */
#define CNA_SHADER_DIALECT_HLSL UINT32_C(4)
/** @brief Metal Shading Language. */
#define CNA_SHADER_DIALECT_MSL UINT32_C(5)
/** @brief WebGPU Shading Language. */
#define CNA_SHADER_DIALECT_WGSL UINT32_C(6)
/**
 * @brief Largest defined shading-dialect identity.
 *
 * The identities occupy the closed range @ref CNA_SHADER_DIALECT_UNKNOWN through this value with
 * no gaps, so a caller can enumerate them without naming each one.
 */
#define CNA_SHADER_DIALECT_MAXIMUM CNA_SHADER_DIALECT_WGSL

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

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_COMPILED_EFFECTS (UINT64_C(1) << 13)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_FLOAT_RENDER_TARGETS. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_FLOAT_RENDER_TARGETS (UINT64_C(1) << 14)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_RENDER_TARGETS. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_HALF_FLOAT_RENDER_TARGETS (UINT64_C(1) << 15)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_TEXTURE_LINEAR_FILTERING. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_HALF_FLOAT_TEXTURE_LINEAR_FILTERING (UINT64_C(1) << 16)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_COMPUTE_SHADERS. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_COMPUTE_SHADERS (UINT64_C(1) << 17)

/** @brief Bit corresponding to @ref CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW. */
#define CNA_GRAPHICS_CAPABILITY_FLAG_INDIRECT_DRAW (UINT64_C(1) << 18)

/**
 * @brief Fixed-width identity of one atomic renderer feature.
 *
 * This append-only identity space is intentionally separate from the legacy 64-bit
 * @ref CNA_GraphicsCapabilityFlags summary. Callers can enumerate the closed range through
 * @ref CNA_RENDERER_FEATURE_MAXIMUM and receive a four-state answer for each entry.
 */
typedef uint32_t CNA_RendererFeature;

/** @brief Complete XNA-style 3D vertex/index drawing pipeline. */
#define CNA_RENDERER_FEATURE_THREE_DIMENSIONAL_PIPELINE UINT32_C(0)
/** @brief Complete depth/stencil attachment usable by graphics draws. */
#define CNA_RENDERER_FEATURE_DEPTH_STENCIL_BUFFER UINT32_C(1)
/** @brief At least one multisample anti-aliasing mode above one sample. */
#define CNA_RENDERER_FEATURE_MULTI_SAMPLE_ANTI_ALIASING UINT32_C(2)
/** @brief More than one simultaneous colour render target. */
#define CNA_RENDERER_FEATURE_MULTIPLE_RENDER_TARGETS UINT32_C(3)
/** @brief Anisotropic texture filtering with an observable result. */
#define CNA_RENDERER_FEATURE_ANISOTROPIC_FILTERING UINT32_C(4)
/** @brief Wire-frame rasterization, native or exactly emulated. */
#define CNA_RENDERER_FEATURE_WIRE_FRAME_RASTERIZATION UINT32_C(5)
/** @brief Occlusion-query begin/end/result operations. */
#define CNA_RENDERER_FEATURE_OCCLUSION_QUERIES UINT32_C(6)
/** @brief Creation and use of source-based shader-effect objects. */
#define CNA_RENDERER_FEATURE_SHADER_EFFECTS UINT32_C(7)
/** @brief Supplied shader-effect source determines rendered pixels. */
#define CNA_RENDERER_FEATURE_SHADER_EFFECT_SOURCE_EXECUTION UINT32_C(8)
/** @brief Persistent three-dimensional texture upload/readback storage. */
#define CNA_RENDERER_FEATURE_TEXTURE_3D_STORAGE UINT32_C(9)
/** @brief Multiple same-rate vertex streams in one draw. */
#define CNA_RENDERER_FEATURE_MULTI_STREAM_VERTEX_INPUT UINT32_C(10)
/** @brief Instanced graphics drawing. */
#define CNA_RENDERER_FEATURE_INSTANCED_DRAWING UINT32_C(11)
/** @brief A stencil plane usable independently of depth. */
#define CNA_RENDERER_FEATURE_STENCIL_BUFFER UINT32_C(12)
/** @brief Faithful additive blending. */
#define CNA_RENDERER_FEATURE_ADDITIVE_BLENDING UINT32_C(13)
/** @brief XNA/FNA Direct3D 9 Effect Framework bytecode execution. */
#define CNA_RENDERER_FEATURE_COMPILED_XNA_EFFECTS UINT32_C(14)
/** @brief Faithful 32-bit-per-channel floating-point render targets. */
#define CNA_RENDERER_FEATURE_FLOAT32_RENDER_TARGETS UINT32_C(15)
/** @brief Faithful 16-bit-per-channel floating-point render targets. */
#define CNA_RENDERER_FEATURE_FLOAT16_RENDER_TARGETS UINT32_C(16)
/** @brief Linear or mip filtering of half-float colour textures. */
#define CNA_RENDERER_FEATURE_FLOAT16_TEXTURE_LINEAR_FILTERING UINT32_C(17)
/** @brief Compute shaders together with the storage-buffer path. */
#define CNA_RENDERER_FEATURE_COMPUTE_SHADERS UINT32_C(18)
/** @brief Binding a two-dimensional texture as a compute image. */
#define CNA_RENDERER_FEATURE_COMPUTE_IMAGE_BINDING UINT32_C(19)
/** @brief GPU-buffer-driven indirect graphics drawing. */
#define CNA_RENDERER_FEATURE_INDIRECT_DRAWING UINT32_C(20)
/** @brief Lit stock/PBR shaders sample configured shadow state. */
#define CNA_RENDERER_FEATURE_SHADOW_SAMPLING UINT32_C(21)
/** @brief PBR shaders consume configured image-based-lighting resources. */
#define CNA_RENDERER_FEATURE_IMAGE_BASED_LIGHTING UINT32_C(22)
/** @brief GPU timestamp queries measure a submitted command range. */
#define CNA_RENDERER_FEATURE_GPU_TIMERS UINT32_C(23)
/** @brief Source-based effects consume desktop OpenGL GLSL. */
#define CNA_RENDERER_FEATURE_SHADER_DIALECT_GLSL_DESKTOP UINT32_C(24)
/** @brief Source-based effects consume OpenGL ES/WebGL GLSL. */
#define CNA_RENDERER_FEATURE_SHADER_DIALECT_GLSL_ES UINT32_C(25)
/** @brief Source-based effects consume Vulkan-oriented GLSL. */
#define CNA_RENDERER_FEATURE_SHADER_DIALECT_GLSL_VULKAN UINT32_C(26)
/** @brief Source-based effects consume HLSL. */
#define CNA_RENDERER_FEATURE_SHADER_DIALECT_HLSL UINT32_C(27)
/** @brief Source-based effects consume Metal Shading Language. */
#define CNA_RENDERER_FEATURE_SHADER_DIALECT_MSL UINT32_C(28)
/** @brief Source-based effects consume WebGPU Shading Language. */
#define CNA_RENDERER_FEATURE_SHADER_DIALECT_WGSL UINT32_C(29)
/** @brief Largest currently defined detailed renderer-feature identity. */
#define CNA_RENDERER_FEATURE_MAXIMUM CNA_RENDERER_FEATURE_SHADER_DIALECT_WGSL

/** @brief Fixed-width classified answer for one detailed renderer feature. */
typedef uint32_t CNA_RendererFeatureSupport;

/** @brief The feature has not yet been audited or runtime-probed. */
#define CNA_RENDERER_FEATURE_SUPPORT_UNKNOWN UINT32_C(0)
/** @brief The complete feature contract is unavailable. */
#define CNA_RENDERER_FEATURE_SUPPORT_UNSUPPORTED UINT32_C(1)
/** @brief The complete documented feature contract is available. */
#define CNA_RENDERER_FEATURE_SUPPORT_SUPPORTED UINT32_C(2)
/** @brief Only an explicitly documented subset of the contract is available. */
#define CNA_RENDERER_FEATURE_SUPPORT_RESTRICTED UINT32_C(3)
/** @brief Largest defined detailed renderer-feature support identity. */
#define CNA_RENDERER_FEATURE_SUPPORT_MAXIMUM CNA_RENDERER_FEATURE_SUPPORT_RESTRICTED

/** @brief Fixed-width identity of one numeric renderer/device limit. */
typedef uint32_t CNA_RendererLimit;

/** @brief Maximum width or height of a two-dimensional texture. */
#define CNA_RENDERER_LIMIT_MAX_TEXTURE_DIMENSION UINT32_C(0)
/** @brief Maximum same-rate vertex streams in one draw. */
#define CNA_RENDERER_LIMIT_MAX_VERTEX_STREAMS UINT32_C(1)
/** @brief Maximum compute work-group count on the X axis. */
#define CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_COUNT_X UINT32_C(2)
/** @brief Maximum compute work-group count on the Y axis. */
#define CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_COUNT_Y UINT32_C(3)
/** @brief Maximum compute work-group count on the Z axis. */
#define CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_COUNT_Z UINT32_C(4)
/** @brief Maximum compute local size on the X axis. */
#define CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_SIZE_X UINT32_C(5)
/** @brief Maximum compute local size on the Y axis. */
#define CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_SIZE_Y UINT32_C(6)
/** @brief Maximum compute local size on the Z axis. */
#define CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_SIZE_Z UINT32_C(7)
/** @brief Maximum product of all compute local sizes. */
#define CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_INVOCATIONS UINT32_C(8)
/** @brief Maximum storage-buffer bindings readable by a vertex shader. */
#define CNA_RENDERER_LIMIT_MAX_VERTEX_SHADER_STORAGE_BLOCKS UINT32_C(9)
/** @brief Largest currently defined numeric renderer-limit identity. */
#define CNA_RENDERER_LIMIT_MAXIMUM CNA_RENDERER_LIMIT_MAX_VERTEX_SHADER_STORAGE_BLOCKS

/** @brief Fixed-width usage masks for per-surface-format support. */
typedef uint32_t CNA_RendererFormatUsageFlags;

/** @brief Faithful two-dimensional texture storage. */
#define CNA_RENDERER_FORMAT_USAGE_TEXTURE_STORAGE (UINT32_C(1) << 0)
/** @brief Sampling in a graphics shader. */
#define CNA_RENDERER_FORMAT_USAGE_SAMPLED (UINT32_C(1) << 1)
/** @brief Linear or mip filtering while sampling. */
#define CNA_RENDERER_FORMAT_USAGE_FILTERABLE (UINT32_C(1) << 2)
/** @brief Creation and binding as a render target. */
#define CNA_RENDERER_FORMAT_USAGE_RENDER_TARGET (UINT32_C(1) << 3)
/** @brief Blending graphics output into the format. */
#define CNA_RENDERER_FORMAT_USAGE_BLENDABLE (UINT32_C(1) << 4)
/** @brief Reading through a compute/storage-image binding. */
#define CNA_RENDERER_FORMAT_USAGE_STORAGE_READ (UINT32_C(1) << 5)
/** @brief Writing through a compute/storage-image binding. */
#define CNA_RENDERER_FORMAT_USAGE_STORAGE_WRITE (UINT32_C(1) << 6)
/** @brief Storage-image atomic operations. */
#define CNA_RENDERER_FORMAT_USAGE_STORAGE_ATOMIC (UINT32_C(1) << 7)
/** @brief Copying the format out of a resource. */
#define CNA_RENDERER_FORMAT_USAGE_TRANSFER_SOURCE (UINT32_C(1) << 8)
/** @brief Copying data into a resource of the format. */
#define CNA_RENDERER_FORMAT_USAGE_TRANSFER_DESTINATION (UINT32_C(1) << 9)
/** @brief Owning or generating more than one mip level. */
#define CNA_RENDERER_FORMAT_USAGE_MIPMAPPED (UINT32_C(1) << 10)
/** @brief Creating a multisampled image. */
#define CNA_RENDERER_FORMAT_USAGE_MULTISAMPLE (UINT32_C(1) << 11)
/** @brief Transferring through a colour-shaped element. */
#define CNA_RENDERER_FORMAT_USAGE_COLOR_TRANSFER (UINT32_C(1) << 12)
/** @brief Mask containing every currently defined renderer-format usage bit. */
#define CNA_RENDERER_FORMAT_USAGE_ALL ((UINT32_C(1) << 13) - UINT32_C(1))

/** @brief Fixed-width surface-format identity used by texture APIs. */
typedef uint32_t CNA_SurfaceFormat;

/** @brief Unsigned 32-bit RGBA format with eight bits per channel. */
#define CNA_SURFACE_FORMAT_COLOR UINT32_C(0)
/** @brief Unsigned 16-bit BGR 5:6:5 format. */
#define CNA_SURFACE_FORMAT_BGR565 UINT32_C(1)
/** @brief Unsigned 16-bit BGRA 5:5:5:1 format. */
#define CNA_SURFACE_FORMAT_BGRA5551 UINT32_C(2)
/** @brief Unsigned 16-bit BGRA 4:4:4:4 format. */
#define CNA_SURFACE_FORMAT_BGRA4444 UINT32_C(3)
/** @brief DXT1 block-compressed format. */
#define CNA_SURFACE_FORMAT_DXT1 UINT32_C(4)
/** @brief DXT3 block-compressed format. */
#define CNA_SURFACE_FORMAT_DXT3 UINT32_C(5)
/** @brief DXT5 block-compressed format. */
#define CNA_SURFACE_FORMAT_DXT5 UINT32_C(6)
/** @brief Signed normalized two-byte format. */
#define CNA_SURFACE_FORMAT_NORMALIZED_BYTE2 UINT32_C(7)
/** @brief Signed normalized four-byte format. */
#define CNA_SURFACE_FORMAT_NORMALIZED_BYTE4 UINT32_C(8)
/** @brief Unsigned 32-bit RGBA 10:10:10:2 format. */
#define CNA_SURFACE_FORMAT_RGBA1010102 UINT32_C(9)
/** @brief Unsigned 32-bit RG format with 16 bits per channel. */
#define CNA_SURFACE_FORMAT_RG32 UINT32_C(10)
/** @brief Unsigned 64-bit RGBA format with 16 bits per channel. */
#define CNA_SURFACE_FORMAT_RGBA64 UINT32_C(11)
/** @brief Unsigned eight-bit alpha-only format. */
#define CNA_SURFACE_FORMAT_ALPHA8 UINT32_C(12)
/** @brief Single-channel IEEE binary32 format. */
#define CNA_SURFACE_FORMAT_SINGLE UINT32_C(13)
/** @brief Two-channel IEEE binary32 format. */
#define CNA_SURFACE_FORMAT_VECTOR2 UINT32_C(14)
/** @brief Four-channel IEEE binary32 format. */
#define CNA_SURFACE_FORMAT_VECTOR4 UINT32_C(15)
/** @brief Single-channel IEEE binary16 format. */
#define CNA_SURFACE_FORMAT_HALF_SINGLE UINT32_C(16)
/** @brief Two-channel IEEE binary16 format. */
#define CNA_SURFACE_FORMAT_HALF_VECTOR2 UINT32_C(17)
/** @brief Four-channel IEEE binary16 format. */
#define CNA_SURFACE_FORMAT_HALF_VECTOR4 UINT32_C(18)
/** @brief High-dynamic-range blendable format. */
#define CNA_SURFACE_FORMAT_HDR_BLENDABLE UINT32_C(19)
/** @brief CNA extension for an unsigned 32-bit BGRA color format. */
#define CNA_SURFACE_FORMAT_COLOR_BGRA_EXT UINT32_C(20)
/** @brief CNA extension for an sRGB-encoded 32-bit color format. */
#define CNA_SURFACE_FORMAT_COLOR_SRGB_EXT UINT32_C(21)
/** @brief CNA extension for sRGB-encoded DXT5 blocks. */
#define CNA_SURFACE_FORMAT_DXT5_SRGB_EXT UINT32_C(22)
/** @brief CNA extension for BC7 blocks. */
#define CNA_SURFACE_FORMAT_BC7_EXT UINT32_C(23)
/** @brief CNA extension for sRGB-encoded BC7 blocks. */
#define CNA_SURFACE_FORMAT_BC7_SRGB_EXT UINT32_C(24)
/** @brief CNA extension for an unsigned eight-bit single-channel format. */
#define CNA_SURFACE_FORMAT_BYTE_EXT UINT32_C(25)
/** @brief CNA extension for an unsigned 16-bit single-channel format. */
#define CNA_SURFACE_FORMAT_USHORT_EXT UINT32_C(26)

/** @brief Fixed-width SpriteBatch draw ordering mode. */
typedef uint32_t CNA_SpriteSortMode;

/** @brief Defers sprites until end and preserves submission order. */
#define CNA_SPRITE_SORT_MODE_DEFERRED UINT32_C(0)
/** @brief Sends each sprite to the renderer during submission. */
#define CNA_SPRITE_SORT_MODE_IMMEDIATE UINT32_C(1)
/** @brief Defers sprites and groups them by texture. */
#define CNA_SPRITE_SORT_MODE_TEXTURE UINT32_C(2)
/** @brief Defers sprites and orders them from greater to lesser layer depth. */
#define CNA_SPRITE_SORT_MODE_BACK_TO_FRONT UINT32_C(3)
/** @brief Defers sprites and orders them from lesser to greater layer depth. */
#define CNA_SPRITE_SORT_MODE_FRONT_TO_BACK UINT32_C(4)

/**
 * @brief Fixed-width bit set of SpriteBatch mirroring effects.
 *
 * The native declaration provides `|`, `&`, `|=` and `&=` operators over its scoped enumeration.
 * This fixed-width integer alias needs no adapter for them: C's own bitwise operators apply
 * directly to a `CNA_SpriteEffects` value and produce the identical bit pattern.
 */
typedef uint32_t CNA_SpriteEffects;

/** @brief Applies no sprite mirroring. */
#define CNA_SPRITE_EFFECT_NONE UINT32_C(0)
/** @brief Mirrors a sprite horizontally. */
#define CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY (UINT32_C(1) << 0)
/** @brief Mirrors a sprite vertically. */
#define CNA_SPRITE_EFFECT_FLIP_VERTICALLY (UINT32_C(1) << 1)

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
 * @brief Describes the logical backbuffer addressed by GraphicsDevice readback.
 */
typedef struct CNA_BackBufferInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Logical backbuffer width in pixels. */
    uint32_t width;

    /** @brief Logical backbuffer height in pixels. */
    uint32_t height;

    /** @brief Canonical surface format reported by the presentation parameters. */
    CNA_SurfaceFormat format;

    /** @brief Reserved for future use; returned as zero. */
    uint32_t reserved;
} CNA_BackBufferInfo;

/**
 * @brief Configures creation of an owned two-dimensional texture.
 */
typedef struct CNA_Texture2DCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Width in pixels; must be greater than zero. */
    uint32_t width;

    /** @brief Height in pixels; must be greater than zero. */
    uint32_t height;

    /** @brief `CNA_TRUE` to allocate a complete mip chain, otherwise `CNA_FALSE`. */
    CNA_Bool mip_map;

    /** @brief Reserved bytes; callers must initialize them to zero. */
    uint8_t reserved[3];

    /** @brief Surface format; the initial bulk-transfer slice supports `CNA_SURFACE_FORMAT_COLOR`. */
    CNA_SurfaceFormat format;
} CNA_Texture2DCreateInfo;

/**
 * @brief Describes an owned two-dimensional texture.
 */
typedef struct CNA_Texture2DInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Texture width in pixels. */
    uint32_t width;

    /** @brief Texture height in pixels. */
    uint32_t height;

    /** @brief Number of allocated mip levels. */
    uint32_t level_count;

    /** @brief Texture surface format. */
    CNA_SurfaceFormat format;
} CNA_Texture2DInfo;

/**
 * @brief Configures one SpriteBatch begin/end interval.
 */
typedef struct CNA_SpriteBatchBeginInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Native sprite ordering mode. */
    CNA_SpriteSortMode sort_mode;

    /** @brief Reserved for future state selection; callers must initialize this to zero. */
    uint32_t reserved;
} CNA_SpriteBatchBeginInfo;

/**
 * @brief Describes one textured quad in a batched SpriteBatch submission.
 */
typedef struct CNA_SpriteCommand {
    /** @brief Size of this array element in bytes; version 1 requires exact current size. */
    uint32_t struct_size;

    /** @brief Version of this array element. */
    uint32_t struct_version;

    /** @brief Owned Color Texture2D handle sampled by this command. */
    CNA_Handle texture;

    /** @brief Destination rectangle in screen-space pixels. */
    CNA_Rectangle destination;

    /** @brief Source rectangle in texture pixels. */
    CNA_Rectangle source;

    /** @brief Per-channel tint multiplied with the sampled texture. */
    CNA_Color color;

    /** @brief Clockwise rotation in radians. Must be finite. */
    float rotation;

    /** @brief Rotation origin in source-texture pixels. Non-finite components are accepted. */
    CNA_Vector2 origin;

    /** @brief Zero or more `CNA_SPRITE_EFFECT_*` bits. */
    CNA_SpriteEffects effects;

    /** @brief Sort depth consumed by depth-based modes. Must be finite. */
    float layer_depth;
} CNA_SpriteCommand;

/**
 * @brief Describes one textured-quad command placed by position and scale.
 *
 * `CNA_SpriteCommand` gives a destination rectangle; the canonical API also has a family of Draw
 * overloads that take a **position and a scale** instead, and the two are not interchangeable: with
 * a position, @ref origin is measured in source-texture pixels and the scale applies after that
 * offset, which a caller cannot reproduce by computing a rectangle without repeating the canonical
 * arithmetic. This is a separate structure rather than more fields on the first one, so nothing
 * already compiled against `CNA_SpriteCommand` changes size or meaning.
 *
 * The uniform-scale overload is the case where both components of @ref scale are equal; there is no
 * separate route for it.
 */
typedef struct CNA_SpriteScaledCommand {
    /** @brief Size of this array element in bytes; version 1 requires exact current size. */
    uint32_t struct_size;

    /** @brief Version of this array element. */
    uint32_t struct_version;

    /** @brief Owned Color Texture2D handle sampled by this command. */
    CNA_Handle texture;

    /** @brief Screen-space position of the command's origin, in pixels. Must be finite. */
    CNA_Vector2 position;

    /**
     * @brief Source rectangle in texture pixels.
     *
     * A rectangle of zero width **and** zero height draws the whole texture, which is what an empty
     * optional means to the canonical call.
     */
    CNA_Rectangle source;

    /** @brief Per-channel tint multiplied with the sampled texture. */
    CNA_Color color;

    /** @brief Clockwise rotation in radians. Must be finite. */
    float rotation;

    /** @brief Rotation and scale origin in source-texture pixels. Non-finite components are accepted. */
    CNA_Vector2 origin;

    /** @brief Per-axis scale; equal components are uniform scale. Non-finite components are accepted. */
    CNA_Vector2 scale;

    /** @brief Zero or more `CNA_SPRITE_EFFECT_*` bits. */
    CNA_SpriteEffects effects;

    /** @brief Sort depth consumed by depth-based modes. Must be finite. */
    float layer_depth;
} CNA_SpriteScaledCommand;

/**
 * @brief Submits an array of position-and-scale commands through one C ABI transition.
 *
 * @param sprite_batch Owned SpriteBatch handle inside a begin/end interval.
 * @param commands Caller-owned commands copied during this call, or null only when
 *        @p command_count is zero.
 * @param command_count Number of elements beginning at @p commands.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` outside a begin/end interval,
 *         `CNA_RESULT_NOT_SUPPORTED` when the renderer refuses the requested operation, or another
 *         documented argument/handle/thread/native failure.
 *
 * The same rules as @ref cna_sprite_batch_submit_many: every handle and field is validated before
 * native submission starts, every texture must belong to the same game as the SpriteBatch and
 * cannot be destroyed until a successful @ref cna_sprite_batch_end releases the batch's references.
 */
CNA_C_API CNA_Result cna_sprite_batch_submit_scaled_many(
    CNA_Handle sprite_batch,
    const CNA_SpriteScaledCommand* commands,
    uint64_t command_count);

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

/**
 * @brief Gets the classified answer for one atomic renderer feature.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param feature One `CNA_RENDERER_FEATURE_*` identity.
 * @param out_support Receives one `CNA_RENDERER_FEATURE_SUPPORT_*` answer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_renderer_feature_support_ext(
    CNA_Handle graphics_device,
    CNA_RendererFeature feature,
    CNA_RendererFeatureSupport* out_support);

/**
 * @brief Gets one numeric renderer/device limit and whether the answer is known.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param limit One `CNA_RENDERER_LIMIT_*` identity.
 * @param out_known Receives `CNA_TRUE` when @p out_value is a classified answer.
 * @param out_value Receives the limit value, or zero when the answer is unknown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_renderer_limit_ext(
    CNA_Handle graphics_device,
    CNA_RendererLimit limit,
    CNA_Bool* out_known,
    uint64_t* out_value);

/**
 * @brief Gets known and supported usage masks for one surface format.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param format One `CNA_SURFACE_FORMAT_*` identity.
 * @param out_known_usages Receives usage bits whose support has been classified.
 * @param out_supported_usages Receives supported usage bits, always a subset of known bits.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A bit absent from @p out_known_usages means unknown, not unsupported. Callers must not infer
 * support or rejection for such a usage from the renderer name or another usage bit.
 */
CNA_C_API CNA_Result cna_graphics_device_get_surface_format_support_ext(
    CNA_Handle graphics_device,
    CNA_SurfaceFormat format,
    CNA_RendererFormatUsageFlags* out_known_usages,
    CNA_RendererFormatUsageFlags* out_supported_usages);

/**
 * @brief Gets the UTF-8 byte count of the complete English renderer-capability report.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_bytes Receives the exact report byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_capability_report_size_ext(
    CNA_Handle graphics_device,
    uint64_t* out_bytes);

/**
 * @brief Copies the complete English renderer-capability report as UTF-8 without a terminator.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param destination Caller-owned destination bytes, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination in bytes.
 * @param out_bytes Receives the required report byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread/native failure. No partial report is written.
 */
CNA_C_API CNA_Result cna_graphics_device_copy_capability_report_ext(
    CNA_Handle graphics_device,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the shading dialect a custom effect's sources must be written in.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_dialect Receives one `CNA_SHADER_DIALECT_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A source-based effect (`cna_shader_effect_create`) is renderer-specific text, and the renderer
 * identity is not a safe way to infer which text to supply: it is wrong in a build carrying
 * several renderers, and meaningless for a renderer that picks its native API per process. Ask
 * here instead.
 *
 * `CNA_SHADER_DIALECT_UNKNOWN` means the active renderer has not declared one. Read that as "do
 * not guess", not as "no shaders": it is the answer a caller should refuse to build sources from,
 * rather than one to fall back on.
 */
CNA_C_API CNA_Result cna_graphics_device_get_shader_dialect_ext(
    CNA_Handle graphics_device,
    CNA_ShaderDialect* out_dialect);

/**
 * @brief Gets the logical backbuffer dimensions and surface format.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_info Caller-provided versioned structure to receive backbuffer information.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_backbuffer_info(
    CNA_Handle graphics_device,
    CNA_BackBufferInfo* out_info);

/**
 * @brief Reads the complete logical backbuffer into a caller-owned RGBA8 array.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param destination Caller-owned output pixels, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination measured in pixels.
 * @param out_pixels Receives the exact width-times-height pixel count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` when the
 * active renderer has no honest backbuffer readback, or another documented
 * argument/handle/thread/native failure. No partial pixel array is written.
 *
 * Readback is intended for draw-time capture before the callback returns and the game presents.
 */
CNA_C_API CNA_Result cna_graphics_device_get_backbuffer_data_rgba8(
    CNA_Handle graphics_device,
    CNA_Color* destination,
    uint64_t capacity,
    uint64_t* out_pixels);

/**
 * @brief Creates an owned two-dimensional texture in a renderer-supported surface format.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param create_info Versioned dimensions, mip and surface-format configuration.
 * @param out_texture Receives an owned texture handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` for a known unavailable format or
 * dimension, or a documented argument/handle/thread/native failure.
 *
 * The texture outlives the callback that creates it, but remains a child of the active game. The
 * caller must destroy it before calling @ref cna_game_destroy.
 */
CNA_C_API CNA_Result cna_texture2d_create(
    CNA_Handle graphics_device,
    const CNA_Texture2DCreateInfo* create_info,
    CNA_Handle* out_texture);

/**
 * @brief Gets dimensions, mip count and format for an owned two-dimensional texture.
 *
 * @param texture Owned texture handle.
 * @param out_info Caller-provided versioned structure to receive texture information.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_texture2d_get_info(
    CNA_Handle texture,
    CNA_Texture2DInfo* out_info);

/**
 * @brief Replaces Color-format mip level zero from an RGBA8 pixel array.
 *
 * @param texture Owned Color-format texture handle.
 * @param pixels Caller-owned pixels copied during this call.
 * @param pixel_count Exact number of pixels; must equal width multiplied by height.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_texture2d_set_data_rgba8(
    CNA_Handle texture,
    const CNA_Color* pixels,
    uint64_t pixel_count);

/**
 * @brief Reads Color-format mip level zero into a caller-owned RGBA8 pixel array.
 *
 * @param texture Owned Color-format texture handle.
 * @param destination Caller-owned output pixels, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination measured in pixels.
 * @param out_pixels Receives the exact required pixel count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread/native failure. No partial pixel array is written.
 */
CNA_C_API CNA_Result cna_texture2d_get_data_rgba8(
    CNA_Handle texture,
    CNA_Color* destination,
    uint64_t capacity,
    uint64_t* out_pixels);

/**
 * @brief Disposes and releases an owned two-dimensional texture.
 *
 * @param texture Owned texture handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. The handle is invalid
 * after success; a second destroy returns `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_texture2d_destroy(CNA_Handle texture);

/**
 * @brief Creates an owned SpriteBatch bound to a borrowed graphics device.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_sprite_batch Receives an owned SpriteBatch handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the renderer refuses SpriteBatch,
 * or a documented argument/handle/thread/native failure.
 *
 * The SpriteBatch outlives the callback that creates it, remains a child of the active game and
 * must be destroyed before @ref cna_game_destroy.
 */
CNA_C_API CNA_Result cna_sprite_batch_create(
    CNA_Handle graphics_device,
    CNA_Handle* out_sprite_batch);

/**
 * @brief Begins a SpriteBatch interval using a native sort mode and fixed default XNA states.
 *
 * @param sprite_batch Owned SpriteBatch handle.
 * @param begin_info Versioned sort-mode configuration.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` if already begun,
 * `CNA_RESULT_NOT_SUPPORTED` when the renderer refuses the requested operation, or another
 * documented argument/handle/thread/native failure.
 *
 * This initial slice always uses AlphaBlend, LinearClamp, DepthStencilState.None,
 * CullCounterClockwise, the identity transform and no custom effect.
 */
CNA_C_API CNA_Result cna_sprite_batch_begin(
    CNA_Handle sprite_batch,
    const CNA_SpriteBatchBeginInfo* begin_info);

/**
 * @brief Submits an array of textured-quad commands through one C ABI transition.
 *
 * @param sprite_batch Owned SpriteBatch handle inside a begin/end interval.
 * @param commands Caller-owned commands copied during this call, or null only when
 * @p command_count is zero.
 * @param command_count Number of elements beginning at @p commands.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` outside a begin/end interval,
 * `CNA_RESULT_NOT_SUPPORTED` when the renderer refuses the requested operation, or another
 * documented argument/handle/thread/native failure.
 *
 * All handles and POD fields are validated before native submission starts. Every texture must
 * belong to the same game as the SpriteBatch and cannot be destroyed until a successful
 * @ref cna_sprite_batch_end releases the batch's references. Version 1 arrays use a fixed stride,
 * so every command's @ref CNA_SpriteCommand::struct_size must equal `sizeof(CNA_SpriteCommand)`.
 */
CNA_C_API CNA_Result cna_sprite_batch_submit_many(
    CNA_Handle sprite_batch,
    const CNA_SpriteCommand* commands,
    uint64_t command_count);

/**
 * @brief Flushes queued sprites and ends the active SpriteBatch interval.
 *
 * @param sprite_batch Owned SpriteBatch handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no interval is active,
 * `CNA_RESULT_NOT_SUPPORTED` when the renderer refuses the operation, or another documented
 * handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sprite_batch_end(CNA_Handle sprite_batch);

/**
 * @brief Disposes and releases an owned SpriteBatch.
 *
 * @param sprite_batch Owned SpriteBatch handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. A second destroy
 * returns `CNA_RESULT_INVALID_HANDLE`.
 *
 * Destroying during an active interval cancels that interval without flushing deferred commands
 * and releases all retained texture references. Commands already emitted by Immediate mode cannot
 * be undone. This cleanup route remains available after a native end/flush failure.
 */
CNA_C_API CNA_Result cna_sprite_batch_destroy(CNA_Handle sprite_batch);

/**
 * @brief Describes one SpriteBatch text draw.
 *
 * The canonical surface has six `DrawString` overloads: three parameter shapes, each accepting
 * either a `std::string` or a `System::Text::StringBuilder`. Both text types are copied before
 * layout, so a single UTF-8 view expresses either; the parameter shapes differ only in which of
 * the transform fields they leave at their defaults.
 */
typedef struct CNA_SpriteTextCommand {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Owned SpriteFont handle belonging to the same game as the batch. */
    CNA_Handle sprite_font;

    /** @brief UTF-8 text copied during this call. */
    CNA_StringView text;

    /** @brief Screen-space position of the text origin. Non-finite components are accepted. */
    CNA_Vector2 position;

    /** @brief Per-glyph tint. */
    CNA_Color color;

    /** @brief Clockwise rotation in radians. Must be finite. */
    float rotation;

    /** @brief Rotation origin in unscaled text pixels. Non-finite components are accepted. */
    CNA_Vector2 origin;

    /** @brief Per-axis scale; use equal components for the uniform-scale overloads. */
    CNA_Vector2 scale;

    /** @brief Zero or more `CNA_SPRITE_EFFECT_*` bits. */
    CNA_SpriteEffects effects;

    /** @brief Sort depth consumed by depth-based modes. Must be finite. */
    float layer_depth;
} CNA_SpriteTextCommand;

/**
 * @brief Describes one indexed triangle mesh submitted through a SpriteBatch.
 */
typedef struct CNA_SpriteMeshEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Owned effect handle belonging to the same game as the batch. */
    CNA_Handle effect;

    /** @brief Caller-owned screen-space positions read during this call. */
    const CNA_Vector2* positions;

    /** @brief Caller-owned per-vertex colors, or null to use opaque white. */
    const CNA_Color* colors;

    /** @brief Caller-owned texture coordinates, or null when the effect samples nothing. */
    const CNA_Vector2* texture_coordinates;

    /** @brief Caller-owned 16-bit triangle indices read during this call. */
    const uint16_t* indices;

    /** @brief Number of vertices in each supplied array. */
    uint64_t vertex_count;

    /** @brief Number of indices beginning at @ref indices. */
    uint64_t index_count;
} CNA_SpriteMeshEXT;

/**
 * @brief Gets the UTF-8 byte count of the SpriteBatch type name.
 *
 * @param sprite_batch Owned SpriteBatch handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sprite_batch_get_type_name_size(
    CNA_Handle sprite_batch,
    uint64_t* out_bytes);

/**
 * @brief Copies the SpriteBatch type name as UTF-8 bytes without a terminator.
 *
 * @param sprite_batch Owned SpriteBatch handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_sprite_batch_copy_type_name(
    CNA_Handle sprite_batch,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Draws one string through an active SpriteBatch interval.
 *
 * @param sprite_batch Owned SpriteBatch handle inside a begin/end interval.
 * @param command Versioned text description validated before native submission.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` outside a begin/end interval,
 * `CNA_RESULT_NOT_SUPPORTED` when the renderer refuses the operation, or another documented
 * argument/handle/thread/native failure.
 *
 * The font must belong to the same game as the batch and is retained until a successful
 * @ref cna_sprite_batch_end, exactly like a drawn texture.
 */
CNA_C_API CNA_Result cna_sprite_batch_draw_string(
    CNA_Handle sprite_batch,
    const CNA_SpriteTextCommand* command);

/**
 * @brief Submits one indexed triangle mesh through an active SpriteBatch interval.
 *
 * @param sprite_batch Owned SpriteBatch handle inside an Immediate begin/end interval.
 * @param mesh Versioned mesh description validated before native submission.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` outside an interval or outside
 * `CNA_SPRITE_SORT_MODE_IMMEDIATE`, `CNA_RESULT_NOT_SUPPORTED` when the renderer refuses the
 * operation, or another documented argument/handle/thread/native failure.
 *
 * A mesh draw deliberately does not join the deferred sprite queue, so the canonical contract
 * requires Immediate mode. All arrays are read during the call and never retained.
 */
CNA_C_API CNA_Result cna_sprite_batch_draw_mesh_ext(
    CNA_Handle sprite_batch,
    const CNA_SpriteMeshEXT* mesh);

#ifdef __cplusplus
}
#endif

#endif
