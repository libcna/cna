// plan_dx.md Phase DX13 (DX-122).
#include "CNA/Internal/Backends/D3D12/D3D12Texture3D.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"

#include <algorithm>
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

        /// Mirrors D3D11Texture3DBackend.cpp's own CalculateMipLevels: full mip chain down to a
        /// 1x1x1 level.
        int CalculateMipLevels(int w, int h, int d)
        {
            int levels = 1;
            while (w > 1 || h > 1 || d > 1)
            {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                d = std::max(1, d / 2);
                ++levels;
            }
            return levels;
        }

        /// Same real "shader-readable, any stage" resting state D3D12TextureBackend.cpp's own
        /// kTextureShaderReadableState already established -- PIXEL_SHADER_RESOURCE isn't meaningful
        /// alone; MSDN's own D3D12_RESOURCE_STATES table lists both bits together for this purpose.
        constexpr D3D12_RESOURCE_STATES kTextureShaderReadableState =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    D3D12Texture3DBackend::D3D12Texture3DBackend(D3D12GraphicsBackend* backend,
                                                  int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
        : backend_(backend)
        , width_(w), height_(h), depth_(depth)
        , mipLevels_(mipMap ? CalculateMipLevels(w, h, depth) : 1)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.Width = static_cast<UINT64>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.DepthOrArraySize = static_cast<UINT16>(depth_); // TEXTURE3D's own "depth" meaning, not an array
        desc.MipLevels = static_cast<UINT16>(mipLevels_);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        HRESULT hr = backend_->GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture_.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DBackend: CreateCommittedResource failed, hr=" + FormatHr(hr));

        backend_->GetResourceStateTrackerEXT().TrackResource(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture3D.MipLevels = static_cast<UINT>(mipLevels_);

        backend_->AllocateCbvSrvUavDescriptorEXT(srvCpuHandle_, srvGpuHandle_);
        backend_->GetDeviceEXT()->CreateShaderResourceView(texture_.Get(), &srvDesc, srvCpuHandle_);

        // No initial pixel data (ITexture3DBackend's own construction contract, unlike
        // D3D12TextureBackend's ImageData-driven level-0 upload) -- transition straight to the
        // resting shader-readable state so this texture is always sampleable after construction,
        // matching D3D12TextureBackend's own "no initial pixels still ends up shader-readable"
        // guarantee.
        TransitionToShaderReadableEXT();
    }

    void D3D12Texture3DBackend::TransitionToShaderReadableEXT()
    {
        ID3D12CommandAllocator* allocator = backend_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        backend_->GetResourceStateTrackerEXT().TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        const HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DBackend: command list Close failed, hr=" + FormatHr(hr));
        backend_->ExecuteCommandListAndWaitEXT(cmdList);
    }

    void D3D12Texture3DBackend::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                        const void* data, int /*dataLength*/)
    {
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0) return;

        // Row-pitch-aligned staging BUFFER, one slice-pitch-sized region per Z slice -- same
        // discipline D3D12TextureBackend::UploadRegion already established for its own 2D case,
        // extended with a depth loop (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT governs row pitch only;
        // there is no separate per-slice alignment requirement for an UPLOAD-heap staging buffer,
        // unlike the resource's own internal tiled layout).
        const UINT rowPitch = AlignUp(static_cast<UINT>(w) * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT slicePitch = rowPitch * static_cast<UINT>(h);
        const UINT64 uploadBufferSize = static_cast<UINT64>(slicePitch) * static_cast<UINT64>(depth);

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
            throw std::runtime_error("D3D12Texture3DBackend: staging CreateCommittedResource failed, hr=" + FormatHr(hr));

        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        hr = staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DBackend: staging Map failed, hr=" + FormatHr(hr));

        // Source data is tightly packed (ITexture3DBackend::SetData's own contract, matching
        // D3D11Texture3DBackend's identical assumption): row stride = w*4, slice stride = w*h*4.
        const uint8_t* src = static_cast<const uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* srcRow = src
                    + static_cast<std::size_t>(slice) * static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4
                    + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4;
                uint8_t* dstRow = mapped
                    + static_cast<std::size_t>(slice) * slicePitch
                    + static_cast<std::size_t>(row) * rowPitch;
                std::memcpy(dstRow, srcRow, static_cast<std::size_t>(w) * 4);
            }
        }
        staging->Unmap(0, nullptr);

        // A TEXTURE3D resource has no array dimension -- the subresource index is unconditionally
        // just the mip level (unlike D3D12TextureBackend's own array-size-1 TEXTURE2D case, which
        // only collapses to this by coincidence of ArraySize=1; TEXTURE3D never has an array
        // dimension to begin with, so this isn't a simplification, it's the real formula).
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = texture_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = static_cast<UINT>(level);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = staging.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = 0;
        srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srcLoc.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        srcLoc.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        srcLoc.PlacedFootprint.Footprint.Depth = static_cast<UINT>(depth);
        srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

        ID3D12CommandAllocator* allocator = backend_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = backend_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyTextureRegion(&dst, static_cast<UINT>(x), static_cast<UINT>(y), static_cast<UINT>(z),
                                   &srcLoc, nullptr);
        tracker.TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DBackend::SetData: command list Close failed, hr=" + FormatHr(hr));
        backend_->ExecuteCommandListAndWaitEXT(cmdList); // synchronous -- staging is safe to release after this
    }

    bool D3D12Texture3DBackend::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                        void* data, int dataLength) const
    {
        // REMED-GFX-130: each silent `return` here became a complete transparent-black volume once
        // the shared layer converted its own zeroed scratch buffer regardless.
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0) return false;
        if (data == nullptr || dataLength < w * h * depth * 4) return false;

        const UINT rowPitch = AlignUp(static_cast<UINT>(w) * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT slicePitch = rowPitch * static_cast<UINT>(h);
        const UINT64 readbackBufferSize = static_cast<UINT64>(slicePitch) * static_cast<UINT64>(depth);

        D3D12_HEAP_PROPERTIES readbackHeapProps{};
        readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = readbackBufferSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        HRESULT hr = backend_->GetDeviceEXT()->CreateCommittedResource(
            &readbackHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr)) return false;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        dst.PlacedFootprint.Footprint.Depth = static_cast<UINT>(depth);
        dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = texture_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = static_cast<UINT>(level);

        D3D12_BOX srcBox{};
        srcBox.left = static_cast<UINT>(x);
        srcBox.top = static_cast<UINT>(y);
        srcBox.front = static_cast<UINT>(z);
        srcBox.right = static_cast<UINT>(x + w);
        srcBox.bottom = static_cast<UINT>(y + h);
        srcBox.back = static_cast<UINT>(z + depth);

        ID3D12CommandAllocator* allocator = backend_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = backend_->GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(texture_.Get());
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);
        tracker.TransitionTo(cmdList, texture_.Get(), priorState); // restore -- this is a read-only readback

        hr = cmdList->Close();
        if (FAILED(hr)) return false;
        backend_->ExecuteCommandListAndWaitEXT(cmdList);

        uint8_t* mapped = nullptr;
        const D3D12_RANGE mapRange{0, static_cast<SIZE_T>(readbackBufferSize)};
        if (FAILED(readback->Map(0, &mapRange, reinterpret_cast<void**>(&mapped)))) return false;

        uint8_t* out = static_cast<uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* srcRow = mapped
                    + static_cast<std::size_t>(slice) * slicePitch
                    + static_cast<std::size_t>(row) * rowPitch;
                uint8_t* dstRow = out
                    + static_cast<std::size_t>(slice) * static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4
                    + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4;
                std::memcpy(dstRow, srcRow, static_cast<std::size_t>(w) * 4);
            }
        }
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }
}
