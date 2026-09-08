// plans/plan_dx.md Phase DX12 (DX-111, closing env_map3d).
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

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

        /// Mirrors D3D11TextureCubeRenderer and the public TextureCube level-count contract.
        int CalculateMipLevels(int size)
        {
            int levels = 1;
            while (size > 1)
            {
                size = std::max(1, size / 2);
                ++levels;
            }
            return levels;
        }

        // Same combined "shader-readable, any stage" resting state D3D12Textures.cpp's own
        // kTextureShaderReadableState already documents and uses -- duplicated locally rather than
        // shared, matching this renderer's own established per-file small-helper-duplication
        // precedent (e.g. VertexCountForPrimitives in DirectX12Renderer.cpp).
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
                    "D3D12TextureCubeRenderer: unsupported SurfaceFormat::" +
                    std::string(SurfaceFormatName(surfaceFormat)) + " (ordinal " +
                    std::to_string(surfaceFormat) + ").");
            }
        }

        std::size_t CompressedLevelByteCount(int size, int bytesPerBlock)
        {
            return static_cast<std::size_t>((size + 3) / 4) *
                   static_cast<std::size_t>((size + 3) / 4) *
                   static_cast<std::size_t>(bytesPerBlock);
        }

        std::vector<std::uint8_t> DecompressLevel(
            int surfaceFormat, const std::vector<std::uint8_t>& blocks, int width, int height)
        {
            using CNA::Internal::Graphics::DxtUtil;
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
                case SurfaceFormat::Dxt1:
                    return DxtUtil::DecompressDxt1(blocks.data(), blocks.size(), width, height);
                case SurfaceFormat::Dxt3:
                    return DxtUtil::DecompressDxt3(blocks.data(), blocks.size(), width, height);
                case SurfaceFormat::Dxt5:
                    return DxtUtil::DecompressDxt5(blocks.data(), blocks.size(), width, height);
                default:
                    return {};
            }
        }
    }

    D3D12TextureCubeRenderer::D3D12TextureCubeRenderer(
        DirectX12Renderer* renderer, int size, bool mipMap, int surfaceFormat)
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12TextureCubeRenderer"),
          size_(size), mipLevels_(mipMap ? CalculateMipLevels(size) : 1),
          surfaceFormat_(surfaceFormat)
    {
        ResolveSurfaceFormat(surfaceFormat_, dxgiFormat_, bytesPerTexel_, compressed_,
                             bytesPerBlock_);
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(size_);
        desc.Height = static_cast<UINT>(size_);
        desc.DepthOrArraySize = 6;
        desc.MipLevels = static_cast<UINT16>(mipLevels_);
        desc.Format = dxgiFormat_;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture_.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureCubeRenderer: CreateCommittedResource failed, hr=" + FormatHr(hr));

        renderer_->GetResourceStateTrackerEXT().TrackResource(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = dxgiFormat_;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(mipLevels_);

        heaps_ = renderer_->GetDescriptorHeapsEXT();
        srvIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
            [&](D3D12_CPU_DESCRIPTOR_HANDLE cpu)
            {
                renderer_->GetDeviceEXT()->CreateShaderResourceView(texture_.Get(), &srvDesc, cpu);
            });

        if (compressed_)
        {
            compressedLevels_.resize(static_cast<std::size_t>(6 * mipLevels_));
            for (int face = 0; face < 6; ++face)
                for (int level = 0; level < mipLevels_; ++level)
                    compressedLevels_[static_cast<std::size_t>(face * mipLevels_ + level)].assign(
                        CompressedLevelByteCount(std::max(1, size_ >> level), bytesPerBlock_), 0);
        }

        // No initial pixel data (matches D3D11TextureCubeRenderer's own constructor shape -- content
        // arrives later via SetData()) -- transition straight to the real shader-readable resting
        // state now, same "always shader-readable after construction" convention D3D12TextureRenderer
        // already established for its own no-initial-pixels case.
        TransitionToShaderReadableEXT();
    }

    D3D12TextureCubeRenderer::~D3D12TextureCubeRenderer()
    {
        if (heaps_) heaps_->cbvSrvUav.Free(srvIndex_);
    }

    void D3D12TextureCubeRenderer::TransitionToShaderReadableEXT()
    {
        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        renderer_->GetResourceStateTrackerEXT().TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        const HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureCubeRenderer: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList);
    }

    bool D3D12TextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                          const void* data, int dataLength)
    {
        // REMED-GFX-135: this used to be a silent `return` the shared layer read as a completed
        // upload.
        if (compressed_ || level < 0 || level >= mipLevels_ || face < 0 || face >= 6 ||
            w <= 0 || h <= 0)
            return false;
        if (data == nullptr) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t required = rowBytes * static_cast<std::size_t>(h);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        const UINT rowPitch = AlignUp(static_cast<UINT>(rowBytes), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT64 uploadBufferSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

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
            throw std::runtime_error("D3D12TextureCubeRenderer: staging CreateCommittedResource failed, hr=" + FormatHr(hr));

        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        hr = staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureCubeRenderer: staging Map failed, hr=" + FormatHr(hr));
        const uint8_t* src = static_cast<const uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(mapped + static_cast<std::size_t>(row) * rowPitch,
                        src + static_cast<std::size_t>(row) * rowBytes, rowBytes);
        }
        staging->Unmap(0, nullptr);

        // Single-plane, ArraySize=6 -> D3D12CalcSubresource()'s formula collapses to
        // level + face*mipLevels_ (see this file's own header comment for why it's computed
        // directly rather than via d3dx12.h, which isn't present in this MinGW-w64 install).
        const UINT subresource = static_cast<UINT>(level) + static_cast<UINT>(face) * static_cast<UINT>(mipLevels_);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = texture_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = staging.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = 0;
        srcLoc.PlacedFootprint.Footprint.Format = dxgiFormat_;
        srcLoc.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        srcLoc.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        srcLoc.PlacedFootprint.Footprint.Depth = 1;
        srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyTextureRegion(&dst, static_cast<UINT>(x), static_cast<UINT>(y), 0, &srcLoc, nullptr);
        tracker.TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureCubeRenderer: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList); // synchronous -- staging is safe to release after this
        return true;
    }

    bool D3D12TextureCubeRenderer::SetCompressedDataEXT(
        int face, int level, int x, int y, int w, int h, const void* data, int dataLength)
    {
        if (!compressed_ || level < 0 || level >= mipLevels_ || face < 0 || face >= 6 ||
            data == nullptr || w <= 0 || h <= 0)
            return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize ||
            (x % 4) != 0 || (y % 4) != 0 ||
            ((w % 4) != 0 && x + w != levelSize) ||
            ((h % 4) != 0 && y + h != levelSize))
            return false;

        const int blockCols = (w + 3) / 4;
        const int blockRows = (h + 3) / 4;
        const std::size_t rowBytes = static_cast<std::size_t>(blockCols) * bytesPerBlock_;
        const std::size_t required = rowBytes * static_cast<std::size_t>(blockRows);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        const int levelBlockCols = (levelSize + 3) / 4;
        const int levelBlockRows = (levelSize + 3) / 4;
        const std::size_t levelRowBytes =
            static_cast<std::size_t>(levelBlockCols) * bytesPerBlock_;
        auto& levelBlocks =
            compressedLevels_[static_cast<std::size_t>(face * mipLevels_ + level)];
        for (int row = 0; row < blockRows; ++row)
        {
            std::memcpy(levelBlocks.data() +
                            static_cast<std::size_t>(y / 4 + row) * levelRowBytes +
                            static_cast<std::size_t>(x / 4) * bytesPerBlock_,
                        static_cast<const std::uint8_t*>(data) +
                            static_cast<std::size_t>(row) * rowBytes,
                        rowBytes);
        }

        const UINT rowPitch = AlignUp(static_cast<UINT>(levelRowBytes),
                                      D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT64 uploadBufferSize = static_cast<UINT64>(rowPitch) * levelBlockRows;
        D3D12_HEAP_PROPERTIES uploadHeapProps{};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = uploadBufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> staging;
        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D12TextureCubeRenderer: compressed staging creation failed, hr=" + FormatHr(hr));

        std::uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        hr = staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D12TextureCubeRenderer: compressed staging Map failed, hr=" + FormatHr(hr));
        for (int row = 0; row < levelBlockRows; ++row)
            std::memcpy(mapped + static_cast<std::size_t>(row) * rowPitch,
                        levelBlocks.data() + static_cast<std::size_t>(row) * levelRowBytes,
                        levelRowBytes);
        staging->Unmap(0, nullptr);

        const UINT subresource = static_cast<UINT>(level) +
                                 static_cast<UINT>(face) * static_cast<UINT>(mipLevels_);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = subresource;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = staging.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint.Format = dxgiFormat_;
        source.PlacedFootprint.Footprint.Width = static_cast<UINT>(levelSize);
        source.PlacedFootprint.Footprint.Height = static_cast<UINT>(levelSize);
        source.PlacedFootprint.Footprint.Depth = 1;
        source.PlacedFootprint.Footprint.RowPitch = rowPitch;

        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* commandList = renderer_->GetCommandListEXT();
        allocator->Reset();
        commandList->Reset(allocator, nullptr);
        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(commandList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        tracker.TransitionTo(commandList, texture_.Get(), kTextureShaderReadableState);
        hr = commandList->Close();
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D12TextureCubeRenderer: compressed command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(commandList);
        return true;
    }

    bool D3D12TextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        // REMED-GFX-130: see D3D12Texture3DRenderer::GetData -- silent returns fabricated a face.
        if (level < 0 || level >= mipLevels_ || face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (data == nullptr) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (compressed_)
        {
            const std::size_t required = static_cast<std::size_t>(w) * h * 4u;
            if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;
            const auto& blocks =
                compressedLevels_[static_cast<std::size_t>(face * mipLevels_ + level)];
            const auto rgba = DecompressLevel(surfaceFormat_, blocks, levelSize, levelSize);
            if (rgba.size() < static_cast<std::size_t>(levelSize) * levelSize * 4u) return false;
            auto* destination = static_cast<std::uint8_t*>(data);
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(destination + static_cast<std::size_t>(row) * w * 4u,
                            rgba.data() +
                                (static_cast<std::size_t>(y + row) * levelSize + x) * 4u,
                            static_cast<std::size_t>(w) * 4u);
            }
            return true;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t required = rowBytes * static_cast<std::size_t>(h);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        const UINT rowPitch = AlignUp(static_cast<UINT>(rowBytes), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT64 readbackBufferSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

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
        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &readbackHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr)) return false;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format = dxgiFormat_;
        dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

        // Same face/level -> subresource formula SetData() already established (single-plane,
        // ArraySize=6 collapses D3D12CalcSubresource() to level + face*mipLevels_).
        const UINT subresource = static_cast<UINT>(level) + static_cast<UINT>(face) * static_cast<UINT>(mipLevels_);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = texture_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = subresource;

        D3D12_BOX srcBox{};
        srcBox.left = static_cast<UINT>(x);
        srcBox.top = static_cast<UINT>(y);
        srcBox.front = 0;
        srcBox.right = static_cast<UINT>(x + w);
        srcBox.bottom = static_cast<UINT>(y + h);
        srcBox.back = 1;

        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(texture_.Get());
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);
        tracker.TransitionTo(cmdList, texture_.Get(), priorState); // restore -- this is a read-only readback

        hr = cmdList->Close();
        if (FAILED(hr)) return false;
        renderer_->ExecuteCommandListAndWaitEXT(cmdList);

        uint8_t* mapped = nullptr;
        const D3D12_RANGE mapRange{0, static_cast<SIZE_T>(readbackBufferSize)};
        if (FAILED(readback->Map(0, &mapRange, reinterpret_cast<void**>(&mapped)))) return false;

        uint8_t* out = static_cast<uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* srcRow = mapped + static_cast<std::size_t>(row) * rowPitch;
            uint8_t* dstRow = out + static_cast<std::size_t>(row) * rowBytes;
            std::memcpy(dstRow, srcRow, rowBytes);
        }
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }
}
