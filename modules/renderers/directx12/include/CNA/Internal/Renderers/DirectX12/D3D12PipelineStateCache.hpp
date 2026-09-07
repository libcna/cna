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
// plans/plan_dx.md DX-202 closed the stencil half of that gap: every field of XNA's DepthStencilState
// except ReferenceStencil is now part of this key and of the D3D12_DEPTH_STENCIL_DESC below.
// ReferenceStencil is deliberately NOT here, and that is not an omission: it is not part of
// D3D12_DEPTH_STENCIL_DESC at all, it is an argument to OMSetStencilRef() at record time -- the
// same split D3D11DepthStencilStateCache already documents for OMSetDepthStencilState() (DX-203).
// Scissor-enable is still out; that is DX-201.

#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <map>
#include <tuple>
#include <utility>

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

        // DX-202: the stencil half, same raw-XNA-ordinal convention as everything above and the
        // same field set D3D11DepthStencilStateCache::GetOrCreate already takes. Defaults are XNA's
        // own DepthStencilState.Default: stencil off, CompareFunction::Always (ordinal 0),
        // StencilOperation::Keep (ordinal 0), full 8-bit read/write masks, single-sided.
        bool stencilEnable = false;
        int stencilFunc = 0;        // CompareFunction::Always
        int stencilPass = 0;        // StencilOperation::Keep
        int stencilFail = 0;        // StencilOperation::Keep
        int stencilDepthFail = 0;   // StencilOperation::Keep
        int stencilMask = 0xFF;
        int stencilWriteMask = 0xFF;
        bool twoSidedStencilMode = false;
        int ccwStencilFunc = 0;
        int ccwStencilPass = 0;
        int ccwStencilFail = 0;
        int ccwStencilDepthFail = 0;

        // Rasterizer (D3DStateMapping::CullModeToD3D11 / FillModeToD3D11 ordinals).
        int cullMode = 2;  // CullMode::CullCounterClockwiseFace (XNA's own RasterizerState.CullCounterClockwise default)
        int fillMode = 0;  // FillMode::Solid
        // plans/plan_dx.md DX-206/DX-256: RasterizerState.DepthBias / SlopeScaleDepthBias. The
        // renderer converts XNA's normalized constant offset to the bound DSV format's native INT
        // units before filling this descriptor. The converted integer participates in the key, so
        // equivalent requests share a pipeline state instead of keying on float noise.
        int depthBias = 0;
        float slopeScaleDepthBias = 0.0f;
        // RasterizerState.ScissorTestEnable is deliberately NOT here: D3D12_RASTERIZER_DESC has no
        // ScissorEnable field (the scissor test is always on), so on this API a disabled test means
        // "the rectangle covers the whole target" -- command-list state, not pipeline state. See
        // DirectX12Renderer::GetEffectiveScissorEXT (DX-201).

        // REMED-GFX-077: BlendState output-merger write state. Both are STATIC parts of the D3D12
        // PSO (RenderTarget[0].RenderTargetWriteMask and D3D12_GRAPHICS_PIPELINE_STATE_DESC::
        // SampleMask), so both participate in the PSO cache key. D3D12 draws are single-target here
        // (no CNA shader emits >1 SV_Target), so only ColorWriteChannels slot 0 applies. XNA
        // ColorWriteChannels (R=1,G=2,B=4,A=8) is bit-identical to D3D12_COLOR_WRITE_ENABLE_*.
        int colorWriteMask = 15;             // ColorWriteChannels.All
        unsigned int sampleMask = 0xFFFFFFFFu; // MultiSampleMask == -1 (all samples)
        // plans/plan_dx.md DX-207: the bound render target's real sample count. D3D12 requires a pipeline
        // state's SampleDesc.Count to MATCH the render target it is used with -- unlike D3D11, which
        // has no such coupling -- so a hardcoded 1 makes every draw into a multisampled
        // RenderTarget2D/RenderTargetCube illegal, however correctly that target was created. 1 is
        // the non-MSAA case and keeps every existing single-sample PSO byte-identical.
        unsigned int sampleCount = 1;
        // plans/plan_dx.md DX-208: the pipeline state's primitive topology CLASS. D3D12 bakes this in and
        // requires it to agree with the topology the command list sets, which is the whole reason
        // LineList/LineStrip/PointListEXT used to throw here. TRIANGLE keeps every existing
        // pipeline state byte-identical.
        int topologyType = static_cast<int>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

        /// DX-202: every field of this struct, by value, as the cache key. Adding a field to the
        /// struct and forgetting it here is the one mistake that would make two different pipeline
        /// states share one cached PSO, so this list and the field list above are kept adjacent
        /// deliberately.
        [[nodiscard]] std::tuple<int, std::size_t,
                                 int, int, int, int, int, int,
                                 bool, bool, int,
                                 bool, int, int, int, int, int, int,
                                 bool, int, int, int, int,
                                 int, int, int, float,
                                 int, unsigned, unsigned, int> AsCacheKeyEXT() const
        {
            return std::make_tuple(static_cast<int>(variant), strideInBytes,
                                   colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                                   colorBlendFunc, alphaBlendFunc,
                                   depthEnable, depthWriteEnable, depthFunc,
                                   stencilEnable, stencilFunc, stencilPass, stencilFail,
                                   stencilDepthFail, stencilMask, stencilWriteMask,
                                   twoSidedStencilMode, ccwStencilFunc, ccwStencilPass,
                                   ccwStencilFail, ccwStencilDepthFail,
                                   cullMode, fillMode, depthBias, slopeScaleDepthBias,
                                   colorWriteMask, sampleMask, sampleCount, topologyType);
        }
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
        // DX-202: the key is derived from the desc rather than re-listed field by field. A hand-written
        // tuple type plus a hand-written brace initialiser is two places to forget a new field, and
        // forgetting one there is silent -- two genuinely different pipeline states would collide on
        // one cache entry and the second draw would quietly get the first one's state. AsCacheKeyEXT()
        // is the single list; the render-target formats are appended here because they are arguments
        // to GetOrCreate(), not properties of the desc.
        using Key = decltype(std::tuple_cat(
            std::declval<const D3D12PipelineStateDesc&>().AsCacheKeyEXT(),
            std::make_tuple(0u, 0u)));
        std::map<Key, ComPtr<ID3D12PipelineState>> cache_;
    };
}
