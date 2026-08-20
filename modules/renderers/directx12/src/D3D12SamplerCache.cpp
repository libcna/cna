// plans/plan_dx.md Phase DX13 (DX-119).
#include "CNA/Internal/Renderers/DirectX12/D3D12SamplerCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"

#include <algorithm>

namespace CNA::Internal::Renderers::DirectX12
{
    using namespace CNA::Internal::Renderers::D3DCommon;

    namespace
    {
        std::uint64_t MakeKey(int filter, int addressU, int addressV, int maxAnisotropy)
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(filter)) << 32)
                 | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(addressU)) << 24)
                 | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(addressV)) << 16)
                 | static_cast<std::uint64_t>(static_cast<std::uint16_t>(maxAnisotropy));
        }
    }

    std::uint32_t D3D12SamplerCache::GetOrCreateIndex(
        int filter, int addressU, int addressV, int maxAnisotropy,
        const std::function<std::uint32_t(const D3D12_SAMPLER_DESC&)>& createSlot)
    {
        const std::uint64_t key = MakeKey(filter, addressU, addressV, maxAnisotropy);
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        D3D12_SAMPLER_DESC desc{};
        desc.Filter = static_cast<D3D12_FILTER>(TextureFilterToD3D11(filter));
        desc.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(TextureAddressModeToD3D11(addressU));
        desc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(TextureAddressModeToD3D11(addressV));
        desc.AddressW = desc.AddressV; // matches D3D11SamplerCache's own documented AddressW==AddressV choice
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = static_cast<UINT>(std::clamp(maxAnisotropy, 1, 16));
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = D3D12_FLOAT32_MAX;

        const std::uint32_t index = createSlot(desc);
        cache_.emplace(key, index);
        return index;
    }
}
