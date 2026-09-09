#pragma once

// plans/plan_dx.md Phase DX13 (DX-120): real D3D12 occlusion query renderer, mirroring D3D11's own
// D3D11OcclusionQueryRenderer (DX-47) XNA-level contract exactly.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    /// Real ID3D12QueryHeap(D3D12_QUERY_HEAP_TYPE_OCCLUSION)-backed occlusion query (DX-120).
    /// The renderer now records draws on a frame-scoped list, but this class preserves DX-120's
    /// one-draw query behavior by synchronously resolving at End(). IsComplete() is therefore true
    /// after End() and false before it. DX-240 replaces this compatibility path with query-boundary
    /// commands, multi-draw accumulation and an asynchronous fence-completion check.
    class D3D12OcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        explicit D3D12OcclusionQueryRenderer(DirectX12Renderer* renderer);

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        /// IOcclusionQueryRenderer::PixelCount() returns int, but D3D12 reports a UINT64 --
        /// explicitly clamped to INT32_MAX rather than silently truncated, matching D3D11's own
        /// D3D11OcclusionQueryRenderer::PixelCount() convention exactly. Returns 0 if End() has not
        /// completed yet (IsComplete() == false), matching D3D11's own "GetData failed -> 0" path.
        [[nodiscard]] int PixelCount() const override;

    private:
        D3D12RendererReference renderer_;
        ComPtr<ID3D12QueryHeap> queryHeap_;
        // D3D12_HEAP_TYPE_READBACK resources must always stay in D3D12_RESOURCE_STATE_COPY_DEST
        // (the only state a readback heap resource may ever be in) -- created once in that state
        // and never transitioned, so this is deliberately NOT registered with
        // D3D12ResourceStateTracker (nothing ever calls TransitionTo() on it).
        ComPtr<ID3D12Resource> readbackBuffer_;
        bool resolved_ = false;
    };
}
