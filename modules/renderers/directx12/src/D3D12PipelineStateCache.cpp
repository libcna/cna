// plans/plan_dx.md Phase DX12 (DX-107).
#include "CNA/Internal/Renderers/DirectX12/D3D12PipelineStateCache.hpp"

#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <atomic>
#include <iterator>

namespace CNA::Internal::Renderers::DirectX12
{
    using namespace CNA::Internal::Renderers::D3DCommon;

    namespace
    {
        std::atomic<std::uint64_t> nextCustomProgramId{1};
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

    std::uint64_t NextD3D12CustomProgramIdEXT()
    {
        return nextCustomProgramId.fetch_add(1, std::memory_order_relaxed);
    }

    ComPtr<ID3D12PipelineState> D3D12PipelineStateCache::GetOrCreate(ID3D12Device* device,
                                                                      ID3D12RootSignature* rootSignature,
                                                                      const D3D12PipelineStateDesc& desc)
    {
        const Key key = desc.AsCacheKeyEXT();
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        ComPtr<ID3D12PipelineState> pso;

        const uint8_t* vsBytes = nullptr; std::size_t vsSize = 0;
        const uint8_t* psBytes = nullptr; std::size_t psSize = 0;
        if (desc.customProgramId != 0)
        {
            vsBytes = static_cast<const uint8_t*>(desc.customVertexShaderBytecode);
            vsSize = desc.customVertexShaderBytecodeSize;
            psBytes = static_cast<const uint8_t*>(desc.customPixelShaderBytecode);
            psSize = desc.customPixelShaderBytecodeSize;
        }
        else
        {
            GetVertexShaderBytecode(desc.variant, vsBytes, vsSize);
            GetPixelShaderBytecode(desc.variant, psBytes, psSize);
        }

        UINT inputElementCount = 0;
        std::vector<D3D12_INPUT_ELEMENT_DESC> translatedElements;
        const D3D12_INPUT_ELEMENT_DESC* inputElements = nullptr;
        if (!desc.vertexInputElements.empty())
        {
            if (InputElementsForLayoutD3D12(desc.vertexInputElements, translatedElements))
            {
                inputElements = translatedElements.data();
                inputElementCount = static_cast<UINT>(translatedElements.size());
            }
        }
        else if (!desc.vertexElements.empty())
        {
            if (InputElementsForDeclarationD3D12(desc.vertexElements, translatedElements))
            {
                inputElements = translatedElements.data();
                inputElementCount = static_cast<UINT>(translatedElements.size());
            }
        }
        else
        {
            inputElements = InputElementsForStrideD3D12(desc.strideInBytes, inputElementCount);
        }

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

        // Blend -- DX-224 mirrors D3D11BlendStateCache's independent ColorWriteChannels0..3
        // handling while keeping the common colour/alpha blend equation for every attachment.
        D3D12_BLEND_DESC& bs = psoDesc.BlendState;
        bs.IndependentBlendEnable =
            (desc.colorWriteMasks[1] != desc.colorWriteMasks[0]
             || desc.colorWriteMasks[2] != desc.colorWriteMasks[0]
             || desc.colorWriteMasks[3] != desc.colorWriteMasks[0]) ? TRUE : FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC rtTemplate{};
        const int saturated = static_cast<int>(
            Microsoft::Xna::Framework::Graphics::Blend::SourceAlphaSaturation);
        if (desc.colorDstBlend == saturated || desc.alphaDstBlend == saturated)
            throw System::NotSupportedException(
                "D3D12 does not support SourceAlphaSaturation as a destination blend factor.");
        rtTemplate.BlendEnable = DeriveBlendEnable(
            desc.colorSrcBlend, desc.colorDstBlend,
            desc.alphaSrcBlend, desc.alphaDstBlend);
        rtTemplate.SrcBlend = static_cast<D3D12_BLEND>(BlendToD3D11(desc.colorSrcBlend));
        rtTemplate.DestBlend = static_cast<D3D12_BLEND>(BlendToD3D11(desc.colorDstBlend));
        rtTemplate.BlendOp = static_cast<D3D12_BLEND_OP>(BlendFunctionToD3D11(desc.colorBlendFunc));
        rtTemplate.SrcBlendAlpha = static_cast<D3D12_BLEND>(AlphaBlendToD3D11(desc.alphaSrcBlend));
        rtTemplate.DestBlendAlpha = static_cast<D3D12_BLEND>(AlphaBlendToD3D11(desc.alphaDstBlend));
        rtTemplate.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(BlendFunctionToD3D11(desc.alphaBlendFunc));
        rtTemplate.LogicOpEnable = FALSE;
        rtTemplate.LogicOp = D3D12_LOGIC_OP_NOOP;
        for (std::size_t i = 0; i < std::size(bs.RenderTarget); ++i)
        {
            bs.RenderTarget[i] = rtTemplate;
            const std::size_t maskIndex = std::min<std::size_t>(i, 3);
            bs.RenderTarget[i].RenderTargetWriteMask =
                static_cast<UINT8>(desc.colorWriteMasks[maskIndex] & 0xF);
        }

        // Depth and stencil. plans/plan_dx.md DX-202: the stencil half is a field-for-field mirror of
        // D3D11DepthStencilStateCache::GetOrCreate, through the same D3DCommon mapping tables, so
        // one XNA DepthStencilState means the same thing on both D3D renderers by construction.
        D3D12_DEPTH_STENCIL_DESC& ds = psoDesc.DepthStencilState;
        // plans/plan_directx12_parity.md DX12-0027: a PSO for a surface without depth (DSVFormat UNKNOWN,
        // e.g. DepthFormat::None) must not enable the tests. Nothing is bound to test against, so the
        // result is the same, but the debug layer reports the mismatch (ID 680) on every such PSO.
        const bool hasDepthStencil = desc.depthStencilFormat != DXGI_FORMAT_UNKNOWN;
        ds.DepthEnable = desc.depthEnable && hasDepthStencil ? TRUE : FALSE;
        ds.DepthWriteMask = desc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(desc.depthFunc));
        ds.StencilEnable = desc.stencilEnable && hasDepthStencil ? TRUE : FALSE;
        ds.StencilReadMask = static_cast<UINT8>(desc.stencilMask);
        ds.StencilWriteMask = static_cast<UINT8>(desc.stencilWriteMask);

