// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX11/D3D11Texture2DArray.hpp"

#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::DirectX11
{
    D3D11Texture2DArray::D3D11Texture2DArray(
        ID3D11Device* device, ID3D11DeviceContext* context,
        int width, int height, int layers, int mipLevels,
        int surfaceFormat, std::uint32_t usage)
        : device_(device), context_(context), width_(width), height_(height),
          layers_(layers), mipLevels_(mipLevels), usage_(usage)
    {
        format_ = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
        compressed_ = D3DCommon::IsXnaBlockCompressedSurfaceFormat(surfaceFormat);
        unitBytes_ = compressed_
            ? D3DCommon::SurfaceFormatBytesPerBlock(surfaceFormat)
            : D3DCommon::SurfaceFormatBytesPerTexel(surfaceFormat);
        if (!device || !context || width <= 0 || height <= 0 || layers <= 0 ||
            mipLevels <= 0 || format_ == DXGI_FORMAT_UNKNOWN || unitBytes_ <= 0 ||
            (usage & UINT32_C(0x01)) == 0)
            throw std::invalid_argument("D3D11 texture array: invalid allocation description");

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(width);
        description.Height = static_cast<UINT>(height);
        description.MipLevels = static_cast<UINT>(mipLevels);
        description.ArraySize = static_cast<UINT>(layers);
        description.Format = format_;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(
                &description, nullptr, texture_.GetAddressOf())))
            throw std::runtime_error("D3D11 texture array: native allocation failed");

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format_;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MostDetailedMip = 0;
        view.Texture2DArray.MipLevels = static_cast<UINT>(mipLevels);
        view.Texture2DArray.FirstArraySlice = 0;
        view.Texture2DArray.ArraySize = static_cast<UINT>(layers);
        if (FAILED(device_->CreateShaderResourceView(
                texture_.Get(), &view, srv_.GetAddressOf())))
            throw std::runtime_error("D3D11 texture array: sampled view creation failed");
    }

    bool D3D11Texture2DArray::ValidRegion(
        int layer, int mipLevel, int x, int y, int width, int height,
        std::size_t byteCount) const
    {
        if (layer < 0 || layer >= layers_ || mipLevel < 0 || mipLevel >= mipLevels_ ||
            x < 0 || y < 0 || width <= 0 || height <= 0)
            return false;
        const int mipWidth = std::max(1, width_ >> mipLevel);
        const int mipHeight = std::max(1, height_ >> mipLevel);
        if (x > mipWidth || width > mipWidth - x ||
            y > mipHeight || height > mipHeight - y)
            return false;
        if (compressed_ &&
            ((x & 3) != 0 || (y & 3) != 0 ||
             ((width & 3) != 0 && x + width != mipWidth) ||
             ((height & 3) != 0 && y + height != mipHeight)))
            return false;
        const std::size_t unitsX = compressed_
            ? static_cast<std::size_t>((width + 3) / 4)
            : static_cast<std::size_t>(width);
        const std::size_t unitsY = compressed_
            ? static_cast<std::size_t>((height + 3) / 4)
            : static_cast<std::size_t>(height);
        return byteCount == unitsX * unitsY * static_cast<std::size_t>(unitBytes_);
    }

    bool D3D11Texture2DArray::SetData(
        int layer, int mipLevel, int x, int y, int width, int height,
        const void* data, std::size_t byteCount)
    {
        if ((usage_ & UINT32_C(0x08)) == 0 || !data ||
            !ValidRegion(layer, mipLevel, x, y, width, height, byteCount))
            return false;
        const int rowUnits = compressed_ ? (width + 3) / 4 : width;
        const UINT rowPitch = static_cast<UINT>(rowUnits * unitBytes_);
        D3D11_BOX region{};
        region.left = static_cast<UINT>(x);
        region.top = static_cast<UINT>(y);
        region.right = static_cast<UINT>(x + width);
        region.bottom = static_cast<UINT>(y + height);
        region.front = 0;
        region.back = 1;
        const UINT subresource = D3D11CalcSubresource(
            static_cast<UINT>(mipLevel), static_cast<UINT>(layer),
            static_cast<UINT>(mipLevels_));
        const int mipWidth = std::max(1, width_ >> mipLevel);
        const int mipHeight = std::max(1, height_ >> mipLevel);
        const bool wholeMip = x == 0 && y == 0 && width == mipWidth && height == mipHeight;
        if (compressed_ && !wholeMip &&
            (((x + width) & 3) != 0 || ((y + height) & 3) != 0))
        {
            // D3D11 requires block-aligned box ends even at an odd mip edge. Read
            // the existing blocks, replace the requested ones, and upload that mip whole.
            D3D11_TEXTURE2D_DESC stagingDescription{};
            stagingDescription.Width = static_cast<UINT>(width_);
            stagingDescription.Height = static_cast<UINT>(height_);
            stagingDescription.MipLevels = static_cast<UINT>(mipLevels_);
            stagingDescription.ArraySize = 1;
            stagingDescription.Format = format_;
            stagingDescription.SampleDesc.Count = 1;
            stagingDescription.Usage = D3D11_USAGE_STAGING;
            stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            if (FAILED(device_->CreateTexture2D(
                    &stagingDescription, nullptr, staging.GetAddressOf())))
                return false;
            context_->CopySubresourceRegion(
                staging.Get(), static_cast<UINT>(mipLevel), 0, 0, 0,
                texture_.Get(), subresource, nullptr);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(staging.Get(), static_cast<UINT>(mipLevel),
                                     D3D11_MAP_READ, 0, &mapped)))
                return false;
            const std::size_t mipPitch =
                static_cast<std::size_t>((mipWidth + 3) / 4) * unitBytes_;
            const std::size_t mipRows = static_cast<std::size_t>((mipHeight + 3) / 4);
            std::vector<std::uint8_t> merged(mipPitch * mipRows);
            const auto* oldBytes = static_cast<const std::uint8_t*>(mapped.pData);
            for (std::size_t row = 0; row < mipRows; ++row)
                std::memcpy(merged.data() + row * mipPitch,
                            oldBytes + row * mapped.RowPitch, mipPitch);
            context_->Unmap(staging.Get(), static_cast<UINT>(mipLevel));
            const auto* newBytes = static_cast<const std::uint8_t*>(data);
            const std::size_t blockX = static_cast<std::size_t>(x / 4);
            const std::size_t blockY = static_cast<std::size_t>(y / 4);
            for (std::size_t row = 0; row < static_cast<std::size_t>((height + 3) / 4); ++row)
                std::memcpy(merged.data() + (blockY + row) * mipPitch +
                                blockX * unitBytes_,
                            newBytes + row * rowPitch, rowPitch);
            context_->UpdateSubresource(texture_.Get(), subresource, nullptr,
                                        merged.data(), static_cast<UINT>(mipPitch), 0);
            return true;
        }
        context_->UpdateSubresource(texture_.Get(), subresource,
                                    compressed_ && wholeMip ? nullptr : &region,
                                    data, rowPitch, 0);
        return true;
    }

    bool D3D11Texture2DArray::GetData(
        int layer, int mipLevel, int x, int y, int width, int height,
        void* data, std::size_t byteCount) const
    {
        if ((usage_ & UINT32_C(0x04)) == 0 || !data ||
            !ValidRegion(layer, mipLevel, x, y, width, height, byteCount))
            return false;
        D3D11_TEXTURE2D_DESC description{};
        // BC subresources smaller than one 4x4 block are legal mips, but cannot be
        // standalone BC resources. Allocate the original mip chain for staging.
        description.Width = static_cast<UINT>(width_);
        description.Height = static_cast<UINT>(height_);
        description.MipLevels = static_cast<UINT>(mipLevels_);
        description.ArraySize = 1;
        description.Format = format_;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(
                &description, nullptr, staging.GetAddressOf())))
            return false;
        const UINT subresource = D3D11CalcSubresource(
            static_cast<UINT>(mipLevel), static_cast<UINT>(layer),
            static_cast<UINT>(mipLevels_));
        context_->CopySubresourceRegion(staging.Get(), static_cast<UINT>(mipLevel), 0, 0, 0,
                                        texture_.Get(), subresource, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), static_cast<UINT>(mipLevel),
                                 D3D11_MAP_READ, 0, &mapped)))
            return false;
        const int rowUnits = compressed_ ? (width + 3) / 4 : width;
        const int rowCount = compressed_ ? (height + 3) / 4 : height;
        const int xUnits = compressed_ ? x / 4 : x;
        const int yUnits = compressed_ ? y / 4 : y;
        const std::size_t rowBytes = static_cast<std::size_t>(rowUnits) * unitBytes_;
        auto* destination = static_cast<std::uint8_t*>(data);
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        for (int row = 0; row < rowCount; ++row)
            std::memcpy(destination + static_cast<std::size_t>(row) * rowBytes,
                        source + static_cast<std::size_t>(yUnits + row) * mapped.RowPitch +
                            static_cast<std::size_t>(xUnits) * unitBytes_,
                        rowBytes);
        context_->Unmap(staging.Get(), static_cast<UINT>(mipLevel));
        return true;
    }
}
