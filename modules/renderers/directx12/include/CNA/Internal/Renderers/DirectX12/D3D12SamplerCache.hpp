#pragma once

// plans/plan_dx.md Phase DX13 (DX-119): real, runtime-settable per-slot D3D12 SamplerState -- replaces
// D3D12RootSignatureCache's own hardcoded D3D12_STATIC_SAMPLER_DESC (fixed LINEAR/WRAP baked into
// the root signature) with real D3D12_SAMPLER_DESC objects created from actual XNA SamplerState
// fields into a real D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER shader-visible heap, mirroring D3D11's own
// D3D11SamplerCache (DX-44) caching discipline.

#include <d3d12.h>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace CNA::Internal::Renderers::DirectX12
{
    /// Caches D3D12 sampler descriptor-heap slots keyed by (filter, addressU, addressV,
    /// maxAnisotropy) -- same per-distinct-XNA-state caching discipline as D3D11SamplerCache, just
    /// returning a GPU descriptor handle (for SetGraphicsRootDescriptorTable) instead of a COM
    /// object, since D3D12 samplers are heap-resident descriptors, not separately-bindable objects.
    class D3D12SamplerCache
    {
    public:
        /// Returns the sampler-heap descriptor INDEX for a cached (or newly created) sampler for the
        /// given raw XNA TextureFilter/TextureAddressMode ordinals (via D3DStateMapping's existing
        /// TextureFilterToD3D11/TextureAddressModeToD3D11 tables). AddressW is set equal to
        /// addressV -- IGraphicsRenderer::ApplySamplerState's signature has no third address mode
        /// parameter (a pre-existing interface limitation, matches D3D11SamplerCache's own
        /// documented choice, not something this cache can fix on its own).
        ///
        /// REMED-GFX-177: an INDEX, not a GPU handle. A shader-visible sampler heap that grows is
        /// replaced by a larger object, so a cached handle would point into a retired heap; an index
        /// is stable across growth and resolves against whichever heap is current.
        ///
        /// @p createSlot is called ONLY on a genuine cache miss, so a cache hit never consumes a
        /// descriptor. It receives the fully populated D3D12_SAMPLER_DESC and must create the
        /// sampler in the renderer's own sampler heap and return the index it landed at.
        std::uint32_t GetOrCreateIndex(
            int filter, int addressU, int addressV, int maxAnisotropy,
            const std::function<std::uint32_t(const D3D12_SAMPLER_DESC&)>& createSlot);

        /// Number of distinct sampler states created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        std::unordered_map<std::uint64_t, std::uint32_t> cache_;
    };
}
