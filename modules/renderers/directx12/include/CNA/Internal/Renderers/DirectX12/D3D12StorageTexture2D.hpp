// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12DescriptorHeaps.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::DirectX12
{
    class DirectX12Renderer;

    /** @brief Native typed-UAV texture with optional sampling and exact mip transfers. */
    class D3D12StorageTexture2D final : public IStorageTexture2DRenderer,
                                        public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        /**
         * @brief Creates a two-dimensional storage texture on one D3D12 device.
         * @param renderer Owning renderer.
         * @param width Level-zero width.
         * @param height Level-zero height.
         * @param mipLevels Allocated mip levels.
         * @param surfaceFormat XNA SurfaceFormat ordinal.
         * @param usage StorageTexture2DUsage mask.
         */
        D3D12StorageTexture2D(DirectX12Renderer* renderer, int width, int height,
                              int mipLevels, int surfaceFormat, std::uint32_t usage);
        /** @brief Releases the native resource and its descriptors. */
        ~D3D12StorageTexture2D() override;

        /**
         * @brief Uploads one native-format mip rectangle.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Source bytes.
         * @param byteCount Exact byte count.
         * @return True after the full upload was recorded.
         */
        [[nodiscard]] bool SetData(int mipLevel, int x, int y, int width, int height,
                                   const void* data, std::size_t byteCount) override;
        /**
         * @brief Reads one native-format mip rectangle from the GPU.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Destination bytes.
         * @param byteCount Exact byte count.
         * @return True after the full readback completed.
         */
        [[nodiscard]] bool GetData(int mipLevel, int x, int y, int width, int height,
                                   void* data, std::size_t byteCount) const override;

        /** @brief Returns the renderer that owns this native texture. */
        [[nodiscard]] DirectX12Renderer* GetOwnerEXT() const noexcept { return owner_; }
        /** @brief Returns the resource retained by queued work. */
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const noexcept { return texture_.Get(); }
        /** @brief Returns the declared StorageTexture2DUsage bits. */
        [[nodiscard]] std::uint32_t GetUsageEXT() const noexcept { return usage_; }
        /** @brief Returns the typed UAV descriptor for compute register u(unit). */
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetUavGpuHandleEXT() const;
        /** @brief Returns the sampled SRV descriptor, or an empty handle without Sampled usage. */
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandleEXT() const;
        /** @brief Removes stale CPU recovery data after a GPU write to mip zero. */
        void InvalidateRecoveryShadowEXT() noexcept;

        /** @brief Releases resources belonging to the old D3D12 device. */
        void ReleaseDeviceResourcesEXT() noexcept override;
        /** @brief Recreates descriptors and restores CPU-written mip data. */
        void RecreateDeviceResourcesEXT() override;

    private:
        [[nodiscard]] bool ValidRegion(int mipLevel, int x, int y, int width,
                                       int height, std::size_t byteCount) const noexcept;
        void CreateResource();
        void UploadBytes(int mipLevel, int x, int y, int width, int height,
                         const std::uint8_t* bytes);

        DirectX12Renderer* owner_ = nullptr;
        D3D12RendererReference renderer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;
        std::uint32_t uavIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        std::uint32_t srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 0;
        int bytesPerTexel_ = 0;
        DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
        std::uint32_t usage_ = 0;
        std::vector<std::vector<std::uint8_t>> shadows_;
    };
}
