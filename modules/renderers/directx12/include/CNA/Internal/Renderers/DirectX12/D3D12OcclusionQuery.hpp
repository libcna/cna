#pragma once

// plans/plan_dx.md DX-120/DX-240: frame-scoped D3D12 occlusion queries with asynchronous fence
// completion and multi-draw accumulation.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    /** @brief Real frame-scoped D3D12 occlusion query with asynchronous result completion. */
    class D3D12OcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Creates a query heap and its CPU-readable result buffer.
         *
         * @param renderer Owning D3D12 renderer.
         */
        explicit D3D12OcclusionQueryRenderer(DirectX12Renderer* renderer);
        /** @brief Balances a still-active native query before releasing its resources. */
        ~D3D12OcclusionQueryRenderer() override;

        /** @brief Begins an occlusion range on the current frame command list. */
        void Begin() override;
        /** @brief Ends and resolves the range without submitting or waiting. */
        void End() override;
        /**
         * @brief Reports whether the frame fence carrying the resolve has completed.
         *
         * @return True when the resolved count can be read without waiting.
         */
        [[nodiscard]] bool IsComplete() const override;
        /**
         * @brief Returns the completed sample count clamped to INT32_MAX, or zero while pending.
         *
         * @return The completed sample count, or zero before completion.
         */
        [[nodiscard]] int PixelCount() const override;

    private:
        D3D12RendererReference renderer_;
        ComPtr<ID3D12QueryHeap> queryHeap_;
        // D3D12_HEAP_TYPE_READBACK resources must always stay in D3D12_RESOURCE_STATE_COPY_DEST
        // (the only state a readback heap resource may ever be in) -- created once in that state
        // and never transitioned, so this is deliberately NOT registered with
        // D3D12ResourceStateTracker (nothing ever calls TransitionTo() on it).
        ComPtr<ID3D12Resource> readbackBuffer_;
        std::uint64_t completionFenceValue_ = 0;
        bool active_ = false;
    };
}
