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

    /** @brief Sampled D3D12 texture array with layer/mip transfers and device recovery. */
    class D3D12Texture2DArray final : public ITexture2DArrayRenderer,
                                      public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        /**
         * @brief Allocates an array with an SRV covering every layer and mip level.
         * @param renderer Owning renderer.
         * @param width Level-zero width.
         * @param height Level-zero height.
         * @param layers Array layer count.
         * @param mipLevels Allocated mip levels.
         * @param surfaceFormat XNA SurfaceFormat ordinal.
         * @param usage Texture2DArrayUsage mask.
         */
        D3D12Texture2DArray(DirectX12Renderer* renderer, int width, int height,
                            int layers, int mipLevels, int surfaceFormat, std::uint32_t usage);
        /** @brief Releases the descriptor and native texture. */
        ~D3D12Texture2DArray() override;

        /**
         * @brief Uploads exact native-format bytes into one layer/mip rectangle.
         * @param layer Array layer.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Source bytes.
         * @param byteCount Exact source byte count.
         * @return True when the upload was recorded.
         */
        [[nodiscard]] bool SetData(int layer, int mipLevel, int x, int y,
                                   int width, int height, const void* data,
                                   std::size_t byteCount) override;
        /**
         * @brief Reads exact native-format bytes from one layer/mip rectangle.
         * @param layer Array layer.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Destination bytes.
         * @param byteCount Exact destination byte count.
         * @return True when readback completed.
         */
        [[nodiscard]] bool GetData(int layer, int mipLevel, int x, int y,
                                   int width, int height, void* data,
                                   std::size_t byteCount) const override;

        /** @brief Returns the native sampled resource for frame retention. */
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const noexcept { return texture_.Get(); }
        /** @brief Returns the owning renderer for cross-device binding checks. */
        [[nodiscard]] DirectX12Renderer* GetOwnerEXT() const noexcept { return owner_; }
        /** @brief Returns the live shader-visible SRV handle. */
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceViewGpuHandleEXT() const;

        /** @brief Releases old-device objects while keeping uploaded data for recovery. */
        void ReleaseDeviceResourcesEXT() noexcept override;
        /** @brief Recreates the native array and restores uploaded subresources. */
        void RecreateDeviceResourcesEXT() override;

    private:
        [[nodiscard]] bool ValidRegion(int layer, int mipLevel, int x, int y,
                                       int width, int height, std::size_t byteCount) const;
        [[nodiscard]] std::size_t SubresourceIndex(int layer, int mipLevel) const noexcept;
        [[nodiscard]] std::size_t MipBytes(int mipLevel) const noexcept;
        void CreateResource();
        void UploadBytes(int layer, int mipLevel, int x, int y, int width, int height,
                         const std::uint8_t* bytes);

        DirectX12Renderer* owner_ = nullptr;
        D3D12RendererReference renderer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;
        std::uint32_t srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        int width_ = 0;
        int height_ = 0;
        int layers_ = 0;
        int mipLevels_ = 0;
        int unitBytes_ = 0;
        DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
        std::uint32_t usage_ = 0;
        bool compressed_ = false;
        std::vector<std::vector<std::uint8_t>> shadows_;
    };
}
