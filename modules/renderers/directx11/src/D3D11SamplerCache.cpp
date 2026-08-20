// plans/plan_dx.md Phase DIRECTX6 (DX-44).
#include "CNA/Internal/Renderers/DirectX11/D3D11SamplerCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX11
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        std::uint64_t MakeKey(int filter, int addressU, int addressV, int maxAnisotropy)
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(filter)) << 32)
                 | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(addressU)) << 24)
                 | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(addressV)) << 16)
                 | static_cast<std::uint64_t>(static_cast<std::uint16_t>(maxAnisotropy));
        }
    }

    ComPtr<ID3D11SamplerState> D3D11SamplerCache::GetOrCreate(
        ID3D11Device* device, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        const std::uint64_t key = MakeKey(filter, addressU, addressV, maxAnisotropy);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3DCommon::TextureFilterToD3D11(filter);
        desc.AddressU = D3DCommon::TextureAddressModeToD3D11(addressU);
        desc.AddressV = D3DCommon::TextureAddressModeToD3D11(addressV);
        // IGraphicsRenderer::ApplySamplerState has no addressW parameter (a pre-existing interface
        // limitation, not introduced here) -- reuse addressV, matching the V axis rather than
        // leaving W at an arbitrary default.
        desc.AddressW = desc.AddressV;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = static_cast<UINT>(std::clamp(maxAnisotropy, 1, 16));
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = D3D11_FLOAT32_MAX;

        ComPtr<ID3D11SamplerState> sampler;
        const HRESULT hr = device->CreateSamplerState(&desc, sampler.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11SamplerCache: CreateSamplerState failed, hr=" + FormatHr(hr));

        cache_.emplace(key, sampler);
        return sampler;
    }
}
