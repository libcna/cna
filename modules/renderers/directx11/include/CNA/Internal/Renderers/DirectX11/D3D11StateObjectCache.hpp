#pragma once

// plans/plan_dx.md Phase DIRECTX7 (DX-50/DX-51/DX-52): real ID3D11BlendState/ID3D11DepthStencilState/
// ID3D11RasterizerState creation + caching from the raw XNA-level ordinals IGraphicsRenderer's
// ApplyBlendState/ApplyDepthStencilState/ApplyRasterizerState already carry, via D3DCommon's
// DX-12-state/this phase's own DX-51 StencilOperationToD3D11 mapping tables -- same per-distinct-
// XNA-state caching discipline as D3D11InputLayoutCache (DX-32) / D3D11SamplerCache (DX-44), so
// repeat Apply*State() calls with identical XNA-level state reuse one GPU object instead of
// creating a fresh one every draw.

#include <d3d11.h>
#include <wrl/client.h>

#include <map>
#include <tuple>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    /// Caches ID3D11BlendState objects keyed by the 6 raw XNA Blend/BlendFunction ordinals
    /// ApplyBlendState() carries PLUS the four per-render-target colour write masks
    /// (BlendState.ColorWriteChannels/1/2/3, REMED-GFX-077). The write mask is part of the
    /// ID3D11BlendState object (static), so it participates in the key; the multisample SampleMask
    /// is NOT part of the object (it is a dynamic OMSetBlendState argument) and therefore stays out
    /// of the key.
    class D3D11BlendStateCache
    {
    public:
        /// Returns a cached (or newly created) blend state for the given raw XNA Blend/
        /// BlendFunction ordinals. BlendEnable is derived: disabled only for the exact
        /// Blend::One/Blend::Zero "Opaque" combination on both color and alpha channels (matches
        /// this project's own already-established VulkanRenderer::ApplyBlendState
        /// heuristic), enabled otherwise -- mathematically equivalent either way, but avoids
        /// paying the (tiny) blend-enabled cost for the common Opaque case.
        /// @param cw0..cw3 Raw XNA ColorWriteChannels for render targets 0..3 (REMED-GFX-077). When
        ///                 cw1==cw2==cw3==cw0 the blend uses a single RenderTarget[0] entry
        ///                 (IndependentBlendEnable=FALSE); differing masks enable independent
        ///                 per-target write masks across RenderTarget[0..3].
        ComPtr<ID3D11BlendState> GetOrCreate(ID3D11Device* device,
                                             int colorSrcBlend, int alphaSrcBlend,
                                             int colorDstBlend, int alphaDstBlend,
                                             int colorBlendFunc, int alphaBlendFunc,
                                             int cw0, int cw1, int cw2, int cw3);

        /// Number of distinct blend states created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        using Key = std::tuple<int, int, int, int, int, int, int, int, int, int>;
        std::map<Key, ComPtr<ID3D11BlendState>> cache_;
    };

    /// Caches ID3D11DepthStencilState objects keyed by every field ApplyDepthStencilState()
    /// carries EXCEPT referenceStencil -- the stencil reference value is not part of
    /// D3D11_DEPTH_STENCIL_DESC, it is a separate argument to OMSetDepthStencilState() applied at
    /// bind time (see DirectX11Renderer::SetReferenceStencil, which re-binds the current cached
    /// state object with a new reference value without needing a new object).
    class D3D11DepthStencilStateCache
    {
    public:
        ComPtr<ID3D11DepthStencilState> GetOrCreate(
            ID3D11Device* device,
            bool depthEnable, bool depthWriteEnable, int depthFunc,
            bool stencilEnable, int stencilFunc, int stencilPass, int stencilFail, int stencilDepthFail,
            int stencilMask, int stencilWriteMask,
            bool twoSidedStencilMode,
            int ccwStencilFunc, int ccwStencilPass, int ccwStencilFail, int ccwStencilDepthFail);

        /// Number of distinct depth-stencil states created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        using Key = std::tuple<bool, bool, int, bool, int, int, int, int, int, int, bool, int, int, int, int>;
        std::map<Key, ComPtr<ID3D11DepthStencilState>> cache_;
    };

    /// Caches ID3D11RasterizerState objects keyed by ApplyRasterizerState()'s 5 fields.
    /// DepthBias is rounded to the nearest D3D11_RASTERIZER_DESC::DepthBias INT (see .cpp for the
    /// unit-convention note); SlopeScaleDepthBias maps directly to the FLOAT
    /// D3D11_RASTERIZER_DESC::SlopeScaledDepthBias field, no conversion needed.
    class D3D11RasterizerStateCache
    {
    public:
        ComPtr<ID3D11RasterizerState> GetOrCreate(ID3D11Device* device,
                                                   int cullMode, int fillMode,
                                                   bool scissorTestEnable,
                                                   float depthBias, float slopeScaleDepthBias);

        /// Number of distinct rasterizer states created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        using Key = std::tuple<int, int, bool, int, float>;
        std::map<Key, ComPtr<ID3D11RasterizerState>> cache_;
    };
}
