// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_STATE_H
#define CNA_C_GRAPHICS_STATE_H

#include "CNA/C/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fully qualified native type name represented by @ref CNA_BlendState. */
#define CNA_BLEND_STATE_TYPE_NAME "Microsoft.Xna.Framework.Graphics.BlendState"
/** @brief Fully qualified native type name represented by @ref CNA_DepthStencilState. */
#define CNA_DEPTH_STENCIL_STATE_TYPE_NAME "Microsoft.Xna.Framework.Graphics.DepthStencilState"
/** @brief Fully qualified native type name represented by @ref CNA_RasterizerState. */
#define CNA_RASTERIZER_STATE_TYPE_NAME "Microsoft.Xna.Framework.Graphics.RasterizerState"
/** @brief Fully qualified native type name represented by @ref CNA_SamplerState. */
#define CNA_SAMPLER_STATE_TYPE_NAME "Microsoft.Xna.Framework.Graphics.SamplerState"

/** @brief Fixed-width blend-factor identity. */
typedef uint32_t CNA_Blend;
/** @brief Multiplies by one. */
#define CNA_BLEND_ONE UINT32_C(0)
/** @brief Multiplies by zero. */
#define CNA_BLEND_ZERO UINT32_C(1)
/** @brief Multiplies by source color. */
#define CNA_BLEND_SOURCE_COLOR UINT32_C(2)
/** @brief Multiplies by inverse source color. */
#define CNA_BLEND_INVERSE_SOURCE_COLOR UINT32_C(3)
/** @brief Multiplies by source alpha. */
#define CNA_BLEND_SOURCE_ALPHA UINT32_C(4)
/** @brief Multiplies by inverse source alpha. */
#define CNA_BLEND_INVERSE_SOURCE_ALPHA UINT32_C(5)
/** @brief Multiplies by destination color. */
#define CNA_BLEND_DESTINATION_COLOR UINT32_C(6)
/** @brief Multiplies by inverse destination color. */
#define CNA_BLEND_INVERSE_DESTINATION_COLOR UINT32_C(7)
/** @brief Multiplies by destination alpha. */
#define CNA_BLEND_DESTINATION_ALPHA UINT32_C(8)
/** @brief Multiplies by inverse destination alpha. */
#define CNA_BLEND_INVERSE_DESTINATION_ALPHA UINT32_C(9)
/** @brief Multiplies by the constant blend factor. */
#define CNA_BLEND_FACTOR UINT32_C(10)
/** @brief Multiplies by inverse constant blend factor. */
#define CNA_BLEND_INVERSE_FACTOR UINT32_C(11)
/** @brief Uses source-alpha saturation. */
#define CNA_BLEND_SOURCE_ALPHA_SATURATION UINT32_C(12)

/** @brief Fixed-width blend-function identity. */
typedef uint32_t CNA_BlendFunction;
/** @brief Adds source and destination contributions. */
#define CNA_BLEND_FUNCTION_ADD UINT32_C(0)
/** @brief Subtracts destination from source. */
#define CNA_BLEND_FUNCTION_SUBTRACT UINT32_C(1)
/** @brief Subtracts source from destination. */
#define CNA_BLEND_FUNCTION_REVERSE_SUBTRACT UINT32_C(2)
/** @brief Selects the component-wise maximum. */
#define CNA_BLEND_FUNCTION_MAX UINT32_C(3)
/** @brief Selects the component-wise minimum. */
#define CNA_BLEND_FUNCTION_MIN UINT32_C(4)

/** @brief Fixed-width color-write channel bit set. */
typedef uint32_t CNA_ColorWriteChannels;
/** @brief Writes no color channels. */
#define CNA_COLOR_WRITE_NONE UINT32_C(0)
/** @brief Writes the red channel. */
#define CNA_COLOR_WRITE_RED UINT32_C(1)
/** @brief Writes the green channel. */
#define CNA_COLOR_WRITE_GREEN UINT32_C(2)
/** @brief Writes the blue channel. */
#define CNA_COLOR_WRITE_BLUE UINT32_C(4)
/** @brief Writes the alpha channel. */
#define CNA_COLOR_WRITE_ALPHA UINT32_C(8)
/** @brief Writes every color channel. */
#define CNA_COLOR_WRITE_ALL UINT32_C(15)

