// plans/plan_dx.md Phase DX13 (DX-119).
#include "CNA/Internal/Renderers/DirectX12/D3D12SamplerCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"

#include <algorithm>

namespace CNA::Internal::Renderers::DirectX12
{
    using namespace CNA::Internal::Renderers::D3DCommon;

    std::uint32_t D3D12SamplerCache::GetOrCreateIndex(
        int filter, int addressU, int addressV, int maxAnisotropy,
        int addressW, int maxMipLevel, float lodBias,
        const std::function<std::uint32_t(const D3D12_SAMPLER_DESC&)>& createSlot)
    {
        const Key key{filter, addressU, addressV, maxAnisotropy, addressW, maxMipLevel, lodBias};
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        D3D12_SAMPLER_DESC desc{};
        desc.Filter = static_cast<D3D12_FILTER>(TextureFilterToD3D11(filter));
        desc.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(TextureAddressModeToD3D11(addressU));
        desc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(TextureAddressModeToD3D11(addressV));
        // plans/plan_dx.md DX-216: the caller's own W mode -- see D3D11SamplerCache for the same change and
        // the same reason.
        desc.AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(TextureAddressModeToD3D11(addressW));
        desc.MipLODBias = lodBias;
        desc.MaxAnisotropy = static_cast<UINT>(std::clamp(maxAnisotropy, 1, 16));
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        // XNA's MaxMipLevel is the MOST DETAILED level index -- larger means coarser -- so it is
        // D3D's MinLOD, not MaxLOD, despite the name.
        desc.MinLOD = static_cast<float>(std::max(0, maxMipLevel));
        desc.MaxLOD = D3D12_FLOAT32_MAX;

        const std::uint32_t index = createSlot(desc);
        cache_.emplace(key, index);
        return index;
    }
}
