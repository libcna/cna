#pragma once

// plans/plan_dx.md Phase DIRECTX3 (DX-12-state): XNA Blend/BlendFunction/CompareFunction/CullMode/FillMode/
// TextureAddressMode/TextureFilter -> D3D11_* equivalents (design decision 4).

#include <d3d11.h>

#include <cstdint>

namespace CNA::Internal::Renderers::D3DCommon
{
    /// Maps an XNA Blend ordinal (Microsoft::Xna::Framework::Graphics::Blend cast to int) to the
    /// corresponding D3D11_BLEND. Returns D3D11_BLEND_ONE for an unrecognized ordinal.
    D3D11_BLEND BlendToD3D11(int blend);

    /// Maps an XNA alpha blend factor to a D3D11 scalar alpha factor. Color factors use their
    /// alpha component, and SourceAlphaSaturation has an alpha component of one.
    D3D11_BLEND AlphaBlendToD3D11(int blend);

    /// Maps an XNA BlendFunction ordinal to the corresponding D3D11_BLEND_OP. Returns
    /// D3D11_BLEND_OP_ADD for an unrecognized ordinal.
    D3D11_BLEND_OP BlendFunctionToD3D11(int blendFunction);

    /// Maps an XNA CompareFunction ordinal to the corresponding D3D11_COMPARISON_FUNC. Returns
    /// D3D11_COMPARISON_ALWAYS for an unrecognized ordinal.
    D3D11_COMPARISON_FUNC CompareFunctionToD3D11(int compareFunction);

    /// Maps an XNA CullMode ordinal to the corresponding D3D11_CULL_MODE. Assumes
    /// D3D11_RASTERIZER_DESC::FrontCounterClockwise = TRUE, matching FNA3D's D3D11 convention.
    /// This keeps native FrontFace/BackFace aligned with XNA's ordinary/CounterClockwise stencil
    /// operation sets instead of compensating only the cull mode and silently swapping stencil.
    D3D11_CULL_MODE CullModeToD3D11(int cullMode);

    /// Maps an XNA FillMode ordinal to the corresponding D3D11_FILL_MODE.
    D3D11_FILL_MODE FillModeToD3D11(int fillMode);

    /// Maps an XNA TextureAddressMode ordinal to the corresponding D3D11_TEXTURE_ADDRESS_MODE.
    D3D11_TEXTURE_ADDRESS_MODE TextureAddressModeToD3D11(int addressMode);

    /// Maps an XNA TextureFilter ordinal to the corresponding D3D11_FILTER. XNA's own TextureFilter
    /// enumerator names were themselves modeled after D3D's min/mag/mip filter naming convention,
    /// so this is a direct, unambiguous one-to-one mapping (no derived/composed bit twiddling).
    D3D11_FILTER TextureFilterToD3D11(int textureFilter);

    /**
     * @brief Converts XNA's normalized constant depth bias to a native D3D integer bias.
     *
     * XNA/D3D9 stores `RasterizerState.DepthBias` as a normalized depth offset. D3D11 instead
     * stores an integer count of the active depth format's least-resolvable value.
     *
     * @param depthBias XNA normalized depth offset.
     * @param depthFormat Native format of the active depth-stencil view.
     * @return Native D3D depth-bias units, or zero when no depth format is active.
     */
    [[nodiscard]] std::int32_t XnaDepthBiasToD3D(float depthBias, DXGI_FORMAT depthFormat) noexcept;

    /// Maps an XNA StencilOperation ordinal (Microsoft::Xna::Framework::Graphics::StencilOperation
    /// cast to int) to the corresponding D3D11_STENCIL_OP. XNA's Increment/Decrement (wrapping) map
    /// to D3D11_STENCIL_OP_INCR/DECR; XNA's IncrementSaturation/DecrementSaturation (clamping) map
    /// to D3D11_STENCIL_OP_INCR_SAT/DECR_SAT -- these are two genuinely distinct D3D11 ops, not
    /// interchangeable. Returns D3D11_STENCIL_OP_KEEP for an unrecognized ordinal (Phase DIRECTX7,
    /// DX-51).
    D3D11_STENCIL_OP StencilOperationToD3D11(int stencilOperation);
}
