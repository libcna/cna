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

    }

    ComPtr<ID3D11SamplerState> D3D11SamplerCache::GetOrCreate(
        ID3D11Device* device, int filter, int addressU, int addressV, int maxAnisotropy,
        int addressW, int maxMipLevel, float lodBias)
    {
        const Key key{filter, addressU, addressV, maxAnisotropy, addressW, maxMipLevel, lodBias};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3DCommon::TextureFilterToD3D11(filter);
        desc.AddressU = D3DCommon::TextureAddressModeToD3D11(addressU);
        desc.AddressV = D3DCommon::TextureAddressModeToD3D11(addressV);
        // plans/plan_dx.md DX-216: the caller's own W mode. This used to be `desc.AddressV` on the strength
        // of a comment saying the interface carried no third address mode -- it does
        // (ApplySamplerAddressW), and mirroring V is precisely the "invent a W mode of your own"
        // that the interface documentation tells a renderer not to do.
        desc.AddressW = D3DCommon::TextureAddressModeToD3D11(addressW);
        desc.MipLODBias = lodBias;
        desc.MaxAnisotropy = static_cast<UINT>(std::clamp(maxAnisotropy, 1, 16));
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        // XNA's SamplerState.MaxMipLevel is the index of the MOST DETAILED level the sampler may
        // use -- larger means coarser -- which is D3D's MinLOD, not MaxLOD. Naming it "Max" and
        // mapping it to MaxLOD is the obvious wrong answer, so it is spelled out here.
        desc.MinLOD = static_cast<float>(std::max(0, maxMipLevel));
        desc.MaxLOD = D3D11_FLOAT32_MAX;

        ComPtr<ID3D11SamplerState> sampler;
        const HRESULT hr = device->CreateSamplerState(&desc, sampler.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11SamplerCache: CreateSamplerState failed, hr=" + FormatHr(hr));

        cache_.emplace(key, sampler);
        return sampler;
    }
}
