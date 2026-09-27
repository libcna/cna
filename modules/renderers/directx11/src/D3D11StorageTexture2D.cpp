// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX11/D3D11StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX11
{
    D3D11StorageTexture2D::D3D11StorageTexture2D(
        ID3D11Device* device, ID3D11DeviceContext* context, int width, int height,
        int mipLevels, int surfaceFormat, std::uint32_t usage)
        : device_(device), context_(context), width_(width), height_(height),
          mipLevels_(mipLevels),
          format_(D3DCommon::SurfaceFormatToDxgi(surfaceFormat)),
          bytesPerTexel_(D3DCommon::SurfaceFormatBytesPerTexel(surfaceFormat)),
          usage_(usage)
    {
        if (!device || !context || width <= 0 || height <= 0 || mipLevels <= 0 ||
            format_ == DXGI_FORMAT_UNKNOWN || bytesPerTexel_ <= 0 ||
            (usage & UINT32_C(0x03)) == 0)
            throw std::invalid_argument("D3D11 storage texture: invalid allocation description");

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(width);
        description.Height = static_cast<UINT>(height);
        description.MipLevels = static_cast<UINT>(mipLevels);
        description.ArraySize = 1;
        description.Format = format_;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        if ((usage & UINT32_C(0x04)) != 0)
            description.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(
                &description, nullptr, texture_.GetAddressOf())))
            throw std::runtime_error("D3D11 storage texture: allocation failed");

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
        uavDescription.Format = description.Format;
        uavDescription.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDescription.Texture2D.MipSlice = 0;
        if (FAILED(device_->CreateUnorderedAccessView(
                texture_.Get(), &uavDescription, uav_.GetAddressOf())))
            throw std::runtime_error("D3D11 storage texture: UAV creation failed");
        if ((usage & UINT32_C(0x04)) != 0 &&
            FAILED(device_->CreateShaderResourceView(
                texture_.Get(), nullptr, srv_.GetAddressOf())))
            throw std::runtime_error("D3D11 storage texture: sampled view creation failed");
    }

    bool D3D11StorageTexture2D::ValidRegion(
        int mipLevel, int x, int y, int width, int height,
        std::size_t byteCount) const
    {
        if (mipLevel < 0 || mipLevel >= mipLevels_ || x < 0 || y < 0 ||
            width <= 0 || height <= 0)
            return false;
        const int mipWidth = std::max(1, width_ >> mipLevel);
        const int mipHeight = std::max(1, height_ >> mipLevel);
        return x <= mipWidth && width <= mipWidth - x &&
               y <= mipHeight && height <= mipHeight - y &&
               byteCount == static_cast<std::size_t>(width) * height *
                   static_cast<std::size_t>(bytesPerTexel_);
    }

    bool D3D11StorageTexture2D::SetData(
        int mipLevel, int x, int y, int width, int height,
        const void* data, std::size_t byteCount)
    {
        if ((usage_ & UINT32_C(0x20)) == 0 || !data ||
            !ValidRegion(mipLevel, x, y, width, height, byteCount))
            return false;
        D3D11_BOX region{};
        region.left = static_cast<UINT>(x);
        region.top = static_cast<UINT>(y);
        region.right = static_cast<UINT>(x + width);
        region.bottom = static_cast<UINT>(y + height);
        region.back = 1;
        context_->UpdateSubresource(texture_.Get(), static_cast<UINT>(mipLevel),
                                    &region, data,
                                    static_cast<UINT>(width * bytesPerTexel_), 0);
        return true;
    }

    bool D3D11StorageTexture2D::GetData(
        int mipLevel, int x, int y, int width, int height,
        void* data, std::size_t byteCount) const
    {
        if ((usage_ & UINT32_C(0x10)) == 0 || !data ||
            !ValidRegion(mipLevel, x, y, width, height, byteCount))
            return false;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(std::max(1, width_ >> mipLevel));
        description.Height = static_cast<UINT>(std::max(1, height_ >> mipLevel));
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = format_;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(
                &description, nullptr, staging.GetAddressOf())))
            return false;
        context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0,
                                        texture_.Get(), static_cast<UINT>(mipLevel), nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        auto* destination = static_cast<std::uint8_t*>(data);
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        const std::size_t rowBytes = static_cast<std::size_t>(width) *
            static_cast<std::size_t>(bytesPerTexel_);
        for (int row = 0; row < height; ++row)
            std::memcpy(destination + static_cast<std::size_t>(row) * rowBytes,
                        source + static_cast<std::size_t>(y + row) * mapped.RowPitch +
                            static_cast<std::size_t>(x) *
                                static_cast<std::size_t>(bytesPerTexel_),
                        rowBytes);
        context_->Unmap(staging.Get(), 0);
        return true;
    }
}