/** @brief Fixed-width comparison-function identity. */
typedef uint32_t CNA_CompareFunction;
/** @brief Always passes. */
#define CNA_COMPARE_ALWAYS UINT32_C(0)
/** @brief Never passes. */
#define CNA_COMPARE_NEVER UINT32_C(1)
/** @brief Passes when the new value is less. */
#define CNA_COMPARE_LESS UINT32_C(2)
/** @brief Passes when the new value is less than or equal. */
#define CNA_COMPARE_LESS_EQUAL UINT32_C(3)
/** @brief Passes when values are equal. */
#define CNA_COMPARE_EQUAL UINT32_C(4)
/** @brief Passes when the new value is greater than or equal. */
#define CNA_COMPARE_GREATER_EQUAL UINT32_C(5)
/** @brief Passes when the new value is greater. */
#define CNA_COMPARE_GREATER UINT32_C(6)
/** @brief Passes when values are not equal. */
#define CNA_COMPARE_NOT_EQUAL UINT32_C(7)

/** @brief Fixed-width stencil-operation identity. */
typedef uint32_t CNA_StencilOperation;
/** @brief Keeps the current stencil value. */
#define CNA_STENCIL_KEEP UINT32_C(0)
/** @brief Replaces the stencil value with zero. */
#define CNA_STENCIL_ZERO UINT32_C(1)
/** @brief Replaces the stencil value with the reference. */
#define CNA_STENCIL_REPLACE UINT32_C(2)
/** @brief Increments with wraparound. */
#define CNA_STENCIL_INCREMENT UINT32_C(3)
/** @brief Decrements with wraparound. */
#define CNA_STENCIL_DECREMENT UINT32_C(4)
/** @brief Increments with saturation. */
#define CNA_STENCIL_INCREMENT_SATURATION UINT32_C(5)
/** @brief Decrements with saturation. */
#define CNA_STENCIL_DECREMENT_SATURATION UINT32_C(6)
/** @brief Inverts every stencil bit. */
#define CNA_STENCIL_INVERT UINT32_C(7)

/** @brief Fixed-width face-culling identity. */
typedef uint32_t CNA_CullMode;
/** @brief Disables face culling. */
#define CNA_CULL_NONE UINT32_C(0)
/** @brief Culls clockwise faces. */
#define CNA_CULL_CLOCKWISE_FACE UINT32_C(1)
/** @brief Culls counter-clockwise faces. */
#define CNA_CULL_COUNTER_CLOCKWISE_FACE UINT32_C(2)

/** @brief Fixed-width polygon-fill identity. */
typedef uint32_t CNA_FillMode;
/** @brief Fills primitive faces. */
#define CNA_FILL_SOLID UINT32_C(0)
/** @brief Draws primitive edges. */
#define CNA_FILL_WIREFRAME UINT32_C(1)

/** @brief Fixed-width texture-address identity. */
typedef uint32_t CNA_TextureAddressMode;
/** @brief Tiles texture coordinates. */
#define CNA_TEXTURE_ADDRESS_WRAP UINT32_C(0)
/** @brief Clamps texture coordinates. */
#define CNA_TEXTURE_ADDRESS_CLAMP UINT32_C(1)
/** @brief Tiles and mirrors texture coordinates. */
#define CNA_TEXTURE_ADDRESS_MIRROR UINT32_C(2)

/** @brief Fixed-width texture-filter identity. */
typedef uint32_t CNA_TextureFilter;
/** @brief Uses linear minification, magnification and mip filtering. */
#define CNA_TEXTURE_FILTER_LINEAR UINT32_C(0)
/** @brief Uses point minification, magnification and mip filtering. */
#define CNA_TEXTURE_FILTER_POINT UINT32_C(1)
/** @brief Uses anisotropic filtering. */
#define CNA_TEXTURE_FILTER_ANISOTROPIC UINT32_C(2)
/** @brief Uses linear filtering with point mip selection. */
#define CNA_TEXTURE_FILTER_LINEAR_MIP_POINT UINT32_C(3)
/** @brief Uses point filtering with linear mip selection. */
#define CNA_TEXTURE_FILTER_POINT_MIP_LINEAR UINT32_C(4)
/** @brief Uses linear minification, point magnification and linear mip filtering. */
#define CNA_TEXTURE_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR UINT32_C(5)
/** @brief Uses linear minification, point magnification and point mip filtering. */
#define CNA_TEXTURE_FILTER_MIN_LINEAR_MAG_POINT_MIP_POINT UINT32_C(6)
/** @brief Uses point minification, linear magnification and linear mip filtering. */
#define CNA_TEXTURE_FILTER_MIN_POINT_MAG_LINEAR_MIP_LINEAR UINT32_C(7)
/** @brief Uses point minification, linear magnification and point mip filtering. */
#define CNA_TEXTURE_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT UINT32_C(8)

