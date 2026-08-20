// plans/plan_dx.md Phase DX12 (DX-108).
#include "CNA/Internal/Renderers/DirectX12/D3D12RootSignatureCache.hpp"

#include <d3dcommon.h>

#include <vector>

namespace CNA::Internal::Renderers::DirectX12
{
    ComPtr<ID3D12RootSignature> D3D12RootSignatureCache::GetOrCreate(ID3D12Device* device, int numCbvs,
                                                                      int numSrvs, int numSamplers)
    {
        const Key key{numCbvs, numSrvs, numSamplers};
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        std::vector<D3D12_ROOT_PARAMETER1> rootParams;
        rootParams.reserve(static_cast<std::size_t>(numCbvs) + static_cast<std::size_t>(numSrvs));

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

        // DX-111 (dual_texture3d): each texture register gets its OWN single-descriptor table root
        // parameter (t0, t1, ...) rather than one shared multi-descriptor range -- found empirically
        // while landing dual_texture3d that a single D3D12_DESCRIPTOR_TABLE range with
        // NumDescriptors>1, populated via CopyDescriptorsSimple into a freshly bump-allocated
        // contiguous heap slot pair, samples as all-zero under this machine's Wine+vkd3d-proton dev
        // loop (DX-100) even though the CPU-side descriptor writes/handles were independently
        // verified correct (heap offsets increment by exactly one descriptor stride, both copies
        // succeed) -- a genuine dev-loop-specific limitation, not a CNA logic bug (the single-
        // descriptor-table case, numSrvs==1, has always worked correctly and still does). N separate
        // 1-descriptor tables sidesteps this entirely: each texture's OWN existing permanent SRV
        // handle (created once, at texture-construction time) is bound directly to its own root
        // parameter, exactly matching the already-proven-working numSrvs==1 path -- no per-draw
        // descriptor copy is needed at all now (DirectX12Renderer::DrawPrimitivesExImpl no longer
        // needs its own CopyDescriptorsSimple scratch-table logic for this). std::vector<...>
        // srvRanges is declared with a FIXED reserved capacity up front so its elements' addresses
        // (referenced by each root param's pDescriptorRanges below) stay stable across every
        // push_back -- no reallocation-invalidation risk.
        std::vector<D3D12_DESCRIPTOR_RANGE1> srvRanges;
        srvRanges.reserve(static_cast<std::size_t>(numSrvs));
        for (int t = 0; t < numSrvs; ++t)
        {
            D3D12_DESCRIPTOR_RANGE1 range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = static_cast<UINT>(t);
            range.RegisterSpace = 0;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
            range.OffsetInDescriptorsFromTableStart = 0;
            srvRanges.push_back(range);
        }
        for (int t = 0; t < numSrvs; ++t)
        {
            D3D12_ROOT_PARAMETER1 p{};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges = &srvRanges[static_cast<std::size_t>(t)];
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            rootParams.push_back(p);
        }

        // DX-119: real, runtime-settable per-slot SamplerState needs a dynamic sampler descriptor
        // table, not a D3D12_STATIC_SAMPLER_DESC baked into the root signature at PSO-creation
        // time (the previous fixed LINEAR/WRAP-for-every-draw approach) -- one root parameter per
        // sampler register (s0, s1, ...), same N-separate-single-descriptor-tables discipline the
        // SRV loop above already established (and for the same reason: this dev loop's own
        // empirically-found Wine+vkd3d-proton multi-descriptor-table limitation, DX-111). Populated
        // at draw time from D3D12SamplerCache via SetGraphicsRootDescriptorTable, mirroring the SRV
        // tables' own binding pattern exactly.
        std::vector<D3D12_DESCRIPTOR_RANGE1> samplerRanges;
        samplerRanges.reserve(static_cast<std::size_t>(numSamplers));
        for (int s = 0; s < numSamplers; ++s)
        {
            D3D12_DESCRIPTOR_RANGE1 range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = static_cast<UINT>(s);
            range.RegisterSpace = 0;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE; // samplers have no DATA_STATIC flag
            range.OffsetInDescriptorsFromTableStart = 0;
            samplerRanges.push_back(range);
        }
        for (int s = 0; s < numSamplers; ++s)
        {
            D3D12_ROOT_PARAMETER1 p{};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges = &samplerRanges[static_cast<std::size_t>(s)];
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            rootParams.push_back(p);
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = static_cast<UINT>(rootParams.size());
        desc.Desc_1_1.pParameters = rootParams.empty() ? nullptr : rootParams.data();
        desc.Desc_1_1.NumStaticSamplers = 0;
        desc.Desc_1_1.pStaticSamplers = nullptr;
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
