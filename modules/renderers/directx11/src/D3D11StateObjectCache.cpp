// plans/plan_dx.md Phase DIRECTX7 (DX-50/DX-51/DX-52).
#include "CNA/Internal/Renderers/DirectX11/D3D11StateObjectCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"
#include "System/NotSupportedException.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"

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

    ComPtr<ID3D11BlendState> D3D11BlendStateCache::GetOrCreate(
        ID3D11Device* device,
        int colorSrcBlend, int alphaSrcBlend,
        int colorDstBlend, int alphaDstBlend,
        int colorBlendFunc, int alphaBlendFunc,
        int cw0, int cw1, int cw2, int cw3)
    {
        const Key key{colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                       colorBlendFunc, alphaBlendFunc, cw0, cw1, cw2, cw3};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        // D3D11 permits SRC_ALPHA_SAT only as a source color factor. Reject the two
        // destination slots before CreateBlendState so the debug layer stays clean.
        const int saturated = static_cast<int>(
            Microsoft::Xna::Framework::Graphics::Blend::SourceAlphaSaturation);
        if (colorDstBlend == saturated || alphaDstBlend == saturated)
            throw System::NotSupportedException(
                "D3D11 does not support SourceAlphaSaturation as a destination blend factor.");

        // REMED-GFX-077: XNA ColorWriteChannels (R=1,G=2,B=4,A=8) is bit-identical to
        // D3D11_COLOR_WRITE_ENABLE_* (RED=1,GREEN=2,BLUE=4,ALPHA=8, ALL=15), so the raw ordinal
        // masked to 0xF is the RenderTargetWriteMask directly. Independent per-target masks
        // (ColorWriteChannels1/2/3) require IndependentBlendEnable + RenderTarget[1..3].
        const UINT8 m0 = static_cast<UINT8>(cw0 & 0xF);
        const UINT8 m1 = static_cast<UINT8>(cw1 & 0xF);
        const UINT8 m2 = static_cast<UINT8>(cw2 & 0xF);
        const UINT8 m3 = static_cast<UINT8>(cw3 & 0xF);
        const bool independent = (m1 != m0 || m2 != m0 || m3 != m0);

        D3D11_BLEND_DESC desc{};
        desc.AlphaToCoverageEnable = FALSE;
        desc.IndependentBlendEnable = independent ? TRUE : FALSE;

        // XNA Blend::One=0, Blend::Zero=1 -- BlendState.Opaque is SrcBlend=One/DestBlend=Zero on
        // both channels, i.e. a mathematical no-op. Matches this project's own already-established
        // VulkanRenderer::ApplyBlendState heuristic (Task 868).
        const bool isOpaque = colorSrcBlend == 0 && colorDstBlend == 1 &&
                              alphaSrcBlend == 0 && alphaDstBlend == 1;

        D3D11_RENDER_TARGET_BLEND_DESC rtTemplate{};
        rtTemplate.BlendEnable = isOpaque ? FALSE : TRUE;
        rtTemplate.SrcBlend = D3DCommon::BlendToD3D11(colorSrcBlend);
        rtTemplate.DestBlend = D3DCommon::BlendToD3D11(colorDstBlend);
        rtTemplate.BlendOp = D3DCommon::BlendFunctionToD3D11(colorBlendFunc);
        rtTemplate.SrcBlendAlpha = D3DCommon::AlphaBlendToD3D11(alphaSrcBlend);
        rtTemplate.DestBlendAlpha = D3DCommon::AlphaBlendToD3D11(alphaDstBlend);
        rtTemplate.BlendOpAlpha = D3DCommon::BlendFunctionToD3D11(alphaBlendFunc);

        desc.RenderTarget[0] = rtTemplate;
        desc.RenderTarget[0].RenderTargetWriteMask = m0;
        if (independent)
        {
            // Each independent target needs its own full blend desc (same equation, own write mask).
            desc.RenderTarget[1] = rtTemplate; desc.RenderTarget[1].RenderTargetWriteMask = m1;
            desc.RenderTarget[2] = rtTemplate; desc.RenderTarget[2].RenderTargetWriteMask = m2;
            desc.RenderTarget[3] = rtTemplate; desc.RenderTarget[3].RenderTargetWriteMask = m3;
        }

        ComPtr<ID3D11BlendState> state;
        const HRESULT hr = device->CreateBlendState(&desc, state.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11BlendStateCache: CreateBlendState failed, hr=" + FormatHr(hr));

        cache_.emplace(key, state);
        return state;
    }

    ComPtr<ID3D11DepthStencilState> D3D11DepthStencilStateCache::GetOrCreate(
        ID3D11Device* device,
        bool depthEnable, bool depthWriteEnable, int depthFunc,
        bool stencilEnable, int stencilFunc, int stencilPass, int stencilFail, int stencilDepthFail,
        int stencilMask, int stencilWriteMask,
        bool twoSidedStencilMode,
        int ccwStencilFunc, int ccwStencilPass, int ccwStencilFail, int ccwStencilDepthFail)
    {
        const Key key{depthEnable, depthWriteEnable, depthFunc,
                       stencilEnable, stencilFunc, stencilPass, stencilFail, stencilDepthFail,
                       stencilMask, stencilWriteMask,
                       twoSidedStencilMode, ccwStencilFunc, ccwStencilPass, ccwStencilFail, ccwStencilDepthFail};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        D3D11_DEPTH_STENCIL_DESC desc{};
        desc.DepthEnable = depthEnable ? TRUE : FALSE;
        desc.DepthWriteMask = depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = D3DCommon::CompareFunctionToD3D11(depthFunc);
        desc.StencilEnable = stencilEnable ? TRUE : FALSE;
        desc.StencilReadMask = static_cast<UINT8>(stencilMask);
        desc.StencilWriteMask = static_cast<UINT8>(stencilWriteMask);

        // plans/plan_graphics_shared_cleanup.md GSC-0002: XNA's DepthStencilState::Apply (IL of
        // Microsoft.Xna.Framework.Graphics.dll) writes StencilFunction/Pass/Fail/DepthBufferFail to
        // D3DRS_STENCIL* and, when TwoSidedStencilMode is set, the CounterClockwise* fields to
        // D3DRS_CCW_STENCILFAIL/ZFAIL/PASS/FUNC (0xBA..0xBD). Direct3D 9 applies the CCW states to the
        // counter-clockwise triangles -- the winding D3DCULL_CCW (CullCounterClockwiseFace) culls.
        // D3D11RasterizerStateCache sets FrontCounterClockwise = TRUE and maps that cull mode to
        // D3D11_CULL_FRONT, so the counter-clockwise triangles are FrontFace here and the ordinary,
        // clockwise ones BackFace. FNA3D's Direct3D 11 driver wires the CCW fields to BackFace under the
        // same rasterizer convention; this follows XNA instead.
        desc.BackFace.StencilFunc = D3DCommon::CompareFunctionToD3D11(stencilFunc);
        desc.BackFace.StencilPassOp = D3DCommon::StencilOperationToD3D11(stencilPass);
        desc.BackFace.StencilFailOp = D3DCommon::StencilOperationToD3D11(stencilFail);
        desc.BackFace.StencilDepthFailOp = D3DCommon::StencilOperationToD3D11(stencilDepthFail);

        // TwoSidedStencilMode = false applies the ordinary operations to both windings (D3D9 ignores
        // the CCW states then); the CounterClockwise* fields may hold anything.
        if (twoSidedStencilMode)
        {
            desc.FrontFace.StencilFunc = D3DCommon::CompareFunctionToD3D11(ccwStencilFunc);
            desc.FrontFace.StencilPassOp = D3DCommon::StencilOperationToD3D11(ccwStencilPass);
            desc.FrontFace.StencilFailOp = D3DCommon::StencilOperationToD3D11(ccwStencilFail);
            desc.FrontFace.StencilDepthFailOp = D3DCommon::StencilOperationToD3D11(ccwStencilDepthFail);
        }
        else
        {
            desc.FrontFace = desc.BackFace;
        }

        ComPtr<ID3D11DepthStencilState> state;
        const HRESULT hr = device->CreateDepthStencilState(&desc, state.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11DepthStencilStateCache: CreateDepthStencilState failed, hr=" + FormatHr(hr));

        cache_.emplace(key, state);
        return state;
    }

    ComPtr<ID3D11RasterizerState> D3D11RasterizerStateCache::GetOrCreate(
        ID3D11Device* device,
        int cullMode, int fillMode,
        bool scissorTestEnable,
        int depthBias, float slopeScaleDepthBias)
    {
        const Key key{cullMode, fillMode, scissorTestEnable, depthBias, slopeScaleDepthBias};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3DCommon::FillModeToD3D11(fillMode);
        desc.CullMode = D3DCommon::CullModeToD3D11(cullMode);
        // Counter-clockwise triangles are front-facing, so CullCounterClockwiseFace is D3D11_CULL_FRONT
        // and XNA's CounterClockwiseStencil* state is FrontFace (D3D11DepthStencilStateCache).
        desc.FrontCounterClockwise = TRUE;
        desc.DepthBias = depthBias;
        desc.DepthBiasClamp = 0.0f;
        desc.SlopeScaledDepthBias = slopeScaleDepthBias;
        desc.DepthClipEnable = TRUE;
        desc.ScissorEnable = scissorTestEnable ? TRUE : FALSE;
        // IGraphicsRenderer::ApplyRasterizerState carries no MultiSampleAntiAlias parameter --
        // documented interface limitation; matches D3D11_RASTERIZER_DESC's own FALSE default
        // (this only affects line/point AA algorithm selection, not MSAA render-target sampling,
        // which Phase DIRECTX6's DXGI_SAMPLE_DESC already controls independently of rasterizer state).
        desc.MultisampleEnable = FALSE;
        desc.AntialiasedLineEnable = FALSE;

        ComPtr<ID3D11RasterizerState> state;
        const HRESULT hr = device->CreateRasterizerState(&desc, state.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RasterizerStateCache: CreateRasterizerState failed, hr=" + FormatHr(hr));

        cache_.emplace(key, state);
        return state;
    }
}
