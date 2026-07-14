#pragma once

// plan_dx.md Phase DX13 (DX-119): real, runtime-settable per-slot D3D12 SamplerState -- replaces
// D3D12RootSignatureCache's own hardcoded D3D12_STATIC_SAMPLER_DESC (fixed LINEAR/WRAP baked into
// the root signature) with real D3D12_SAMPLER_DESC objects created from actual XNA SamplerState
// fields into a real D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER shader-visible heap, mirroring D3D11's own
// D3D11SamplerCache (DX-44) caching discipline.

#include <d3d12.h>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace CNA::Internal::Backends::D3D12
{
    /// Caches D3D12 sampler descriptor-heap slots keyed by (filter, addressU, addressV,
    /// maxAnisotropy) -- same per-distinct-XNA-state caching discipline as D3D11SamplerCache, just
    /// returning a GPU descriptor handle (for SetGraphicsRootDescriptorTable) instead of a COM
    /// object, since D3D12 samplers are heap-resident descriptors, not separately-bindable objects.
    class D3D12SamplerCache
    {
    public:
        /// Returns the GPU descriptor handle for a cached (or newly created) sampler for the given
        /// raw XNA TextureFilter/TextureAddressMode ordinals (via D3DStateMapping's existing
        /// TextureFilterToD3D11/TextureAddressModeToD3D11 tables). AddressW is set equal to
        /// addressV -- IGraphicsBackend::ApplySamplerState's signature has no third address mode
        /// parameter (a pre-existing interface limitation, matches D3D11SamplerCache's own
        /// documented choice, not something this cache can fix on its own.
        ///
        /// @p allocateSlot is called ONLY on a genuine cache miss (a fresh sampler heap slot is a
        /// real, finite, non-reclaimed resource -- DX-103's own bump-allocator discipline -- so a
        /// cache hit must never consume one). It must fill in a real CPU handle (for
        /// ID3D12Device::CreateSampler) and the matching GPU handle (for
        /// SetGraphicsRootDescriptorTable) from the backend's own sampler descriptor heap.
        D3D12_GPU_DESCRIPTOR_HANDLE GetOrCreate(
            ID3D12Device* device, int filter, int addressU, int addressV, int maxAnisotropy,
            const std::function<void(D3D12_CPU_DESCRIPTOR_HANDLE&, D3D12_GPU_DESCRIPTOR_HANDLE&)>& allocateSlot);

        /// Number of distinct sampler states created so far (NOXNA diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        std::unordered_map<std::uint64_t, D3D12_GPU_DESCRIPTOR_HANDLE> cache_;
    };
}
