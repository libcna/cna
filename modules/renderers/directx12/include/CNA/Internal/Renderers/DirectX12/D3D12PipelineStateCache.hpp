#pragma once

// plans/plan_dx.md Phase DX12 (DX-107): pipeline state objects (PSOs). Bootstraps directly off D3D11's own
// already-solved pieces (design decision 4/5): D3DShaderCache's checked-in DXBC bytecode
// (DX-14-compile -- the *same* compiled bytes D3D11 uses, PSOs accept DXBC directly, no DXIL
// requirement), D3DVertexFormatHelper's stride-keyed D3D12_INPUT_ELEMENT_DESC arrays (DX-16-vtx's
// D3D12 counterpart, added alongside this task), and D3DStateMapping's XNA->D3D11_* enum tables,
// static_cast to the D3D12_* equivalent per that header's own verified "identical enum values"
// claim.
//
// Caching/hashing strategy (this task's own explicitly-flagged design question, decided here): one
// PSO per full (shader variant, vertex stride, blend 6-tuple, depth-stencil 3-tuple, rasterizer
// 2-tuple) key, mirroring D3D11's own per-object state caches (D3D11BlendStateCache/
// D3D11DepthStencilStateCache/D3D11RasterizerStateCache) but merged into ONE composite cache key --
// unlike D3D11, a D3D12 PSO bakes shader+input-layout+blend+depth-stencil+rasterizer state into one
// indivisible object, so there is no meaningful way to cache the pieces independently and combine
// them at bind time the way D3D11's OMSetBlendState/OMSetDepthStencilState/RSSetState do. This is
// the real "PSO explosion" tradeoff the plan's own row flags -- accepted here as the correct
// first-implementation strategy; a future task MAY introduce a smaller/derived key (e.g. hashing
// only the fields that differ from a per-variant default) if the full-tuple key's cache-object count
// becomes a real, measured problem, but that is not assumed to be true yet.
//
// Stencil state and scissor-enable are deliberately NOT part of this first key/desc (matches the
// concrete-first-implementation scope this and other DX-12x rows explicitly allow) -- a documented,
// honest gap, not silently dropped.