        // plans/plan_graphics_shared_cleanup.md GSC-0002: with FrontCounterClockwise = TRUE (above) the
        // counter-clockwise triangles are FrontFace, and XNA's CounterClockwiseStencil* state belongs to
        // exactly those -- DepthStencilState::Apply writes it to D3DRS_CCW_STENCIL*, which Direct3D 9
        // applies to the winding CullCounterClockwiseFace culls. The ordinary operations are BackFace;
        // D3D11DepthStencilStateCache carries the full account.
        ds.BackFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(desc.stencilFunc));
        ds.BackFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.stencilPass));
        ds.BackFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.stencilFail));
        ds.BackFace.StencilDepthFailOp =
            static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.stencilDepthFail));

        // TwoSidedStencilMode = false applies the ordinary operations to both windings.
        if (desc.twoSidedStencilMode)
        {
            ds.FrontFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(desc.ccwStencilFunc));
            ds.FrontFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.ccwStencilPass));
            ds.FrontFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.ccwStencilFail));
            ds.FrontFace.StencilDepthFailOp =
                static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(desc.ccwStencilDepthFail));
        }
        else
        {
            ds.FrontFace = ds.BackFace;
        }

        psoDesc.NumRenderTargets = std::min<UINT>(
            desc.renderTargetCount, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
        for (UINT i = 0; i < psoDesc.NumRenderTargets; ++i)
            psoDesc.RTVFormats[i] = desc.renderTargetFormats[i];
        psoDesc.DSVFormat = desc.depthStencilFormat;

        HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            pso.Reset();

        cache_[key] = pso;
        return pso;
    }
}
