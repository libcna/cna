#pragma once

// plans/plan_dx.md Phase DIRECTX6 (DX-44): real ID3D11SamplerState creation + caching from XNA SamplerState
// fields, via D3DCommon's DX-12-state filter/address-mode mapping table.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <tuple>
#include <unordered_map>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    /// Caches ID3D11SamplerState objects keyed by every XNA SamplerState field this renderer
    /// carries -- same per-distinct-XNA-state caching discipline as D3D11InputLayoutCache (DX-32),
    /// so repeat ApplySamplerState() calls with identical XNA-level state reuse one GPU object
    /// instead of creating a fresh one every draw.
    class D3D11SamplerCache
    {
    public:
        /// Returns a cached (or newly created) sampler for the given raw XNA
        /// TextureFilter/TextureAddressMode ordinals.
        ///
        /// plans/plan_dx.md DX-216: `addressW`, `maxMipLevel` and `lodBias` are real parameters now. The
        /// comment that used to stand here said `IGraphicsRenderer::ApplySamplerState` "has no
        /// addressW parameter (a pre-existing interface limitation)" and set `AddressW = AddressV`
        /// on that basis. The interface gained `ApplySamplerAddressW(slot, addressW)` after that
        /// comment was written, and its own documentation says a renderer that does not override it
        /// must **not** invent a W mode -- which is exactly what mirroring V was doing.
        ///
        /// @param maxMipLevel XNA `SamplerState.MaxMipLevel`: the index of the MOST DETAILED mip
        ///                    level the sampler may use, which is D3D's `MinLOD`, not `MaxLOD`.
        /// @param lodBias     XNA `SamplerState.MipMapLevelOfDetailBias` -> `MipLODBias`.
        ComPtr<ID3D11SamplerState> GetOrCreate(ID3D11Device* device, int filter,
                                               int addressU, int addressV, int maxAnisotropy,
                                               int addressW, int maxMipLevel, float lodBias);

        /// Number of distinct sampler states created so far (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetCacheSizeEXT() const { return cache_.size(); }

    private:
        /// DX-216: a tuple key rather than a hand-packed 64-bit word -- the field set no longer
        /// fits in one, and a packed key that silently drops a field is the failure this task
        /// is fixing in the first place.
        using Key = std::tuple<int, int, int, int, int, int, float>;
        std::map<Key, ComPtr<ID3D11SamplerState>> cache_;
    };
}
