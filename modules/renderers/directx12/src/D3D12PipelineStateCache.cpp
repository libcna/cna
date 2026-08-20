// plans/plan_dx.md Phase DX12 (DX-107).
#include "CNA/Internal/Renderers/DirectX12/D3D12PipelineStateCache.hpp"

#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"

namespace CNA::Internal::Renderers::DirectX12
{
    using namespace CNA::Internal::Renderers::D3DCommon;

    namespace
    {
        // Same "BlendEnable disabled only for the exact Blend::One/Blend::Zero Opaque combination"
        // heuristic D3D11BlendStateCache::GetOrCreate already established (D3D11StateObjectCache.cpp)
        // -- kept consistent across both renderers rather than re-derived. XNA Blend::One's real
        // ordinal is 0, Blend::Zero's is 1 (Blend.hpp) -- matches D3D11StateObjectCache.cpp's own
        // `colorSrcBlend == 0` check exactly.
        bool DeriveBlendEnable(int colorSrcBlend, int colorDstBlend, int alphaSrcBlend, int alphaDstBlend)
        {
            const bool colorOpaque = (colorSrcBlend == 0 /*One*/ && colorDstBlend == 1 /*Zero*/);
            const bool alphaOpaque = (alphaSrcBlend == 0 /*One*/ && alphaDstBlend == 1 /*Zero*/);
            return !(colorOpaque && alphaOpaque);
        }
    }

    ComPtr<ID3D12PipelineState> D3D12PipelineStateCache::GetOrCreate(ID3D12Device* device,
                                                                      ID3D12RootSignature* rootSignature,
                                                                      const D3D12PipelineStateDesc& desc,
                                                                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
    {
        const Key key{static_cast<int>(desc.variant), desc.strideInBytes,
                      desc.colorSrcBlend, desc.alphaSrcBlend, desc.colorDstBlend, desc.alphaDstBlend,
                      desc.colorBlendFunc, desc.alphaBlendFunc,
                      desc.depthEnable, desc.depthWriteEnable, desc.depthFunc,
                      desc.cullMode, desc.fillMode,
                      desc.colorWriteMask, desc.sampleMask,
                      static_cast<unsigned>(rtvFormat), static_cast<unsigned>(dsvFormat)};
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        ComPtr<ID3D12PipelineState> pso;

        const uint8_t* vsBytes = nullptr; std::size_t vsSize = 0;
        const uint8_t* psBytes = nullptr; std::size_t psSize = 0;
        GetVertexShaderBytecode(desc.variant, vsBytes, vsSize);
        GetPixelShaderBytecode(desc.variant, psBytes, psSize);

        UINT inputElementCount = 0;
        const D3D12_INPUT_ELEMENT_DESC* inputElements = InputElementsForStrideD3D12(desc.strideInBytes, inputElementCount);

        if (!device || !rootSignature || !vsBytes || !psBytes || !inputElements)
        {
            cache_[key] = pso; // Cache the (null) failure too, same convention as D3D12RootSignatureCache.
            return pso;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature;
        psoDesc.VS = {vsBytes, vsSize};
        psoDesc.PS = {psBytes, psSize};
        psoDesc.InputLayout = {inputElements, inputElementCount};
        psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        // REMED-GFX-077: BlendState.MultiSampleMask (static PSO state → keyed above).
        psoDesc.SampleMask = desc.sampleMask;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.NodeMask = 0;

        // Rasterizer -- D3DStateMapping returns D3D11-prefixed enums; static_cast to the D3D12
        // equivalent per D3DStateMapping.hpp's own verified "identical enum values" claim (re-spot-
        // checked against this machine's real d3d11.h/d3d12.h for D3D11_CULL_MODE/D3D12_CULL_MODE
        // and D3D11_FILL_MODE/D3D12_FILL_MODE specifically while implementing this task).
        D3D12_RASTERIZER_DESC& rs = psoDesc.RasterizerState;
        rs.FillMode = static_cast<D3D12_FILL_MODE>(FillModeToD3D11(desc.fillMode));
        rs.CullMode = static_cast<D3D12_CULL_MODE>(CullModeToD3D11(desc.cullMode));
        rs.FrontCounterClockwise = FALSE; // Matches D3DStateMapping::CullModeToD3D11's own documented D3D11 default.
        rs.DepthClipEnable = TRUE;
        rs.MultisampleEnable = FALSE;

        // Blend -- single render target, same DeriveBlendEnable heuristic D3D11BlendStateCache uses.
        D3D12_BLEND_DESC& bs = psoDesc.BlendState;
        D3D12_RENDER_TARGET_BLEND_DESC& rt0 = bs.RenderTarget[0];
        rt0.BlendEnable = DeriveBlendEnable(desc.colorSrcBlend, desc.colorDstBlend, desc.alphaSrcBlend, desc.alphaDstBlend);
        rt0.SrcBlend = static_cast<D3D12_BLEND>(BlendToD3D11(desc.colorSrcBlend));
        rt0.DestBlend = static_cast<D3D12_BLEND>(BlendToD3D11(desc.colorDstBlend));
        rt0.BlendOp = static_cast<D3D12_BLEND_OP>(BlendFunctionToD3D11(desc.colorBlendFunc));
        rt0.SrcBlendAlpha = static_cast<D3D12_BLEND>(BlendToD3D11(desc.alphaSrcBlend));
        rt0.DestBlendAlpha = static_cast<D3D12_BLEND>(BlendToD3D11(desc.alphaDstBlend));
        rt0.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(BlendFunctionToD3D11(desc.alphaBlendFunc));
        rt0.LogicOpEnable = FALSE;
        rt0.LogicOp = D3D12_LOGIC_OP_NOOP;
        // REMED-GFX-077: BlendState.ColorWriteChannels slot 0 (bit-identical to D3D12_COLOR_WRITE_ENABLE_*).
        rt0.RenderTargetWriteMask = static_cast<UINT8>(desc.colorWriteMask & 0xF);

        // Depth -- stencil deliberately left disabled/default, see this header's own file-level doc
        // comment for why that's an honest, documented gap rather than a silent omission.
        D3D12_DEPTH_STENCIL_DESC& ds = psoDesc.DepthStencilState;
        ds.DepthEnable = desc.depthEnable ? TRUE : FALSE;
        ds.DepthWriteMask = desc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(desc.depthFunc));
        ds.StencilEnable = FALSE;

        psoDesc.NumRenderTargets = (rtvFormat == DXGI_FORMAT_UNKNOWN) ? 0 : 1;
        psoDesc.RTVFormats[0] = rtvFormat;
        psoDesc.DSVFormat = dsvFormat;

        HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            pso.Reset();

        cache_[key] = pso;
        return pso;
    }
}
