#pragma once

// plans/plan_dx.md Phase DIRECTX6 (DX-47): real D3D11 occlusion query renderer.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    /// Real ID3D11Query(D3D11_QUERY_OCCLUSION)-backed occlusion query (DX-47). Unlike EasyGL's
    /// GLES3 GL_ANY_SAMPLES_PASSED (which only ever reports 0 or 1 visible sample), D3D11's
    /// D3D11_QUERY_OCCLUSION gives an exact visible-sample count -- this renderer can be more
    /// precise than EasyGL here, not less (this row's own plans/plan_dx.md note).
    class D3D11OcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        D3D11OcclusionQueryRenderer(ID3D11Device* device, ID3D11DeviceContext* context);

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        /// IOcclusionQueryRenderer::PixelCount() returns int, but D3D11 reports a UINT64 --
        /// explicitly clamped to INT_MAX rather than silently truncated (this row's own plans/plan_dx.md
        /// note; in practice no real render target has anywhere near 2^31 pixels).
        [[nodiscard]] int PixelCount() const override;

    private:
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11Query> query_;
    };
}
