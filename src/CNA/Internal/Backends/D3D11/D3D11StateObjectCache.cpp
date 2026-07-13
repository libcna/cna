// plan_dx.md Phase DX7 (DX-50/DX-51/DX-52).
#include "CNA/Internal/Backends/D3D11/D3D11StateObjectCache.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DStateMapping.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Backends::D3D11
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
        int colorBlendFunc, int alphaBlendFunc)
    {
        const Key key{colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
                       colorBlendFunc, alphaBlendFunc};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        D3D11_BLEND_DESC desc{};
        desc.AlphaToCoverageEnable = FALSE;
        desc.IndependentBlendEnable = FALSE;

        // XNA Blend::One=0, Blend::Zero=1 -- BlendState.Opaque is SrcBlend=One/DestBlend=Zero on
        // both channels, i.e. a mathematical no-op. Matches this project's own already-established
        // VulkanGraphicsBackend::ApplyBlendState heuristic (Task 868).
        const bool isOpaque = colorSrcBlend == 0 && colorDstBlend == 1 &&
                              alphaSrcBlend == 0 && alphaDstBlend == 1;

        D3D11_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
        rt.BlendEnable = isOpaque ? FALSE : TRUE;
        rt.SrcBlend = D3DCommon::BlendToD3D11(colorSrcBlend);
        rt.DestBlend = D3DCommon::BlendToD3D11(colorDstBlend);
        rt.BlendOp = D3DCommon::BlendFunctionToD3D11(colorBlendFunc);
        rt.SrcBlendAlpha = D3DCommon::BlendToD3D11(alphaSrcBlend);
        rt.DestBlendAlpha = D3DCommon::BlendToD3D11(alphaDstBlend);
        rt.BlendOpAlpha = D3DCommon::BlendFunctionToD3D11(alphaBlendFunc);
        // IGraphicsBackend::ApplyBlendState carries no per-target color write mask -- documented
        // interface limitation (this cache's own header comment), always write all 4 channels.
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

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

        desc.FrontFace.StencilFunc = D3DCommon::CompareFunctionToD3D11(stencilFunc);
        desc.FrontFace.StencilPassOp = D3DCommon::StencilOperationToD3D11(stencilPass);
        desc.FrontFace.StencilFailOp = D3DCommon::StencilOperationToD3D11(stencilFail);
        desc.FrontFace.StencilDepthFailOp = D3DCommon::StencilOperationToD3D11(stencilDepthFail);

        // XNA's DepthStencilState.TwoSidedStencilMode gates whether the CounterClockwise* fields
        // are actually used at all -- when false, FNA/D3D9 apply the front-face ops to both faces
        // (matches this project's own EasyGL precedent, which only calls the *_separate(Back, ...)
        // GL entry points when twoSidedStencilMode is true). Mirror that here rather than always
        // wiring BackFace to the (possibly stale/default) ccw* fields.
        if (twoSidedStencilMode)
        {
            desc.BackFace.StencilFunc = D3DCommon::CompareFunctionToD3D11(ccwStencilFunc);
            desc.BackFace.StencilPassOp = D3DCommon::StencilOperationToD3D11(ccwStencilPass);
            desc.BackFace.StencilFailOp = D3DCommon::StencilOperationToD3D11(ccwStencilFail);
            desc.BackFace.StencilDepthFailOp = D3DCommon::StencilOperationToD3D11(ccwStencilDepthFail);
        }
        else
        {
            desc.BackFace = desc.FrontFace;
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
        float depthBias, float slopeScaleDepthBias)
    {
        // XNA's RasterizerState.DepthBias is a float already expressed in units of "r", the
        // depth buffer format's minimum resolvable difference -- the exact same convention this
        // project's own Vulkan backend feeds unscaled into vkCmdSetDepthBias's depthBiasConstantFactor,
        // and EasyGL feeds unscaled into glPolygonOffset's "units" parameter (see
        // VulkanGraphicsBackend::ApplyRasterizerState / EasyGLGraphicsBackend::ApplyRasterizerState's
        // own comments, Task 767). D3D11_RASTERIZER_DESC::DepthBias is the same "r"-scaled bias,
        // but declared INT rather than FLOAT -- round to the nearest representable integer count
        // rather than truncating.
        const int depthBiasInt = static_cast<int>(std::lround(depthBias));
        const Key key{cullMode, fillMode, scissorTestEnable, depthBiasInt, slopeScaleDepthBias};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3DCommon::FillModeToD3D11(fillMode);
        desc.CullMode = D3DCommon::CullModeToD3D11(cullMode);
        // D3DCommon::CullModeToD3D11's own header doc: assumes FrontCounterClockwise = FALSE.
        desc.FrontCounterClockwise = FALSE;
        desc.DepthBias = depthBiasInt;
        desc.DepthBiasClamp = 0.0f;
        desc.SlopeScaledDepthBias = slopeScaleDepthBias;
        desc.DepthClipEnable = TRUE;
        desc.ScissorEnable = scissorTestEnable ? TRUE : FALSE;
        // IGraphicsBackend::ApplyRasterizerState carries no MultiSampleAntiAlias parameter --
        // documented interface limitation; matches D3D11_RASTERIZER_DESC's own FALSE default
        // (this only affects line/point AA algorithm selection, not MSAA render-target sampling,
        // which Phase DX6's DXGI_SAMPLE_DESC already controls independently of rasterizer state).
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
