// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX12/D3D12StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        constexpr D3D12_RESOURCE_STATES kShaderReadable =
            static_cast<D3D12_RESOURCE_STATES>(
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        [[nodiscard]] UINT AlignRow(UINT bytes)
        {
            return (bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                   ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        }
    }

    D3D12StorageTexture2D::D3D12StorageTexture2D(
        DirectX12Renderer* renderer, int width, int height, int mipLevels,
        int surfaceFormat, std::uint32_t usage)
        : owner_(renderer),
          renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12StorageTexture2D"),
          width_(width), height_(height), mipLevels_(mipLevels), usage_(usage)
    {
        format_ = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
        bytesPerTexel_ = D3DCommon::SurfaceFormatBytesPerTexel(surfaceFormat);
        if (!renderer || width <= 0 || height <= 0 || mipLevels <= 0 ||
            format_ == DXGI_FORMAT_UNKNOWN || bytesPerTexel_ <= 0 ||
            (usage & UINT32_C(0x03)) == 0)
            throw std::invalid_argument("D3D12 storage texture: invalid allocation description");
        shadows_.resize(static_cast<std::size_t>(mipLevels_));
        CreateResource();
        renderer_->RegisterRecoverableResourceEXT(this);
    }

    D3D12StorageTexture2D::~D3D12StorageTexture2D()
    {
        if (renderer_)
            renderer_->UnregisterRecoverableResourceEXT(this);
        ReleaseDeviceResourcesEXT();
    }

    void D3D12StorageTexture2D::CreateResource()
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = static_cast<UINT64>(width_);
        description.Height = static_cast<UINT>(height_);
        description.DepthOrArraySize = 1;
        description.MipLevels = static_cast<UINT16>(mipLevels_);
        description.Format = format_;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(renderer_->GetDeviceEXT()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &description,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(texture_.ReleaseAndGetAddressOf()))))
            throw std::runtime_error("D3D12 storage texture: native allocation failed");
        renderer_->GetResourceStateTrackerEXT().TrackResource(
            texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        heaps_ = renderer_->GetDescriptorHeapsEXT();
        try
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = format_;
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uav.Texture2D.MipSlice = 0;
            uavIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
                [&](D3D12_CPU_DESCRIPTOR_HANDLE handle)
                {
                    renderer_->GetDeviceEXT()->CreateUnorderedAccessView(
                        texture_.Get(), nullptr, &uav, handle);
                });
            if ((usage_ & UINT32_C(0x04)) != 0)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = format_;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Texture2D.MipLevels = static_cast<UINT>(mipLevels_);
                srvIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
                    [&](D3D12_CPU_DESCRIPTOR_HANDLE handle)
                    {
                        renderer_->GetDeviceEXT()->CreateShaderResourceView(
                            texture_.Get(), &srv, handle);
                    });
            }
            ID3D12GraphicsCommandList* list = renderer_->GetFrameCommandListEXT();
            renderer_->RetainFrameObjectEXT(texture_.Get());
            renderer_->GetResourceStateTrackerEXT().TransitionTo(
                list, texture_.Get(), kShaderReadable);
        }
        catch (...)
        {
            ReleaseDeviceResourcesEXT();
            throw;
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12StorageTexture2D::GetUavGpuHandleEXT() const
    {
        return heaps_ && uavIndex_ != D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex
            ? heaps_->cbvSrvUav.GpuHandle(uavIndex_)
            : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12StorageTexture2D::GetSrvGpuHandleEXT() const
    {
        return heaps_ && srvIndex_ != D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex
            ? heaps_->cbvSrvUav.GpuHandle(srvIndex_)
            : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    bool D3D12StorageTexture2D::ValidRegion(
        int mipLevel, int x, int y, int width, int height,
        std::size_t byteCount) const noexcept
    {
        if (mipLevel < 0 || mipLevel >= mipLevels_ ||
            x < 0 || y < 0 || width <= 0 || height <= 0)
            return false;
        const int mipWidth = std::max(1, width_ >> mipLevel);
        const int mipHeight = std::max(1, height_ >> mipLevel);
        return x <= mipWidth && width <= mipWidth - x &&
               y <= mipHeight && height <= mipHeight - y &&
               byteCount == static_cast<std::size_t>(width) * height * bytesPerTexel_;
    }

    void D3D12StorageTexture2D::UploadBytes(
        int mipLevel, int x, int y, int width, int height,
        const std::uint8_t* bytes)
    {
        const UINT rowBytes = static_cast<UINT>(width * bytesPerTexel_);
        const UINT rowPitch = AlignRow(rowBytes);
        auto upload = renderer_->AllocateFrameUploadEXT(
            static_cast<std::size_t>(rowPitch) * height,
            D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        for (int row = 0; row < height; ++row)
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
        destination.SubresourceIndex = static_cast<UINT>(mipLevel);

        ID3D12GraphicsCommandList* list = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(texture_.Get());
        auto& states = renderer_->GetResourceStateTrackerEXT();
        states.TransitionTo(list, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyTextureRegion(&destination, static_cast<UINT>(x), static_cast<UINT>(y), 0,
                                &source, nullptr);
        states.TransitionTo(list, texture_.Get(), kShaderReadable);
    }

    bool D3D12StorageTexture2D::SetData(
        int mipLevel, int x, int y, int width, int height,
        const void* data, std::size_t byteCount)
    {
        if ((usage_ & UINT32_C(0x20)) == 0 || !data ||
            !ValidRegion(mipLevel, x, y, width, height, byteCount))
            return false;
        auto& shadow = shadows_[static_cast<std::size_t>(mipLevel)];
        const int mipWidth = std::max(1, width_ >> mipLevel);
        const int mipHeight = std::max(1, height_ >> mipLevel);
        if (shadow.empty())
            shadow.resize(static_cast<std::size_t>(mipWidth) * mipHeight * bytesPerTexel_, 0);
        const auto* input = static_cast<const std::uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * bytesPerTexel_;
        const std::size_t mipRowBytes = static_cast<std::size_t>(mipWidth) * bytesPerTexel_;
        for (int row = 0; row < height; ++row)
            std::memcpy(shadow.data() + static_cast<std::size_t>(y + row) * mipRowBytes +
                            static_cast<std::size_t>(x) * bytesPerTexel_,
                        input + static_cast<std::size_t>(row) * rowBytes, rowBytes);
        UploadBytes(mipLevel, x, y, width, height, input);
        return true;
    }

    bool D3D12StorageTexture2D::GetData(
        int mipLevel, int x, int y, int width, int height,
        void* data, std::size_t byteCount) const
    {
        if ((usage_ & UINT32_C(0x10)) == 0 || !data ||
            !ValidRegion(mipLevel, x, y, width, height, byteCount))
            return false;
        const UINT rowBytes = static_cast<UINT>(width * bytesPerTexel_);
        const UINT rowPitch = AlignRow(rowBytes);
        const std::size_t allocationBytes = static_cast<std::size_t>(rowPitch) * height;
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
        destination.PlacedFootprint.Footprint.Format = format_;
        destination.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
        destination.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
        destination.PlacedFootprint.Footprint.Depth = 1;
        destination.PlacedFootprint.Footprint.RowPitch = rowPitch;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = texture_.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = static_cast<UINT>(mipLevel);
        const D3D12_BOX region{static_cast<UINT>(x), static_cast<UINT>(y), 0,
                               static_cast<UINT>(x + width),
                               static_cast<UINT>(y + height), 1};

        ID3D12GraphicsCommandList* list = renderer_->BeginImmediateCommandsEXT();
        auto& states = renderer_->GetResourceStateTrackerEXT();
        const auto previous = states.GetTrackedStateEXT(texture_.Get());
        states.TransitionTo(list, texture_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, &region);
        states.TransitionTo(list, texture_.Get(), previous);
        if (FAILED(list->Close())) return false;
        renderer_->ExecuteCommandListAndWaitEXT(list);

        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, allocationBytes};
        if (FAILED(readback->Map(0, &readRange, &mapped))) return false;
        auto* output = static_cast<std::uint8_t*>(data);
        const auto* input = static_cast<const std::uint8_t*>(mapped);
        for (int row = 0; row < height; ++row)
            std::memcpy(output + static_cast<std::size_t>(row) * rowBytes,
                        input + static_cast<std::size_t>(row) * rowPitch, rowBytes);
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }

    void D3D12StorageTexture2D::InvalidateRecoveryShadowEXT() noexcept
    {
        if (!shadows_.empty()) shadows_[0].clear();
    }

    void D3D12StorageTexture2D::ReleaseDeviceResourcesEXT() noexcept
    {
        if (renderer_ && texture_)
            renderer_->GetResourceStateTrackerEXT().UntrackResource(texture_.Get());
        if (heaps_)
        {
            heaps_->cbvSrvUav.Free(uavIndex_);
            heaps_->cbvSrvUav.Free(srvIndex_);
        }
        uavIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        heaps_.reset();
        texture_.Reset();
    }

    void D3D12StorageTexture2D::RecreateDeviceResourcesEXT()
    {
        CreateResource();
        for (int mip = 0; mip < mipLevels_; ++mip)
        {
            const auto& shadow = shadows_[static_cast<std::size_t>(mip)];
            if (!shadow.empty())
                UploadBytes(mip, 0, 0, std::max(1, width_ >> mip),
                            std::max(1, height_ >> mip), shadow.data());
        }
    }
}
