#pragma once

// plans/plan_dx.md Phase DIRECTX6 (DX-44): real ID3D11SamplerState creation + caching from XNA SamplerState
// fields, via D3DCommon's DX-12-state filter/address-mode mapping table.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <unordered_map>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    /// Caches ID3D11SamplerState objects keyed by (filter, addressU, addressV, maxAnisotropy) --
    /// same per-distinct-XNA-state caching discipline as D3D11InputLayoutCache (DX-32), so repeat
    /// ApplySamplerState() calls with identical XNA-level state reuse one GPU object instead of
    /// creating a fresh one every draw.
    class D3D11SamplerCache
    {
    public:
        /// Returns a cached (or newly created) sampler for the given raw XNA
        /// TextureFilter/TextureAddressMode ordinals. AddressW (XNA's SamplerState has one, but
        /// IGraphicsRenderer::ApplySamplerState's signature does not carry a third address mode)
        /// is set equal to addressV -- documented limitation of the existing interface, not
        /// something this cache can fix on its own.
        ComPtr<ID3D11SamplerState> GetOrCreate(ID3D11Device* device, int filter,
                                               int addressU, int addressV, int maxAnisotropy);

        /// Number of distinct sampler states created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        std::unordered_map<std::uint64_t, ComPtr<ID3D11SamplerState>> cache_;
    };
}
