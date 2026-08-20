#pragma once

// plans/plan_dx.md Phase DIRECTX3 (DX-12-state): XNA Blend/BlendFunction/CompareFunction/CullMode/FillMode/
// TextureAddressMode/TextureFilter -> D3D11_* equivalents, shared between D3D11 and D3D12 (design
// decision 4).
//
// design decision 4's own caveat: D3D11 and D3D12 use numerically identical enum values for most
// of these (D3D12 reused D3D11's own constants) -- VERIFIED, not assumed, by direct inspection of
// both SDK headers on this machine (d3d11.h vs d3d12.h): D3D11_BLEND/D3D12_BLEND,
// D3D11_BLEND_OP/D3D12_BLEND_OP, D3D11_COMPARISON_FUNC/D3D12_COMPARISON_FUNC,
// D3D11_CULL_MODE/D3D12_CULL_MODE, D3D11_FILL_MODE/D3D12_FILL_MODE, and
// D3D11_TEXTURE_ADDRESS_MODE/D3D12_TEXTURE_ADDRESS_MODE all share identical enumerator values one
// for one. D3D11_FILTER/D3D12_FILTER also match. This header therefore returns the D3D11-prefixed
// enum types; a future D3D12 consumer (Phase DX12) can `static_cast` directly rather than needing
// a second, duplicate table -- re-verify this claim against whatever SDK/MinGW header versions
// Phase DX12 actually builds against before relying on the static_cast, per this plan's own
// Boundaries section.

#include <d3d11.h>

namespace CNA::Internal::Renderers::D3DCommon
{
    /// Maps an XNA Blend ordinal (Microsoft::Xna::Framework::Graphics::Blend cast to int) to the
    /// corresponding D3D11_BLEND. Returns D3D11_BLEND_ONE for an unrecognized ordinal.
    D3D11_BLEND BlendToD3D11(int blend);

    /// Maps an XNA BlendFunction ordinal to the corresponding D3D11_BLEND_OP. Returns
    /// D3D11_BLEND_OP_ADD for an unrecognized ordinal.
    D3D11_BLEND_OP BlendFunctionToD3D11(int blendFunction);

    /// Maps an XNA CompareFunction ordinal to the corresponding D3D11_COMPARISON_FUNC. Returns
    /// D3D11_COMPARISON_ALWAYS for an unrecognized ordinal.
    D3D11_COMPARISON_FUNC CompareFunctionToD3D11(int compareFunction);

    /// Maps an XNA CullMode ordinal to the corresponding D3D11_CULL_MODE. Assumes
    /// D3D11_RASTERIZER_DESC::FrontCounterClockwise = FALSE (D3D11's own default, matching D3D's
    /// native clockwise-is-front convention -- unlike this project's Vulkan renderer, which must
    /// explicitly override VkPipelineRasterizationStateCreateInfo::frontFace to CLOCKWISE since
    /// Vulkan/GL's own native default is counter-clockwise-is-front). With that default,
    /// CullClockwiseFace culls the front (clockwise) faces -> D3D11_CULL_FRONT, and
    /// CullCounterClockwiseFace culls the back (counter-clockwise) faces -> D3D11_CULL_BACK.
    D3D11_CULL_MODE CullModeToD3D11(int cullMode);

    /// Maps an XNA FillMode ordinal to the corresponding D3D11_FILL_MODE.
    D3D11_FILL_MODE FillModeToD3D11(int fillMode);

    /// Maps an XNA TextureAddressMode ordinal to the corresponding D3D11_TEXTURE_ADDRESS_MODE.
    D3D11_TEXTURE_ADDRESS_MODE TextureAddressModeToD3D11(int addressMode);

    /// Maps an XNA TextureFilter ordinal to the corresponding D3D11_FILTER. XNA's own TextureFilter
    /// enumerator names were themselves modeled after D3D's min/mag/mip filter naming convention,
    /// so this is a direct, unambiguous one-to-one mapping (no derived/composed bit twiddling).
    D3D11_FILTER TextureFilterToD3D11(int textureFilter);

    /// Maps an XNA StencilOperation ordinal (Microsoft::Xna::Framework::Graphics::StencilOperation
    /// cast to int) to the corresponding D3D11_STENCIL_OP. XNA's Increment/Decrement (wrapping) map
    /// to D3D11_STENCIL_OP_INCR/DECR; XNA's IncrementSaturation/DecrementSaturation (clamping) map
    /// to D3D11_STENCIL_OP_INCR_SAT/DECR_SAT -- these are two genuinely distinct D3D11 ops, not
    /// interchangeable. Returns D3D11_STENCIL_OP_KEEP for an unrecognized ordinal (Phase DIRECTX7,
    /// DX-51 -- used by Phase DIRECTX3's D3D12 consumer too, design decision 4: D3D11_STENCIL_OP and
    /// D3D12_STENCIL_OP share identical enumerator values, verified against both SDK headers).
    D3D11_STENCIL_OP StencilOperationToD3D11(int stencilOperation);
}
