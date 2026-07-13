#pragma once

// plan_dx.md Phase DX12 (DX-108): D3D12 root signatures -- the binding-layout declaration a
// D3D12_GRAPHICS_PIPELINE_STATE_DESC needs before a PSO can be created (DX-107). Reuses D3D11's own
// D3DPerDrawConstants/D3DFogConstants/D3DLightingConstants/etc. struct layouts from D3DCommon
// (DX-60/DX-60a) as-is -- this class only declares the *binding shape* (how many CBVs/SRVs/samplers,
// at which registers), not the byte layout of what's inside a constant buffer, which is already
// solved and shared (design decision 4).
//
// Register scheme matches DX-13-hlsl's HLSL shaders exactly (same compiled DXBC bytecode is reused
// verbatim from D3D11's own D3DShaderCache, per design decision 5's "same source" framing): CBVs at
// b0/b1/b2, textures at t0/t1, samplers at s0/s1. Every root signature built here binds its CBVs as
// root descriptors (D3D12_ROOT_PARAMETER_TYPE_CBV) with D3D12_SHADER_VISIBILITY_ALL -- this
// project's own HLSL shaders share PerDraw/FogParams-style cbuffers between the vertex and pixel
// stage (confirmed by reading the actual .hlsl source: colored3d.vert.hlsl declares PerDraw@b0/
// FogParams@b1, colored3d.frag.hlsl separately declares FogParams@b1 too), so a single ALL-visibility
// root CBV per register correctly satisfies both stages without needing per-stage-specific
// visibility tuning.
//
// Textures/samplers use a descriptor table (SRVs) + D3D12_STATIC_SAMPLER_DESC entries -- static
// samplers are a deliberate scope choice: they need no SAMPLER descriptor heap at all (DX-103 only
// built RTV/DSV/CBV_SRV_UAV heaps, no SAMPLER heap), and every stock effect variant's sampler state
// is fixed at shader-authoring time anyway (this project's own D3D11SamplerCache-driven dynamic
// sampler binding is a D3D11-specific convenience, not an XNA-level requirement D3D12 must match
// bit-for-bit here) -- if a later task needs genuinely dynamic per-draw sampler state, that's real,
// scoped follow-up work (a SAMPLER descriptor heap + non-static samplers), not something this cache
// silently forecloses.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <map>
#include <tuple>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    /// Caches ID3D12RootSignature objects keyed by (numCbvs, numSrvs, numSamplers) -- the binding
    /// *shape* a given shader-variant family needs (DX-108's own "one per shader-variant family"
    /// framing: colored3d's family needs (2,0,0) [PerDraw@b0, FogParams@b1], textured3d's family
    /// needs (2,1,1) [+ t0/s0], lit_textured3d needs (2,0,0) too since it folds lighting into the
    /// same 2-CBV shape via a different byte layout inside the same b0/b1 registers -- the binding
    /// *shape*, not the per-family byte layout, is what determines root-signature reuse). Any two
    /// variants with the same (numCbvs, numSrvs, numSamplers) shape genuinely share one root
    /// signature, matching the D3D12 root-signature's own real constraint: it only describes
    /// binding slots, not cbuffer contents.
    class D3D12RootSignatureCache
    {
    public:
        /// Returns a cached (or newly created) root signature with @p numCbvs root CBV descriptors
        /// at b0..b(numCbvs-1) (ALL-visibility), plus -- only if numSrvs > 0 -- one descriptor table
        /// with @p numSrvs SRVs at t0..t(numSrvs-1) (PIXEL-visibility, matching this project's own
        /// "textures are always sampled in the pixel shader" convention) and @p numSamplers static
        /// samplers at s0..s(numSamplers-1) using a fixed linear-wrap description (a reasonable
        /// stock-effect default; this is the same scope boundary the class-level doc comment already
        /// documents). Returns a null ComPtr (does not throw) if D3D12SerializeRootSignature or
        /// CreateRootSignature fails, matching this project's established D3DShaderCache/
        /// D3D11*Cache error-handling convention (callers check the returned ComPtr).
        ComPtr<ID3D12RootSignature> GetOrCreate(ID3D12Device* device, int numCbvs, int numSrvs, int numSamplers);

        /// Number of distinct root-signature shapes created so far (NOXNA diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        using Key = std::tuple<int, int, int>;
        std::map<Key, ComPtr<ID3D12RootSignature>> cache_;
    };
}
