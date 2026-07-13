// plan_dx.md Phase DX12 (DX-108).
#include "CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.hpp"

#include <d3dcommon.h>

#include <vector>

namespace CNA::Internal::Backends::D3D12
{
    namespace
    {
        D3D12_STATIC_SAMPLER_DESC MakeDefaultStaticSampler(UINT registerIndex)
        {
            D3D12_STATIC_SAMPLER_DESC s{};
            s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            s.MipLODBias = 0.0f;
            s.MaxAnisotropy = 1;
            s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            s.MinLOD = 0.0f;
            s.MaxLOD = D3D12_FLOAT32_MAX;
            s.ShaderRegister = registerIndex;
            s.RegisterSpace = 0;
            s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            return s;
        }
    }

    ComPtr<ID3D12RootSignature> D3D12RootSignatureCache::GetOrCreate(ID3D12Device* device, int numCbvs,
                                                                      int numSrvs, int numSamplers)
    {
        const Key key{numCbvs, numSrvs, numSamplers};
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        std::vector<D3D12_ROOT_PARAMETER1> rootParams;
        rootParams.reserve(static_cast<std::size_t>(numCbvs) + (numSrvs > 0 ? 1 : 0));

        for (int b = 0; b < numCbvs; ++b)
        {
            D3D12_ROOT_PARAMETER1 p{};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            p.Descriptor.ShaderRegister = static_cast<UINT>(b);
            p.Descriptor.RegisterSpace = 0;
            p.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // See class doc comment: shared VS/PS cbuffers.
            rootParams.push_back(p);
        }

        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        if (numSrvs > 0)
        {
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = static_cast<UINT>(numSrvs);
            srvRange.BaseShaderRegister = 0;
            srvRange.RegisterSpace = 0;
            srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
            srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER1 p{};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges = &srvRange;
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            rootParams.push_back(p);
        }

        std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
        staticSamplers.reserve(static_cast<std::size_t>(numSamplers));
        for (int s = 0; s < numSamplers; ++s)
            staticSamplers.push_back(MakeDefaultStaticSampler(static_cast<UINT>(s)));

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = static_cast<UINT>(rootParams.size());
        desc.Desc_1_1.pParameters = rootParams.empty() ? nullptr : rootParams.data();
        desc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
        desc.Desc_1_1.pStaticSamplers = staticSamplers.empty() ? nullptr : staticSamplers.data();
        desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, serialized.ReleaseAndGetAddressOf(),
                                                            errorBlob.ReleaseAndGetAddressOf());
        ComPtr<ID3D12RootSignature> rootSig;
        if (FAILED(hr) || !device)
        {
            cache_[key] = rootSig; // Cache the failure too -- matches this project's other *Cache classes.
            return rootSig;
        }

        hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                          IID_PPV_ARGS(rootSig.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            rootSig.Reset();

        cache_[key] = rootSig;
        return rootSig;
    }
}