#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <map>
#include <tuple>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;
    using CNA::Internal::Renderers::D3DCommon::D3DShaderVariant;

    /// The subset of D3D11_RASTERIZER_DESC/D3D11_BLEND_DESC/D3D11_DEPTH_STENCIL_DESC fields this
    /// first PSO cache covers -- raw XNA-level ordinals, exactly matching the parameter shapes
    /// D3D11BlendStateCache::GetOrCreate/D3D11DepthStencilStateCache::GetOrCreate (minus stencil)/
    /// D3D11RasterizerStateCache::GetOrCreate already established, so a future caller wiring real
    /// BlendState/DepthStencilState/RasterizerState XNA objects into this cache (DX-109 onward) can
    /// reuse the exact same field-extraction logic those D3D11 caches already use.
    struct D3D12PipelineStateDesc
    {
        D3DShaderVariant variant = D3DShaderVariant::Colored3d;
        std::size_t strideInBytes = 16;

        // Blend (D3DStateMapping::BlendToD3D11 / BlendFunctionToD3D11 ordinals -- raw
        // Microsoft::Xna::Framework::Graphics enum ordinals, fed through D3DStateMapping's own
        // XxxToD3D11() functions in the .cpp; NOT already-mapped D3D11_BLEND/D3D11_COMPARISON_FUNC
        // native values, despite the numeric coincidences that made an earlier version of this
        // comment block mislabel colorSrcBlend/alphaSrcBlend's default as `2` -- Blend::One's real
        // XNA ordinal is 0 (Blend.hpp: One, Zero, SourceColor, ...); `2` is Blend::SourceColor.
        // Confirmed functionally inert while it stood: DeriveBlendEnable()'s own opaque-detection
        // check used the same (wrong but internally self-consistent) `2` literal, and BlendEnable
        // ends up FALSE for the default/opaque case either way, which makes D3D12 ignore
        // SrcBlend/DestBlend entirely -- but a real BlendState explicitly requesting Blend::One
        // (ordinal 0, via GraphicsDevice::setBlendStateProperty's real (int)Blend cast) always
        // bypassed these defaults through ApplyBlendState() correctly regardless.
        int colorSrcBlend = 0;   // Blend::One
        int alphaSrcBlend = 0;   // Blend::One
        int colorDstBlend = 1;   // Blend::Zero
        int alphaDstBlend = 1;   // Blend::Zero
        int colorBlendFunc = 0;  // BlendFunction::Add
        int alphaBlendFunc = 0;  // BlendFunction::Add

        // Depth (D3DStateMapping::CompareFunctionToD3D11 ordinal -- same raw-XNA-ordinal
        // convention as the blend fields above). CompareFunction::LessEqual's real XNA ordinal is
        // 3 (CompareFunction.hpp: Always, Never, Less, LessEqual, ...).
        bool depthEnable = true;
        bool depthWriteEnable = true;
        int depthFunc = 3; // CompareFunction::LessEqual (XNA's own DepthStencilState.Default)

        // Rasterizer (D3DStateMapping::CullModeToD3D11 / FillModeToD3D11 ordinals).
        int cullMode = 2;  // CullMode::CullCounterClockwiseFace (XNA's own RasterizerState.CullCounterClockwise default)
        int fillMode = 0;  // FillMode::Solid

        // REMED-GFX-077: BlendState output-merger write state. Both are STATIC parts of the D3D12
        // PSO (RenderTarget[0].RenderTargetWriteMask and D3D12_GRAPHICS_PIPELINE_STATE_DESC::
        // SampleMask), so both participate in the PSO cache key. D3D12 draws are single-target here
        // (no CNA shader emits >1 SV_Target), so only ColorWriteChannels slot 0 applies. XNA
        // ColorWriteChannels (R=1,G=2,B=4,A=8) is bit-identical to D3D12_COLOR_WRITE_ENABLE_*.
        int colorWriteMask = 15;             // ColorWriteChannels.All
        unsigned int sampleMask = 0xFFFFFFFFu; // MultiSampleMask == -1 (all samples)
    };

    /// Caches ID3D12PipelineState (graphics) objects keyed by D3D12PipelineStateDesc's full field
    /// tuple (see class-level -- er, file-level -- doc comment for the caching strategy rationale).
    class D3D12PipelineStateCache
    {
    public:
        /// Returns a cached (or newly created) graphics PSO for @p desc, built from
        /// D3DShaderCache's checked-in DXBC bytecode for @p desc.variant, D3DVertexFormatHelper's
        /// D3D12 input layout for @p desc.strideInBytes, and @p desc's blend/depth/rasterizer
        /// fields mapped through D3DStateMapping. @p rootSignature must already be a real, live
        /// object (see D3D12RootSignatureCache) -- this cache does not create root signatures
        /// itself. @p rtvFormat/@p dsvFormat describe the render target(s)/depth-stencil buffer
        /// this PSO will be used against (D3D12 bakes these into the PSO, unlike D3D11's dynamic
        /// OMSetRenderTargets binding) -- pass DXGI_FORMAT_UNKNOWN for @p dsvFormat if no depth
        /// buffer is bound.
        ///
        /// Returns a null ComPtr (does not throw) if the shader variant's DXBC bytecode is missing,
        /// the stride isn't one of the 5 established layouts, or CreateGraphicsPipelineState()
        /// itself fails -- callers check the returned ComPtr, matching this project's established
        /// D3DShaderCache/D3D11*Cache error-handling convention.
        ComPtr<ID3D12PipelineState> GetOrCreate(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                                 const D3D12PipelineStateDesc& desc,
                                                 DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);

        /// Number of distinct PSOs created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        using Key = std::tuple<int, std::size_t,
                               int, int, int, int, int, int,
                               bool, bool, int,
                               int, int,
                               int, unsigned,          // + colorWriteMask, sampleMask (REMED-GFX-077)
                               unsigned, unsigned>; // + rtvFormat, dsvFormat
        std::map<Key, ComPtr<ID3D12PipelineState>> cache_;
    };
}
