// plan_dx.md Phase DX12 (DX-109).
#include "CNA/Internal/Backends/D3D12/D3D12Textures.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Backends::D3D12
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        UINT AlignUp(UINT value, UINT alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        /// The resting "readable by any shader stage" combined state this backend transitions every
        /// texture into once its content is ready -- not D3D12_RESOURCE_STATE_GENERIC_READ (that
        /// combination is meant for buffer-shaped usages like vertex/index/constant reads, not
        /// textures; MSDN's own D3D12_RESOURCE_STATES table lists PIXEL_SHADER_RESOURCE |
        /// NON_PIXEL_SHADER_RESOURCE as the correct "shader-readable, any stage" texture state).
        constexpr D3D12_RESOURCE_STATES kTextureShaderReadableState =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    D3D12TextureBackend::D3D12TextureBackend(D3D12GraphicsBackend* backend, const ImageData& data)
        : backend_(backend)
        , width_(data.width), height_(data.height)
        , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<UINT16>(mipLevels_);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // driver-chosen tiled layout, standard for TEXTURE2D

        // COPY_DEST is a legal initial state for a DEFAULT-heap texture that (per this backend's own
        // constructor flow) is about to receive its level-0 upload -- and, when there IS no initial
        // pixel data, gets deliberately transitioned to kTextureShaderReadableState below rather
        // than left here.
        HRESULT hr = backend_->GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture_.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureBackend: CreateCommittedResource failed, hr=" + FormatHr(hr));

        backend_->GetResourceStateTrackerEXT().TrackResource(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(mipLevels_);

        heaps_ = backend_->GetDescriptorHeapsEXT();
        srvIndex_ = backend_->CreateCbvSrvUavDescriptorEXT(
            [&](D3D12_CPU_DESCRIPTOR_HANDLE cpu)
            {
                backend_->GetDeviceEXT()->CreateShaderResourceView(texture_.Get(), &srvDesc, cpu);
            });

        if (!data.pixels.empty())
        {
            UploadRegion(0, data.pixels.data(), width_, height_, width_ * 4);
        }
        else
        {
            TransitionToShaderReadableEXT();
        }
    }

    D3D12TextureBackend::~D3D12TextureBackend()
    {
        if (heaps_) heaps_->cbvSrvUav.Free(srvIndex_);
    }

    void D3D12TextureBackend::UploadRegion(
        int level, const uint8_t* rgba, int levelW, int levelH, int sourceStrideBytes)
    {
        const UINT rowPitch = AlignUp(static_cast<UINT>(levelW) * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT64 uploadBufferSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(levelH);

        D3D12_HEAP_PROPERTIES uploadHeapProps{};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = uploadBufferSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> staging;
        HRESULT hr = backend_->GetDeviceEXT()->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureBackend: staging CreateCommittedResource failed, hr=" + FormatHr(hr));

        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        hr = staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureBackend: staging Map failed, hr=" + FormatHr(hr));
        for (int row = 0; row < levelH; ++row)
        {
            std::memcpy(mapped + static_cast<std::size_t>(row) * rowPitch,
                        rgba + static_cast<std::size_t>(row) * sourceStrideBytes,
                        static_cast<std::size_t>(levelW) * 4);
        }
        staging->Unmap(0, nullptr);

        // Array size 1, single plane -> the general D3D12CalcSubresource() formula
        // (MipSlice + ArraySlice*MipLevels + PlaneSlice*MipLevels*ArraySize) collapses to the mip
        // level itself. d3dx12.h (which normally provides that helper) is not present in this
        // MinGW-w64 install (DX-100's own finding) -- computed directly instead, see file header.
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = texture_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = static_cast<UINT>(level);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = staging.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = static_cast<UINT>(levelW);
        src.PlacedFootprint.Footprint.Height = static_cast<UINT>(levelH);
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = rowPitch;

        ID3D12CommandAllocator* allocator = backend_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = backend_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        tracker.TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureBackend: command list Close failed, hr=" + FormatHr(hr));
        backend_->ExecuteCommandListAndWaitEXT(cmdList); // synchronous -- staging is safe to release after this
    }

    void D3D12TextureBackend::TransitionToShaderReadableEXT()
    {
        ID3D12CommandAllocator* allocator = backend_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        backend_->GetResourceStateTrackerEXT().TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        const HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureBackend: command list Close failed, hr=" + FormatHr(hr));
        backend_->ExecuteCommandListAndWaitEXT(cmdList);
    }

    void D3D12TextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        const int sourceStride = stride > 0 ? stride : width_ * 4;
        UploadRegion(0, rgba, width_, height_, sourceStride);
    }

    void D3D12TextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level < 0 || level >= mipLevels_) return;
        UploadRegion(level, rgba, levelW, levelH, levelW * 4);
    }
}