/** @brief Identifies a native BlendState preset. */
typedef uint32_t CNA_BlendStatePreset;
/** @brief XNA-compatible default BlendState values. */
#define CNA_BLEND_STATE_PRESET_DEFAULT UINT32_C(0)
/** @brief Additive blending preset. */
#define CNA_BLEND_STATE_PRESET_ADDITIVE UINT32_C(1)
/** @brief Premultiplied-alpha blending preset. */
#define CNA_BLEND_STATE_PRESET_ALPHA_BLEND UINT32_C(2)
/** @brief Non-premultiplied-alpha blending preset. */
#define CNA_BLEND_STATE_PRESET_NON_PREMULTIPLIED UINT32_C(3)
/** @brief Opaque blending preset. */
#define CNA_BLEND_STATE_PRESET_OPAQUE UINT32_C(4)

/** @brief Identifies a native DepthStencilState preset. */
typedef uint32_t CNA_DepthStencilStatePreset;
/** @brief XNA-compatible default depth/stencil state. */
#define CNA_DEPTH_STENCIL_STATE_PRESET_DEFAULT UINT32_C(0)
/** @brief Read-only depth-buffer preset. */
#define CNA_DEPTH_STENCIL_STATE_PRESET_DEPTH_READ UINT32_C(1)
/** @brief Disabled depth/stencil preset. */
#define CNA_DEPTH_STENCIL_STATE_PRESET_NONE UINT32_C(2)

/** @brief Identifies a native RasterizerState preset. */
typedef uint32_t CNA_RasterizerStatePreset;
/** @brief XNA-compatible default rasterizer state. */
#define CNA_RASTERIZER_STATE_PRESET_DEFAULT UINT32_C(0)
/** @brief Clockwise-face culling preset. */
#define CNA_RASTERIZER_STATE_PRESET_CULL_CLOCKWISE UINT32_C(1)
/** @brief Counter-clockwise-face culling preset. */
#define CNA_RASTERIZER_STATE_PRESET_CULL_COUNTER_CLOCKWISE UINT32_C(2)
/** @brief No-culling preset. */
#define CNA_RASTERIZER_STATE_PRESET_CULL_NONE UINT32_C(3)

/** @brief Identifies a native SamplerState preset. */
typedef uint32_t CNA_SamplerStatePreset;
/** @brief XNA-compatible default sampler values. */
#define CNA_SAMPLER_STATE_PRESET_DEFAULT UINT32_C(0)
/** @brief Anisotropic filtering with clamp addressing. */
#define CNA_SAMPLER_STATE_PRESET_ANISOTROPIC_CLAMP UINT32_C(1)
/** @brief Anisotropic filtering with wrap addressing. */
#define CNA_SAMPLER_STATE_PRESET_ANISOTROPIC_WRAP UINT32_C(2)
/** @brief Linear filtering with clamp addressing. */
#define CNA_SAMPLER_STATE_PRESET_LINEAR_CLAMP UINT32_C(3)
/** @brief Linear filtering with wrap addressing. */
#define CNA_SAMPLER_STATE_PRESET_LINEAR_WRAP UINT32_C(4)
/** @brief Point filtering with clamp addressing. */
#define CNA_SAMPLER_STATE_PRESET_POINT_CLAMP UINT32_C(5)
/** @brief Point filtering with wrap addressing. */
#define CNA_SAMPLER_STATE_PRESET_POINT_WRAP UINT32_C(6)

/** @brief Selects a pixel- or vertex-shader sampler collection. */
typedef uint32_t CNA_ShaderStage;
/** @brief Pixel-shader sampler collection. */
#define CNA_SHADER_STAGE_PIXEL UINT32_C(0)
/** @brief Vertex-shader sampler collection. */
#define CNA_SHADER_STAGE_VERTEX UINT32_C(1)
/** @brief Number of entries in each native sampler-state collection. */
#define CNA_MAX_SAMPLERS UINT32_C(16)

