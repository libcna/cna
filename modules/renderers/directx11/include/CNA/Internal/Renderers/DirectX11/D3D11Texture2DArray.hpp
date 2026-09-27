// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX11
{
    /** @brief Native D3D11 array texture with exact layer/mip transfers. */
    class D3D11Texture2DArray final : public ITexture2DArrayRenderer
    {
    public:
        /**
         * @brief Allocates an array texture and its shader-resource view.
         * @param device Owning D3D11 device.
         * @param context Immediate context used for transfers.
         * @param width Level-zero width.
         * @param height Level-zero height.
         * @param layers Array layer count.
         * @param mipLevels Allocated mip count.
         * @param surfaceFormat XNA SurfaceFormat ordinal.
         * @param usage Declared portable usage bits.
         */
        D3D11Texture2DArray(
            ID3D11Device* device, ID3D11DeviceContext* context,
            int width, int height, int layers, int mipLevels,
            int surfaceFormat, std::uint32_t usage);

        /**
         * @brief Uploads one complete validated layer/mip rectangle.
         * @param layer Array layer.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width in texels.
         * @param height Rectangle height in texels.
         * @param data Tightly packed native-format bytes.
         * @param byteCount Exact byte count.
         * @return True when the upload was accepted.
         */
        [[nodiscard]] bool SetData(
            int layer, int mipLevel, int x, int y, int width, int height,
            const void* data, std::size_t byteCount) override;

        /**
         * @brief Reads one complete validated layer/mip rectangle from GPU memory.
         * @param layer Array layer.
         * @param mipLevel Mip level.
         * @param x Left texel.
         * @param y Top texel.
         * @param width Rectangle width in texels.
         * @param height Rectangle height in texels.
         * @param data Destination for tightly packed native-format bytes.
         * @param byteCount Exact byte count.
         * @return True when the GPU copy and readback succeeded.
         */
        [[nodiscard]] bool GetData(
            int layer, int mipLevel, int x, int y, int width, int height,
            void* data, std::size_t byteCount) const override;

        /**
         * @brief Returns the native sampled array view.
         * @return Device-owned SRV.
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

    private:
        [[nodiscard]] bool ValidRegion(
            int layer, int mipLevel, int x, int y, int width, int height,
            std::size_t byteCount) const;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
        int width_ = 0;
        int height_ = 0;
        int layers_ = 0;
        int mipLevels_ = 0;
        int unitBytes_ = 0;
        bool compressed_ = false;
        DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
        std::uint32_t usage_ = 0;
    };
}
