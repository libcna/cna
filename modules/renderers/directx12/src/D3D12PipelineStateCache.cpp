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
        const Key key = std::tuple_cat(
            desc.AsCacheKeyEXT(),
            std::make_tuple(static_cast<unsigned>(rtvFormat), static_cast<unsigned>(dsvFormat)));
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
        psoDesc.PrimitiveTopologyType = // plans/plan_dx.md DX-208
            static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(desc.topologyType);
        // REMED-GFX-077: BlendState.MultiSampleMask (static PSO state → keyed above).
        psoDesc.SampleMask = desc.sampleMask;
        psoDesc.SampleDesc.Count = desc.sampleCount; // plans/plan_dx.md DX-207
        psoDesc.NodeMask = 0;

        // Rasterizer -- D3DStateMapping returns D3D11-prefixed enums; static_cast to the D3D12
        // equivalent per D3DStateMapping.hpp's own verified "identical enum values" claim (re-spot-
        // checked against this machine's real d3d11.h/d3d12.h for D3D11_CULL_MODE/D3D12_CULL_MODE
        // and D3D11_FILL_MODE/D3D12_FILL_MODE specifically while implementing this task).
        D3D12_RASTERIZER_DESC& rs = psoDesc.RasterizerState;
        rs.FillMode = static_cast<D3D12_FILL_MODE>(FillModeToD3D11(desc.fillMode));
        rs.CullMode = static_cast<D3D12_CULL_MODE>(CullModeToD3D11(desc.cullMode));
        // Match FNA3D and D3D11StateObjectCache so culling and two-sided stencil agree on faces.
        rs.FrontCounterClockwise = TRUE;
        rs.DepthClipEnable = TRUE;
        // plans/plan_dx.md DX-207: deliberately left FALSE, matching D3D11RasterizerStateCache exactly.
        // Setting it from the sample count was tried while chasing the missing anti-aliasing and
        // MEASURED to make no difference -- easygl_rendertarget2d_msaa_test passes either way once
        // SampleDesc.Count is right -- so the change was reverted rather than kept on a plausible
        // story. It only selects the line/point AA algorithm; triangle coverage comes from the
        // target's sample count.
        rs.MultisampleEnable = FALSE;
        // plans/plan_dx.md DX-206: same three fields D3D11RasterizerStateCache fills, same meanings.
        rs.DepthBias = desc.depthBias;
        rs.DepthBiasClamp = 0.0f;
        rs.SlopeScaledDepthBias = desc.slopeScaleDepthBias;

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

        // Depth and stencil. plans/plan_dx.md DX-202: the stencil half is a field-for-field mirror of
        // D3D11DepthStencilStateCache::GetOrCreate, through the same D3DCommon mapping tables, so
        // one XNA DepthStencilState means the same thing on both D3D renderers by construction.
        D3D12_DEPTH_STENCIL_DESC& ds = psoDesc.DepthStencilState;
        ds.DepthEnable = desc.depthEnable ? TRUE : FALSE;
        ds.DepthWriteMask = desc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(desc.depthFunc));
        ds.StencilEnable = desc.stencilEnable ? TRUE : FALSE;
        ds.StencilReadMask = static_cast<UINT8>(desc.stencilMask);
        ds.StencilWriteMask = static_cast<UINT8>(desc.stencilWriteMask);

        ds.FrontFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(desc.stencilFunc));
        ds.FrontFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.stencilPass));
        ds.FrontFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.stencilFail));
        ds.FrontFace.StencilDepthFailOp =
            static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.stencilDepthFail));

        // XNA's DepthStencilState.TwoSidedStencilMode gates whether the CounterClockwise* fields are
        // used at all; when false, the front-face ops apply to both faces. Same rule
        // D3D11DepthStencilStateCache states, and the same one EasyGL follows by only calling the
        // *_separate(Back, ...) GL entry points when it is true.
        if (desc.twoSidedStencilMode)
        {
            ds.BackFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(desc.ccwStencilFunc));
            ds.BackFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.ccwStencilPass));
            ds.BackFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.ccwStencilFail));
            ds.BackFace.StencilDepthFailOp =
                static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.ccwStencilDepthFail));
        }
        else
        {
            ds.BackFace = ds.FrontFace;
        }

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