/** @brief Complete mutable BlendState value represented as a versioned C POD. */
typedef struct CNA_BlendState {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Alpha-channel blend function. */
    CNA_BlendFunction alpha_blend_function;
    /** @brief Destination alpha blend factor. */
    CNA_Blend alpha_destination_blend;
    /** @brief Source alpha blend factor. */
    CNA_Blend alpha_source_blend;
    /** @brief Color-channel blend function. */
    CNA_BlendFunction color_blend_function;
    /** @brief Destination color blend factor. */
    CNA_Blend color_destination_blend;
    /** @brief Source color blend factor. */
    CNA_Blend color_source_blend;
    /** @brief Color-write channels for render target zero. */
    CNA_ColorWriteChannels color_write_channels;
    /** @brief Color-write channels for render target one. */
    CNA_ColorWriteChannels color_write_channels1;
    /** @brief Color-write channels for render target two. */
    CNA_ColorWriteChannels color_write_channels2;
    /** @brief Color-write channels for render target three. */
    CNA_ColorWriteChannels color_write_channels3;
    /** @brief Constant blend-factor color. */
    CNA_Color blend_factor;
    /** @brief Multisample coverage mask. */
    int32_t multi_sample_mask;
} CNA_BlendState;

/** @brief Complete mutable DepthStencilState value represented as a versioned C POD. */
typedef struct CNA_DepthStencilState {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Whether depth testing is enabled. */
    CNA_Bool depth_buffer_enable;
    /** @brief Whether depth writes are enabled. */
    CNA_Bool depth_buffer_write_enable;
    /** @brief Whether stencil testing is enabled. */
    CNA_Bool stencil_enable;
    /** @brief Whether counter-clockwise faces use separate stencil rules. */
    CNA_Bool two_sided_stencil_mode;
    /** @brief Depth comparison function. */
    CNA_CompareFunction depth_buffer_function;
    /** @brief Clockwise-face stencil comparison function. */
    CNA_CompareFunction stencil_function;
    /** @brief Stencil read mask. */
    int32_t stencil_mask;
    /** @brief Stencil write mask. */
    int32_t stencil_write_mask;
    /** @brief Stencil reference value. */
    int32_t reference_stencil;
    /** @brief Operation when the clockwise stencil test fails. */
    CNA_StencilOperation stencil_fail;
    /** @brief Operation when clockwise stencil passes and depth fails. */
    CNA_StencilOperation stencil_depth_buffer_fail;
    /** @brief Operation when clockwise stencil and depth pass. */
    CNA_StencilOperation stencil_pass;
    /** @brief Counter-clockwise-face stencil comparison function. */
    CNA_CompareFunction counter_clockwise_stencil_function;
    /** @brief Operation when the counter-clockwise stencil test fails. */
    CNA_StencilOperation counter_clockwise_stencil_fail;
    /** @brief Operation when counter-clockwise stencil passes and depth fails. */
    CNA_StencilOperation counter_clockwise_stencil_depth_buffer_fail;
    /** @brief Operation when counter-clockwise stencil and depth pass. */
    CNA_StencilOperation counter_clockwise_stencil_pass;
    /** @brief Reserved for future use; must be zero. */
    uint32_t reserved;
} CNA_DepthStencilState;

/** @brief Complete mutable RasterizerState value represented as a versioned C POD. */
typedef struct CNA_RasterizerState {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Face-culling mode. */
    CNA_CullMode cull_mode;
    /** @brief Polygon fill mode. */
    CNA_FillMode fill_mode;
    /** @brief Constant depth bias. */
    float depth_bias;
    /** @brief Slope-scaled depth bias. */
    float slope_scale_depth_bias;
    /** @brief Whether multisample antialiasing is enabled. */
    CNA_Bool multi_sample_anti_alias;
    /** @brief Whether scissor testing is enabled. */
    CNA_Bool scissor_test_enable;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved[2];
} CNA_RasterizerState;

/** @brief Complete mutable SamplerState value represented as a versioned C POD. */
typedef struct CNA_SamplerState {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief U-coordinate address mode. */
    CNA_TextureAddressMode address_u;
    /** @brief V-coordinate address mode. */
    CNA_TextureAddressMode address_v;
    /** @brief W-coordinate address mode. */
    CNA_TextureAddressMode address_w;
    /** @brief Texture filtering mode. */
    CNA_TextureFilter filter;
    /** @brief Maximum anisotropy. */
    int32_t max_anisotropy;
    /** @brief Maximum mip level. */
    int32_t max_mip_level;
    /** @brief Mipmap level-of-detail bias. */
    float mip_map_level_of_detail_bias;
    /** @brief Reserved for future use; must be zero. */
    uint32_t reserved;
} CNA_SamplerState;

