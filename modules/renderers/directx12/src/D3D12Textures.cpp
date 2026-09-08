// plans/plan_dx.md Phase DX12 (DX-109).
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
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

        /// The resting "readable by any shader stage" combined state this renderer transitions every
        /// texture into once its content is ready -- not D3D12_RESOURCE_STATE_GENERIC_READ (that
        /// combination is meant for buffer-shaped usages like vertex/index/constant reads, not
        /// textures; MSDN's own D3D12_RESOURCE_STATES table lists PIXEL_SHADER_RESOURCE |
        /// NON_PIXEL_SHADER_RESOURCE as the correct "shader-readable, any stage" texture state).
        constexpr D3D12_RESOURCE_STATES kTextureShaderReadableState =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

        void ResolveSurfaceFormat(int surfaceFormat, DXGI_FORMAT& dxgiFormat, int& bytesPerTexel,
                                  bool& compressed, int& bytesPerBlock)
        {
            using namespace D3DCommon;
            dxgiFormat = SurfaceFormatToDxgi(surfaceFormat);
            bytesPerTexel = SurfaceFormatBytesPerTexel(surfaceFormat);
            compressed = IsXnaBlockCompressedSurfaceFormat(surfaceFormat);
            bytesPerBlock = SurfaceFormatBytesPerBlock(surfaceFormat);
            if ((!IsXnaUncompressedSurfaceFormat(surfaceFormat) && !compressed) ||
                dxgiFormat == DXGI_FORMAT_UNKNOWN ||
                (compressed ? bytesPerBlock == 0 : bytesPerTexel == 0))
            {
                throw std::invalid_argument(
                    "D3D12TextureRenderer: unsupported SurfaceFormat::" +
                    std::string(SurfaceFormatName(surfaceFormat)) + " (ordinal " +
                    std::to_string(surfaceFormat) + ").");
            }
        }
    }

    D3D12TextureRenderer::D3D12TextureRenderer(DirectX12Renderer* renderer, const ImageData& data)
        : renderer_(renderer)
        , width_(data.width), height_(data.height)
        , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
        , surfaceFormat_(data.surfaceFormat)
    {
        ResolveSurfaceFormat(surfaceFormat_, dxgiFormat_, bytesPerTexel_, compressed_,
                             bytesPerBlock_);
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<UINT16>(mipLevels_);
        desc.Format = dxgiFormat_;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // driver-chosen tiled layout, standard for TEXTURE2D

        // COPY_DEST is a legal initial state for a DEFAULT-heap texture that (per this renderer's own
        // constructor flow) is about to receive its level-0 upload -- and, when there IS no initial
        // pixel data, gets deliberately transitioned to kTextureShaderReadableState below rather
        // than left here.
        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture_.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureRenderer: CreateCommittedResource failed, hr=" + FormatHr(hr));

        renderer_->GetResourceStateTrackerEXT().TrackResource(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = dxgiFormat_;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(mipLevels_);

        heaps_ = renderer_->GetDescriptorHeapsEXT();
        srvIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
            [&](D3D12_CPU_DESCRIPTOR_HANDLE cpu)
            {
                renderer_->GetDeviceEXT()->CreateShaderResourceView(texture_.Get(), &srvDesc, cpu);
            });

        if (!data.pixels.empty())
        {
            const int rowUnits = compressed_ ? (width_ + 3) / 4 : width_;
            const int rowCount = compressed_ ? (height_ + 3) / 4 : height_;
            const int unitBytes = compressed_ ? bytesPerBlock_ : bytesPerTexel_;
            const std::size_t rowBytes = static_cast<std::size_t>(rowUnits) * unitBytes;
            const std::size_t required = rowBytes * static_cast<std::size_t>(rowCount);
            if (data.pixels.size() < required)
                throw std::invalid_argument(
                    "D3D12TextureRenderer: level-zero pixel buffer is too small for SurfaceFormat::" +
                    std::string(D3DCommon::SurfaceFormatName(surfaceFormat_)) + ".");
            UploadRegion(0, data.pixels.data(), width_, height_, static_cast<int>(rowBytes));
        }
        else
        {
            TransitionToShaderReadableEXT();
        }
    }

    D3D12TextureRenderer::~D3D12TextureRenderer()
    {
        if (heaps_) heaps_->cbvSrvUav.Free(srvIndex_);
    }

    void D3D12TextureRenderer::UploadRegion(
        int level, const uint8_t* rgba, int levelW, int levelH, int sourceStrideBytes)
    {
        const UINT rowBytes = compressed_
            ? static_cast<UINT>(((levelW + 3) / 4) * bytesPerBlock_)
            : static_cast<UINT>(levelW * bytesPerTexel_);
        const int rowCount = compressed_ ? (levelH + 3) / 4 : levelH;
        const UINT rowPitch = AlignUp(rowBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT64 uploadBufferSize = static_cast<UINT64>(rowPitch) *
                                        static_cast<UINT64>(rowCount);

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
        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureRenderer: staging CreateCommittedResource failed, hr=" + FormatHr(hr));

        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        hr = staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureRenderer: staging Map failed, hr=" + FormatHr(hr));
        for (int row = 0; row < rowCount; ++row)
        {
            std::memcpy(mapped + static_cast<std::size_t>(row) * rowPitch,
                        rgba + static_cast<std::size_t>(row) * sourceStrideBytes,
                        rowBytes);
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
        src.PlacedFootprint.Footprint.Format = dxgiFormat_;
        src.PlacedFootprint.Footprint.Width = static_cast<UINT>(levelW);
        src.PlacedFootprint.Footprint.Height = static_cast<UINT>(levelH);
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = rowPitch;

        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        tracker.TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureRenderer: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList); // synchronous -- staging is safe to release after this
    }

    void D3D12TextureRenderer::TransitionToShaderReadableEXT()
    {
        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        renderer_->GetResourceStateTrackerEXT().TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        const HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureRenderer: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList);
    }

    void D3D12TextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        const int nativeStride = compressed_ ? ((width_ + 3) / 4) * bytesPerBlock_
                                             : width_ * bytesPerTexel_;
        const int sourceStride = stride > 0 ? stride : nativeStride;
        UploadRegion(0, rgba, width_, height_, sourceStride);
    }

    void D3D12TextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level < 0 || level >= mipLevels_) return;
        const int sourceStride = compressed_ ? ((levelW + 3) / 4) * bytesPerBlock_
                                             : levelW * bytesPerTexel_;
        UploadRegion(level, rgba, levelW, levelH, sourceStride);
    }

    bool D3D12TextureRenderer::GetData(int level, int x, int y, int w, int h,
                                       void* data, int dataLength) const
    {
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || data == nullptr) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH) return false;
        if (compressed_ && ((x % 4) != 0 || (y % 4) != 0 ||
                            ((w % 4) != 0 && x + w != levelW) ||
                            ((h % 4) != 0 && y + h != levelH)))
            return false;
        const int rowCount = compressed_ ? (h + 3) / 4 : h;
        const std::size_t rowBytes = compressed_
            ? static_cast<std::size_t>((w + 3) / 4) * bytesPerBlock_
            : static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t required = rowBytes * static_cast<std::size_t>(rowCount);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        const UINT rowPitch = AlignUp(static_cast<UINT>(rowBytes),
                                      D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT64 readbackBufferSize = static_cast<UINT64>(rowPitch) * rowCount;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = readbackBufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr)) return false;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = dxgiFormat_;
        dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = texture_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = static_cast<UINT>(level);
        D3D12_BOX srcBox{static_cast<UINT>(x), static_cast<UINT>(y), 0,
                         static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1};

        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);
        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(texture_.Get());
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);
        tracker.TransitionTo(cmdList, texture_.Get(), priorState);
        hr = cmdList->Close();
        if (FAILED(hr)) return false;
        renderer_->ExecuteCommandListAndWaitEXT(cmdList);

        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, static_cast<SIZE_T>(readbackBufferSize)};
        if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) return false;
        auto* out = static_cast<uint8_t*>(data);
        for (int row = 0; row < rowCount; ++row)
            std::memcpy(out + static_cast<std::size_t>(row) * rowBytes,
                        mapped + static_cast<std::size_t>(row) * rowPitch, rowBytes);
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }
}
