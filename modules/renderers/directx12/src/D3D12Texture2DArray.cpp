// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX12/D3D12Texture2DArray.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        constexpr D3D12_RESOURCE_STATES kShaderReadable =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

        [[nodiscard]] UINT AlignRow(UINT bytes)
        {
            return (bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                   ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        }
    }

    D3D12Texture2DArray::D3D12Texture2DArray(
        DirectX12Renderer* renderer, int width, int height, int layers, int mipLevels,
        int surfaceFormat, std::uint32_t usage)
        : owner_(renderer),
          renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12Texture2DArray"),
          width_(width), height_(height), layers_(layers), mipLevels_(mipLevels), usage_(usage)
    {
        format_ = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
        compressed_ = D3DCommon::IsXnaBlockCompressedSurfaceFormat(surfaceFormat);
        unitBytes_ = compressed_ ? D3DCommon::SurfaceFormatBytesPerBlock(surfaceFormat)
                                 : D3DCommon::SurfaceFormatBytesPerTexel(surfaceFormat);
        if (!renderer || width <= 0 || height <= 0 || layers <= 0 || mipLevels <= 0 ||
            format_ == DXGI_FORMAT_UNKNOWN || unitBytes_ <= 0 || (usage & 1u) == 0)
            throw std::invalid_argument("D3D12 texture array: invalid allocation description");
        shadows_.resize(static_cast<std::size_t>(layers_) * mipLevels_);
        CreateResource();
        renderer_->RegisterRecoverableResourceEXT(this);
    }

    D3D12Texture2DArray::~D3D12Texture2DArray()
    {
        if (renderer_)
            renderer_->UnregisterRecoverableResourceEXT(this);
        ReleaseDeviceResourcesEXT();
    }

    std::size_t D3D12Texture2DArray::SubresourceIndex(int layer, int mipLevel) const noexcept
    {
        return static_cast<std::size_t>(layer) * mipLevels_ + mipLevel;
    }

    std::size_t D3D12Texture2DArray::MipBytes(int mipLevel) const noexcept
    {
        const int width = std::max(1, width_ >> mipLevel);
        const int height = std::max(1, height_ >> mipLevel);
        const std::size_t columns = compressed_ ? static_cast<std::size_t>((width + 3) / 4)
                                                : static_cast<std::size_t>(width);
        const std::size_t rows = compressed_ ? static_cast<std::size_t>((height + 3) / 4)
                                             : static_cast<std::size_t>(height);
        return columns * rows * static_cast<std::size_t>(unitBytes_);
    }

    void D3D12Texture2DArray::CreateResource()
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = static_cast<UINT64>(width_);
        description.Height = static_cast<UINT>(height_);
        description.DepthOrArraySize = static_cast<UINT16>(layers_);
        description.MipLevels = static_cast<UINT16>(mipLevels_);
        description.Format = format_;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(renderer_->GetDeviceEXT()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &description,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(texture_.ReleaseAndGetAddressOf()))))
            throw std::runtime_error("D3D12 texture array: native allocation failed");
        renderer_->GetResourceStateTrackerEXT().TrackResource(
            texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format_;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2DArray.MipLevels = static_cast<UINT>(mipLevels_);
        view.Texture2DArray.ArraySize = static_cast<UINT>(layers_);
        heaps_ = renderer_->GetDescriptorHeapsEXT();
        srvIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
            [&](D3D12_CPU_DESCRIPTOR_HANDLE handle)
            {
                renderer_->GetDeviceEXT()->CreateShaderResourceView(texture_.Get(), &view, handle);
            });
        ID3D12GraphicsCommandList* list = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(texture_.Get());
        renderer_->GetResourceStateTrackerEXT().TransitionTo(list, texture_.Get(), kShaderReadable);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12Texture2DArray::GetShaderResourceViewGpuHandleEXT() const
    {
        return heaps_ && srvIndex_ != D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex
            ? heaps_->cbvSrvUav.GpuHandle(srvIndex_)
            : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    bool D3D12Texture2DArray::ValidRegion(
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
        if (compressed_ && ((x & 3) != 0 || (y & 3) != 0 ||
            ((width & 3) != 0 && x + width != mipWidth) ||
            ((height & 3) != 0 && y + height != mipHeight)))
            return false;
        const std::size_t columns = compressed_ ? static_cast<std::size_t>((width + 3) / 4)
                                                : static_cast<std::size_t>(width);
        const std::size_t rows = compressed_ ? static_cast<std::size_t>((height + 3) / 4)
                                             : static_cast<std::size_t>(height);
        return byteCount == columns * rows * static_cast<std::size_t>(unitBytes_);
    }

    void D3D12Texture2DArray::UploadBytes(
        int layer, int mipLevel, int x, int y, int width, int height,
        const std::uint8_t* bytes)
    {
        const UINT rowBytes = static_cast<UINT>(
            (compressed_ ? (width + 3) / 4 : width) * unitBytes_);
        const UINT rowCount = static_cast<UINT>(compressed_ ? (height + 3) / 4 : height);
        const UINT rowPitch = AlignRow(rowBytes);
        const std::size_t allocationBytes = static_cast<std::size_t>(rowPitch) * rowCount;
        auto upload = renderer_->AllocateFrameUploadEXT(
            allocationBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        for (UINT row = 0; row < rowCount; ++row)
            std::memcpy(upload.mapped + static_cast<std::size_t>(row) * rowPitch,
                        bytes + static_cast<std::size_t>(row) * rowBytes, rowBytes);

        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Offset = upload.offset;
        source.PlacedFootprint.Footprint.Format = format_;
        source.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
        source.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
        source.PlacedFootprint.Footprint.Depth = 1;
        source.PlacedFootprint.Footprint.RowPitch = rowPitch;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = static_cast<UINT>(SubresourceIndex(layer, mipLevel));

        ID3D12GraphicsCommandList* list = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(texture_.Get());
        auto& states = renderer_->GetResourceStateTrackerEXT();
        states.TransitionTo(list, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyTextureRegion(&destination, static_cast<UINT>(x), static_cast<UINT>(y), 0,
                                &source, nullptr);
        states.TransitionTo(list, texture_.Get(), kShaderReadable);
    }

    bool D3D12Texture2DArray::SetData(
        int layer, int mipLevel, int x, int y, int width, int height,
        const void* data, std::size_t byteCount)
    {
        if ((usage_ & 0x08u) == 0 || !data ||
            !ValidRegion(layer, mipLevel, x, y, width, height, byteCount))
            return false;
        auto& shadow = shadows_[SubresourceIndex(layer, mipLevel)];
        if (shadow.empty()) shadow.resize(MipBytes(mipLevel), 0);
        const int mipWidth = std::max(1, width_ >> mipLevel);
        const int mipHeight = std::max(1, height_ >> mipLevel);
        const std::size_t fullRow = static_cast<std::size_t>(
            compressed_ ? (mipWidth + 3) / 4 : mipWidth) * unitBytes_;
        const std::size_t rowBytes = static_cast<std::size_t>(
            compressed_ ? (width + 3) / 4 : width) * unitBytes_;
        const int rows = compressed_ ? (height + 3) / 4 : height;
        const int rowStart = compressed_ ? y / 4 : y;
        const int columnStart = compressed_ ? x / 4 : x;
        const auto* input = static_cast<const std::uint8_t*>(data);
        for (int row = 0; row < rows; ++row)
            std::memcpy(shadow.data() + static_cast<std::size_t>(rowStart + row) * fullRow +
                            static_cast<std::size_t>(columnStart) * unitBytes_,
                        input + static_cast<std::size_t>(row) * rowBytes, rowBytes);

        const bool wholeMip = x == 0 && y == 0 && width == mipWidth && height == mipHeight;
        if (compressed_ && !wholeMip &&
            (((x + width) & 3) != 0 || ((y + height) & 3) != 0))
            UploadBytes(layer, mipLevel, 0, 0, mipWidth, mipHeight, shadow.data());
        else
            UploadBytes(layer, mipLevel, x, y, width, height, input);
        return true;
    }

    bool D3D12Texture2DArray::GetData(
        int layer, int mipLevel, int x, int y, int width, int height,
        void* data, std::size_t byteCount) const
    {
        if ((usage_ & 0x04u) == 0 || !data ||
            !ValidRegion(layer, mipLevel, x, y, width, height, byteCount))
            return false;
        const int mipWidth = std::max(1, width_ >> mipLevel);
        const int mipHeight = std::max(1, height_ >> mipLevel);
        const int copiedWidth = compressed_ ? mipWidth : width;
        const int copiedHeight = compressed_ ? mipHeight : height;
        const UINT rowBytes = static_cast<UINT>(
            (compressed_ ? (copiedWidth + 3) / 4 : copiedWidth) * unitBytes_);
        const UINT rowCount = static_cast<UINT>(compressed_ ? (copiedHeight + 3) / 4
                                                         : copiedHeight);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT footprintRows = 0;
        UINT64 footprintRowBytes = 0;
        UINT64 totalBytes = 0;
        if (compressed_)
        {
            const D3D12_RESOURCE_DESC description = texture_->GetDesc();
            renderer_->GetDeviceEXT()->GetCopyableFootprints(
                &description, static_cast<UINT>(SubresourceIndex(layer, mipLevel)), 1, 0,
                &footprint, &footprintRows, &footprintRowBytes, &totalBytes);
        }
        const UINT rowPitch = compressed_ ? footprint.Footprint.RowPitch : AlignRow(rowBytes);
        const std::size_t allocationBytes = compressed_ ? static_cast<std::size_t>(totalBytes)
                                                       : static_cast<std::size_t>(rowPitch) * rowCount;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = allocationBytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if (FAILED(renderer_->GetDeviceEXT()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(readback.GetAddressOf()))))
            return false;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        if (!compressed_)
        {
            destination.PlacedFootprint.Footprint.Format = format_;
            destination.PlacedFootprint.Footprint.Width = static_cast<UINT>(copiedWidth);
            destination.PlacedFootprint.Footprint.Height = static_cast<UINT>(copiedHeight);
            destination.PlacedFootprint.Footprint.Depth = 1;
            destination.PlacedFootprint.Footprint.RowPitch = rowPitch;
        }
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = texture_.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = static_cast<UINT>(SubresourceIndex(layer, mipLevel));
        D3D12_BOX region{};
        region.left = static_cast<UINT>(x);
        region.top = static_cast<UINT>(y);
        region.right = static_cast<UINT>(x + width);
        region.bottom = static_cast<UINT>(y + height);
        region.back = 1;

        ID3D12GraphicsCommandList* list = renderer_->BeginImmediateCommandsEXT();
        auto& states = renderer_->GetResourceStateTrackerEXT();
        const auto previous = states.GetTrackedStateEXT(texture_.Get());
        states.TransitionTo(list, texture_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, compressed_ ? nullptr : &region);
        states.TransitionTo(list, texture_.Get(), previous);
        if (FAILED(list->Close())) return false;
        renderer_->ExecuteCommandListAndWaitEXT(list);

        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, allocationBytes};
        if (FAILED(readback->Map(0, &readRange, &mapped))) return false;
        const std::size_t wantedRow = static_cast<std::size_t>(
            compressed_ ? (width + 3) / 4 : width) * unitBytes_;
        const int wantedRows = compressed_ ? (height + 3) / 4 : height;
        const int firstRow = compressed_ ? y / 4 : 0;
        const std::size_t firstColumn = static_cast<std::size_t>(compressed_ ? x / 4 : 0)
                                        * unitBytes_;
        auto* output = static_cast<std::uint8_t*>(data);
        const auto* input = static_cast<const std::uint8_t*>(mapped);
        for (int row = 0; row < wantedRows; ++row)
            std::memcpy(output + static_cast<std::size_t>(row) * wantedRow,
                        input + static_cast<std::size_t>(firstRow + row) * rowPitch + firstColumn,
                        wantedRow);
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }

    void D3D12Texture2DArray::ReleaseDeviceResourcesEXT() noexcept
    {
        if (renderer_ && texture_)
            renderer_->GetResourceStateTrackerEXT().UntrackResource(texture_.Get());
        if (heaps_ && srvIndex_ != D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex)
            heaps_->cbvSrvUav.Free(srvIndex_);
        srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        heaps_.reset();
        texture_.Reset();
    }

    void D3D12Texture2DArray::RecreateDeviceResourcesEXT()
    {
        CreateResource();
        for (int layer = 0; layer < layers_; ++layer)
            for (int mip = 0; mip < mipLevels_; ++mip)
            {
                const auto& shadow = shadows_[SubresourceIndex(layer, mip)];
                if (!shadow.empty())
                    UploadBytes(layer, mip, 0, 0,
                                std::max(1, width_ >> mip), std::max(1, height_ >> mip),
                                shadow.data());
            }
    }
}
