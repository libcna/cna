#pragma once

// plans/plan_dx.md Phase DX12 (DX-108): D3D12 root signatures -- the binding-layout declaration a
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
// Textures/samplers use one descriptor table per texture register (t0, t1, ... -- NOT one shared
// multi-descriptor table) + D3D12_STATIC_SAMPLER_DESC entries -- static samplers are a deliberate
// scope choice: they need no SAMPLER descriptor heap at all (DX-103 only built RTV/DSV/CBV_SRV_UAV
// heaps, no SAMPLER heap), and every stock effect variant's sampler state is fixed at shader-
// authoring time anyway (this project's own D3D11SamplerCache-driven dynamic sampler binding is a
// D3D11-specific convenience, not an XNA-level requirement D3D12 must match bit-for-bit here) -- if
// a later task needs genuinely dynamic per-draw sampler state, that's real, scoped follow-up work
// (a SAMPLER descriptor heap + non-static samplers), not something this cache silently forecloses.
//
// N SEPARATE single-descriptor tables, not one N-descriptor table -- a real, empirically-found
// dev-loop limitation (DX-111, landing dual_texture3d): a single D3D12_DESCRIPTOR_TABLE range with
// NumDescriptors>1, populated via a fresh per-draw CopyDescriptorsSimple into a contiguous bump-
// allocated heap slot pair, sampled as all-zero under this machine's Wine+vkd3d-proton dev loop
// (DX-100) even though the CPU-side descriptor writes/handles were independently verified correct
// (heap offsets incremented by exactly one descriptor stride, both CopyDescriptorsSimple calls
// succeeded with sane handles). Switching to N separate 1-descriptor tables -- each texture's own
// permanent SRV (created once, at texture-construction time) bound directly to its own root
// parameter, no per-draw descriptor copy at all -- fixed it immediately and is simpler code besides.
// This may be a genuine vkd3d-proton/Wine-specific limitation (DX-90/DX-114's real-Windows pass is
// the place to re-verify whether a real Windows driver would have accepted the original multi-
// descriptor-table approach) rather than a portable D3D12 correctness rule -- documented as an
// empirical dev-loop finding, not asserted as universal D3D12 behavior.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <map>
#include <tuple>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    /// Caches ID3D12RootSignature objects keyed by (numCbvs, numSrvs, numSamplers) -- the binding
    /// *shape* a given shader-variant family needs (DX-108's own "one per shader-variant family"
    /// framing: colored3d's family needs (2,0,0) [PerDraw@b0, FogParams@b1], textured3d/
    /// colored_textured3d need (2,1,1) [+ t0/s0]. DX-111's own landing of lit_textured3d confirmed
    /// (by reading the real HLSL, not assuming) that it ALSO needs (2,1,1) -- lit_textured3d.frag.hlsl
    /// declares its own Texture2D uTexture/SamplerState at t0/s0 just like textured3d, it just folds
    /// the extra lighting fields into a second CBV (LitLightParams@b1) instead of FogParams -- so it
    /// shares textured3d's exact root-signature object via this cache, no separate (2,0,0) shape as
    /// originally speculated here ahead of DX-111 actually landing it. alpha_test3d needs (1,1,1) --
    /// a single combined PerDraw@b0, no separate FogParams cbuffer. Any two variants with the same
    /// (numCbvs, numSrvs, numSamplers) shape genuinely share one root signature, matching the D3D12
    /// root-signature's own real constraint: it only describes binding slots, not cbuffer contents.
    class D3D12RootSignatureCache
    {
    public:
        /// Returns a cached (or newly created) root signature with @p numCbvs root CBV descriptors
        /// at b0..b(numCbvs-1) (ALL-visibility), plus -- for each of t0..t(numSrvs-1) -- its OWN
        /// single-descriptor table root parameter at index numCbvs+i (PIXEL-visibility, matching
        /// this project's own "textures are always sampled in the pixel shader" convention; see the
        /// class-level doc comment for why N separate tables, not one shared multi-descriptor table)
        /// and @p numSamplers static
        /// samplers at s0..s(numSamplers-1) using a fixed linear-wrap description (a reasonable
        /// stock-effect default; this is the same scope boundary the class-level doc comment already
        /// documents). Returns a null ComPtr (does not throw) if D3D12SerializeRootSignature or
        /// CreateRootSignature fails, matching this project's established D3DShaderCache/
        /// D3D11*Cache error-handling convention (callers check the returned ComPtr).
        ComPtr<ID3D12RootSignature> GetOrCreate(ID3D12Device* device, int numCbvs, int numSrvs, int numSamplers);

        /// Number of distinct root-signature shapes created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        using Key = std::tuple<int, int, int>;
        std::map<Key, ComPtr<ID3D12RootSignature>> cache_;
    };
}