/**
 * @brief Initializes a BlendState descriptor from a native preset.
 *
 * @param preset One of the `CNA_BLEND_STATE_PRESET_*` identities.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_blend_state_init(
    CNA_BlendStatePreset preset,
    CNA_BlendState* out_state);

/**
 * @brief Initializes a DepthStencilState descriptor from a native preset.
 *
 * @param preset One of the `CNA_DEPTH_STENCIL_STATE_PRESET_*` identities.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_depth_stencil_state_init(
    CNA_DepthStencilStatePreset preset,
    CNA_DepthStencilState* out_state);

/**
 * @brief Initializes a RasterizerState descriptor from a native preset.
 *
 * @param preset One of the `CNA_RASTERIZER_STATE_PRESET_*` identities.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_rasterizer_state_init(
    CNA_RasterizerStatePreset preset,
    CNA_RasterizerState* out_state);

/**
 * @brief Initializes a SamplerState descriptor from a native preset.
 *
 * @param preset One of the `CNA_SAMPLER_STATE_PRESET_*` identities.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_sampler_state_init(
    CNA_SamplerStatePreset preset,
    CNA_SamplerState* out_state);

/**
 * @brief Gets the active graphics-device blend state.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_blend_state(
    CNA_Handle graphics_device,
    CNA_BlendState* out_state);

/**
 * @brief Applies a complete blend state to the graphics device.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param state Caller-owned versioned descriptor copied during the call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_blend_state(
    CNA_Handle graphics_device,
    const CNA_BlendState* state);

/**
 * @brief Gets the active graphics-device depth/stencil state.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_depth_stencil_state(
    CNA_Handle graphics_device,
    CNA_DepthStencilState* out_state);

/**
 * @brief Applies a complete depth/stencil state to the graphics device.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param state Caller-owned versioned descriptor copied during the call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_depth_stencil_state(
    CNA_Handle graphics_device,
    const CNA_DepthStencilState* state);

/**
 * @brief Gets the active graphics-device rasterizer state.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_rasterizer_state(
    CNA_Handle graphics_device,
    CNA_RasterizerState* out_state);

/**
 * @brief Applies a complete rasterizer state to the graphics device.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param state Caller-owned versioned descriptor copied during the call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_rasterizer_state(
    CNA_Handle graphics_device,
    const CNA_RasterizerState* state);

/**
 * @brief Gets one entry from a pixel- or vertex-shader sampler collection.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param stage `CNA_SHADER_STAGE_PIXEL` or `CNA_SHADER_STAGE_VERTEX`.
 * @param slot Sampler slot in the range zero through `CNA_MAX_SAMPLERS - 1`.
 * @param out_state Receives a complete version-one descriptor.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_sampler_state(
    CNA_Handle graphics_device,
    CNA_ShaderStage stage,
    uint32_t slot,
    CNA_SamplerState* out_state);

/**
 * @brief Replaces one entry in a pixel- or vertex-shader sampler collection.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param stage `CNA_SHADER_STAGE_PIXEL` or `CNA_SHADER_STAGE_VERTEX`.
 * @param slot Sampler slot in the range zero through `CNA_MAX_SAMPLERS - 1`.
 * @param state Caller-owned versioned descriptor copied during the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_sampler_state(
    CNA_Handle graphics_device,
    CNA_ShaderStage stage,
    uint32_t slot,
    const CNA_SamplerState* state);

/**
 * @brief Begins a SpriteBatch interval with explicit blend, sampler, depth and rasterizer states.
 *
 * @param sprite_batch Owned SpriteBatch handle.
 * @param sort_mode Native SpriteBatch ordering mode.
 * @param blend_state Required complete blend-state descriptor.
 * @param sampler_state Required complete sampler-state descriptor.
 * @param depth_stencil_state Required complete depth/stencil-state descriptor.
 * @param rasterizer_state Required complete rasterizer-state descriptor.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE`, `CNA_RESULT_NOT_SUPPORTED`, or another
 * documented argument/handle/thread/native failure.
 *
 * This maps the state-bearing Begin overload without exposing a C++ Effect or Matrix. Those
 * parameters remain owned by CBIND-035.
 */
CNA_C_API CNA_Result cna_sprite_batch_begin_with_states(
    CNA_Handle sprite_batch,
    CNA_SpriteSortMode sort_mode,
    const CNA_BlendState* blend_state,
    const CNA_SamplerState* sampler_state,
    const CNA_DepthStencilState* depth_stencil_state,
    const CNA_RasterizerState* rasterizer_state);

#ifdef __cplusplus
}
#endif

#endif
