// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace CNA::Internal::Renderers::DirectX12
{
    class DirectX12Renderer;

    /** @brief Measures a command range with native D3D12 timestamp queries. */
    class D3D12GpuTimer final : public IGpuTimerRenderer,
                                public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        /**
         * @brief Creates GPU timestamp and readback resources for one reusable range.
         * @param renderer Owning renderer and command queue.
         */
        explicit D3D12GpuTimer(DirectX12Renderer* renderer);
        /** @brief Releases query resources and unregisters device recovery. */
        ~D3D12GpuTimer() override;

        /** @brief Records the starting GPU timestamp on the current frame list. */
        void Begin() override;
        /** @brief Records the ending timestamp and queues asynchronous readback. */
        void End() override;
        /**
         * @brief Checks the submission fence without waiting for the GPU.
         * @return True when the result can be read without blocking.
         */
        [[nodiscard]] bool IsResultAvailable() const override;
        /**
         * @brief Returns elapsed GPU nanoseconds for a completed range.
         * @return GPU time, or zero while pending or after device recovery.
         */
        [[nodiscard]] std::uint64_t ElapsedNanoseconds() const override;

        /** @brief Releases resources belonging to the old D3D12 device. */
        void ReleaseDeviceResourcesEXT() noexcept override;
        /** @brief Recreates timestamp resources against the replacement device. */
        void RecreateDeviceResourcesEXT() override;

    private:
        void CreateResources();

        D3D12RendererReference renderer_;
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
        std::uint64_t frequency_ = 0;
        std::uint64_t completionFenceValue_ = 0;
        mutable std::uint64_t elapsedNanoseconds_ = 0;
        bool active_ = false;
        bool aborted_ = false;
        mutable bool ready_ = false;
    };
}
