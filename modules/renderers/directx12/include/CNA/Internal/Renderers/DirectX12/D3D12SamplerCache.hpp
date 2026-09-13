#pragma once

// plans/plan_dx.md Phase DX13 (DX-119): real, runtime-settable per-slot D3D12 SamplerState -- replaces
// D3D12RootSignatureCache's own hardcoded D3D12_STATIC_SAMPLER_DESC (fixed LINEAR/WRAP baked into
// the root signature) with real D3D12_SAMPLER_DESC objects created from actual XNA SamplerState
// fields into a real D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER shader-visible heap, mirroring D3D11's own
// D3D11SamplerCache (DX-44) caching discipline.

#include <d3d12.h>

#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
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
        /// TextureFilterToD3D11/TextureAddressModeToD3D11 tables).
        ///
        /// plans/plan_dx.md DX-216: `addressW`, `maxMipLevel` and `lodBias` are real parameters. The text
        /// that used to stand here said AddressW was set equal to addressV because the interface had
        /// no third address mode -- it has `ApplySamplerAddressW`, and its documentation names
        /// inventing a W mode as the thing a renderer must not do. `maxMipLevel` is XNA's
        /// `SamplerState.MaxMipLevel`, the MOST DETAILED level index, so it maps to `MinLOD`.
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
            int addressW, int maxMipLevel, float lodBias,
            const std::function<std::uint32_t(const D3D12_SAMPLER_DESC&)>& createSlot);

        /// Number of distinct sampler states created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        /// DX-216: a tuple key rather than a hand-packed 64-bit word, for the same reason
        /// D3D11SamplerCache changed -- the field set no longer fits in one, and a packed key
        /// that silently drops a field is the defect this task exists to fix.
        using Key = std::tuple<int, int, int, int, int, int, float>;
        std::map<Key, std::uint32_t> cache_;
    };
}
