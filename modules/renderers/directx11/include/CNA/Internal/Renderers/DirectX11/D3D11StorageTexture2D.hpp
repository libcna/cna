// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX11
{
    /** @brief Native D3D11 two-dimensional typed UAV texture. */
    class D3D11StorageTexture2D final : public IStorageTexture2DRenderer
    {
    public:
        /**
         * @brief Allocates a Color storage texture and its requested native views.
         * @param device Owning D3D11 device.
         * @param context Immediate context for transfers.
         * @param width Level-zero width.
         * @param height Level-zero height.
         * @param mipLevels Allocated mip count.
         * @param usage Portable storage-texture usage bits.
         */
        D3D11StorageTexture2D(ID3D11Device* device, ID3D11DeviceContext* context,
                              int width, int height, int mipLevels, std::uint32_t usage);
        /**
         * @brief Uploads a tightly packed Color rectangle into one mip.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Source bytes.
         * @param byteCount Exact byte count.
         * @return True after the complete upload.
         */
        [[nodiscard]] bool SetData(int mipLevel, int x, int y, int width, int height,
                                   const void* data, std::size_t byteCount) override;
        /**
         * @brief Reads a tightly packed Color rectangle from one mip.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Destination bytes.
         * @param byteCount Exact byte count.
         * @return True after the complete readback.
         */
        [[nodiscard]] bool GetData(int mipLevel, int x, int y, int width, int height,
                                   void* data, std::size_t byteCount) const override;
        /**
         * @brief Returns the native UAV for mip zero.
         * @return Native UAV.
         */
        [[nodiscard]] ID3D11UnorderedAccessView* GetUnorderedAccessViewEXT() const
        {
            return uav_.Get();
        }
        /**
         * @brief Returns the native sampled view when sampling was requested.
         * @return Native SRV, or null without Sampled usage.
         */
        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceViewEXT() const
        {
            return srv_.Get();
        }
        /**
         * @brief Returns the owning native device.
         * @return D3D11 device identity.
         */
        [[nodiscard]] ID3D11Device* GetDeviceEXT() const { return device_.Get(); }
        /**
         * @brief Returns declared portable usage bits.
         * @return StorageTexture2DUsage bit mask.
         */
        [[nodiscard]] std::uint32_t GetUsageEXT() const { return usage_; }

    private:
        [[nodiscard]] bool ValidRegion(int mipLevel, int x, int y, int width,
                                       int height, std::size_t byteCount) const;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 0;
        std::uint32_t usage_ = 0;
    };
}
