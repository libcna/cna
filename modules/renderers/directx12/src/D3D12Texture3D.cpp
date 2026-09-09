// plans/plan_dx.md Phase DX13 (DX-122).
#include "CNA/Internal/Renderers/DirectX12/D3D12Texture3D.hpp"
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

        /// Mirrors D3D11Texture3DRenderer.cpp's own CalculateMipLevels: full mip chain down to a
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

        /// Same real "shader-readable, any stage" resting state D3D12TextureRenderer.cpp's own
        /// kTextureShaderReadableState already established -- PIXEL_SHADER_RESOURCE isn't meaningful
        /// alone; MSDN's own D3D12_RESOURCE_STATES table lists both bits together for this purpose.
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
                    "D3D12Texture3DRenderer: unsupported SurfaceFormat::" +
                    std::string(SurfaceFormatName(surfaceFormat)) + " (ordinal " +
                    std::to_string(surfaceFormat) + ").");
            }
        }

        std::size_t CompressedVolumeLevelByteCount(
            int width, int height, int depth, int bytesPerBlock)
        {
            return static_cast<std::size_t>((width + 3) / 4) *
                   static_cast<std::size_t>((height + 3) / 4) *
                   static_cast<std::size_t>(depth) *
                   static_cast<std::size_t>(bytesPerBlock);
        }

        std::vector<std::uint8_t> DecompressLevel(
            int surfaceFormat, const std::uint8_t* blocks, std::size_t byteCount,
            int width, int height)
        {
            using CNA::Internal::Graphics::DxtUtil;
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
                case SurfaceFormat::Dxt1:
                    return DxtUtil::DecompressDxt1(blocks, byteCount, width, height);
                case SurfaceFormat::Dxt3:
                    return DxtUtil::DecompressDxt3(blocks, byteCount, width, height);
                case SurfaceFormat::Dxt5:
                    return DxtUtil::DecompressDxt5(blocks, byteCount, width, height);
                default:
                    return {};
            }
        }
    }

    D3D12Texture3DRenderer::D3D12Texture3DRenderer(DirectX12Renderer* renderer,
                                                  int w, int h, int depth, bool mipMap, int surfaceFormat)
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12Texture3DRenderer")
        , width_(w), height_(h), depth_(depth)
        , mipLevels_(mipMap ? CalculateMipLevels(w, h, depth) : 1)
        , surfaceFormat_(surfaceFormat)
    {
        ResolveSurfaceFormat(surfaceFormat_, dxgiFormat_, bytesPerTexel_, compressed_,
                             bytesPerBlock_);
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.Width = static_cast<UINT64>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.DepthOrArraySize = static_cast<UINT16>(depth_); // TEXTURE3D's own "depth" meaning, not an array
        desc.MipLevels = static_cast<UINT16>(mipLevels_);
        desc.Format = dxgiFormat_;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture_.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DRenderer: CreateCommittedResource failed, hr=" + FormatHr(hr));

        renderer_->GetResourceStateTrackerEXT().TrackResource(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = dxgiFormat_;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture3D.MipLevels = static_cast<UINT>(mipLevels_);

        heaps_ = renderer_->GetDescriptorHeapsEXT();
        srvIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
            [&](D3D12_CPU_DESCRIPTOR_HANDLE cpu)
            {
                renderer_->GetDeviceEXT()->CreateShaderResourceView(texture_.Get(), &srvDesc, cpu);
            });

        if (compressed_)
        {
            compressedLevels_.resize(static_cast<std::size_t>(mipLevels_));
            for (int level = 0; level < mipLevels_; ++level)
            {
                compressedLevels_[static_cast<std::size_t>(level)].assign(
                    CompressedVolumeLevelByteCount(
                        std::max(1, width_ >> level), std::max(1, height_ >> level),
                        std::max(1, depth_ >> level), bytesPerBlock_),
                    0);
            }
        }

        // No initial pixel data (ITexture3DRenderer's own construction contract, unlike
        // D3D12TextureRenderer's ImageData-driven level-0 upload) -- transition straight to the
        // resting shader-readable state so this texture is always sampleable after construction,
        // matching D3D12TextureRenderer's own "no initial pixels still ends up shader-readable"
        // guarantee.
        TransitionToShaderReadableEXT();
    }

    D3D12Texture3DRenderer::~D3D12Texture3DRenderer()
    {
        if (heaps_) heaps_->cbvSrvUav.Free(srvIndex_);
    }

    void D3D12Texture3DRenderer::TransitionToShaderReadableEXT()
    {
        ID3D12GraphicsCommandList* cmdList = renderer_->BeginImmediateCommandsEXT();

        renderer_->GetResourceStateTrackerEXT().TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        const HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DRenderer: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList);
    }

    bool D3D12Texture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                        const void* data, int dataLength)
    {
        // REMED-GFX-135: this used to be a silent `return` the shared layer read as a completed
        // upload.
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0) return false;
        if (data == nullptr) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        if (compressed_ && ((x % 4) != 0 || (y % 4) != 0 ||
                            ((w % 4) != 0 && x + w != levelW) ||
                            ((h % 4) != 0 && y + h != levelH)))
            return false;
        const int rowCount = compressed_ ? (h + 3) / 4 : h;
        const std::size_t rowBytes = compressed_
            ? static_cast<std::size_t>((w + 3) / 4) * bytesPerBlock_
            : static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t tightSliceBytes = rowBytes * static_cast<std::size_t>(rowCount);
        const std::size_t required = tightSliceBytes * static_cast<std::size_t>(depth);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        if (compressed_)
        {
            const int levelBlockCols = (levelW + 3) / 4;
            const int levelBlockRows = (levelH + 3) / 4;
            const std::size_t levelRowBytes =
                static_cast<std::size_t>(levelBlockCols) * bytesPerBlock_;
            const std::size_t levelSliceBytes =
                levelRowBytes * static_cast<std::size_t>(levelBlockRows);
            auto& levelBlocks = compressedLevels_[static_cast<std::size_t>(level)];
            for (int slice = 0; slice < depth; ++slice)
            {
                for (int row = 0; row < rowCount; ++row)
                {
                    std::memcpy(
                        levelBlocks.data() +
                            static_cast<std::size_t>(z + slice) * levelSliceBytes +
                            static_cast<std::size_t>(y / 4 + row) * levelRowBytes +
                            static_cast<std::size_t>(x / 4) * bytesPerBlock_,
                        static_cast<const std::uint8_t*>(data) +
                            static_cast<std::size_t>(slice) * tightSliceBytes +
                            static_cast<std::size_t>(row) * rowBytes,
                        rowBytes);
                }
            }
        }

        // Row-pitch-aligned staging BUFFER, one slice-pitch-sized region per Z slice -- same
        // discipline D3D12TextureRenderer::UploadRegion already established for its own 2D case,
        // extended with a depth loop (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT governs row pitch only;
        // there is no separate per-slice alignment requirement for an UPLOAD-heap staging buffer,
        // unlike the resource's own internal tiled layout).
        const UINT rowPitch = AlignUp(static_cast<UINT>(rowBytes), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const UINT slicePitch = rowPitch * static_cast<UINT>(rowCount);
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
        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DRenderer: staging CreateCommittedResource failed, hr=" + FormatHr(hr));

        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        hr = staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DRenderer: staging Map failed, hr=" + FormatHr(hr));

        // Source data is tightly packed in the texture's native SurfaceFormat.
        const uint8_t* src = static_cast<const uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < rowCount; ++row)
            {
                const uint8_t* srcRow = src
                    + static_cast<std::size_t>(slice) * tightSliceBytes
                    + static_cast<std::size_t>(row) * rowBytes;
                uint8_t* dstRow = mapped
                    + static_cast<std::size_t>(slice) * slicePitch
                    + static_cast<std::size_t>(row) * rowPitch;
                std::memcpy(dstRow, srcRow, rowBytes);
            }
        }
        staging->Unmap(0, nullptr);

        // A TEXTURE3D resource has no array dimension -- the subresource index is unconditionally
        // just the mip level (unlike D3D12TextureRenderer's own array-size-1 TEXTURE2D case, which
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
        srcLoc.PlacedFootprint.Footprint.Format = dxgiFormat_;
        srcLoc.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        srcLoc.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        srcLoc.PlacedFootprint.Footprint.Depth = static_cast<UINT>(depth);
        srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

        ID3D12GraphicsCommandList* cmdList = renderer_->BeginImmediateCommandsEXT();

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyTextureRegion(&dst, static_cast<UINT>(x), static_cast<UINT>(y), static_cast<UINT>(z),
                                   &srcLoc, nullptr);
        tracker.TransitionTo(cmdList, texture_.Get(), kTextureShaderReadableState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12Texture3DRenderer::SetData: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList); // synchronous -- staging is safe to release after this
        return true;
    }

    bool D3D12Texture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                        void* data, int dataLength) const
    {
        // REMED-GFX-130: each silent `return` here became a complete transparent-black volume once
        // the shared layer converted its own zeroed scratch buffer regardless.
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0) return false;
        if (data == nullptr) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        if (compressed_)
        {
            const std::size_t required = static_cast<std::size_t>(w) * h * depth * 4u;
            if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;
            const int levelBlockRows = (levelH + 3) / 4;
            const std::size_t levelSliceBytes =
                static_cast<std::size_t>((levelW + 3) / 4) * levelBlockRows * bytesPerBlock_;
            const auto& levelBlocks = compressedLevels_[static_cast<std::size_t>(level)];
            auto* destination = static_cast<std::uint8_t*>(data);
            for (int slice = 0; slice < depth; ++slice)
            {
                const std::uint8_t* sliceBlocks = levelBlocks.data() +
                    static_cast<std::size_t>(z + slice) * levelSliceBytes;
                const auto rgba = DecompressLevel(
                    surfaceFormat_, sliceBlocks, levelSliceBytes, levelW, levelH);
                if (rgba.size() < static_cast<std::size_t>(levelW) * levelH * 4u) return false;
                for (int row = 0; row < h; ++row)
                {
                    std::memcpy(
                        destination +
                            (static_cast<std::size_t>(slice) * h + row) * w * 4u,
                        rgba.data() +
                            (static_cast<std::size_t>(y + row) * levelW + x) * 4u,
                        static_cast<std::size_t>(w) * 4u);
                }
            }
            return true;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t tightSliceBytes = rowBytes * static_cast<std::size_t>(h);
        const std::size_t required = tightSliceBytes * static_cast<std::size_t>(depth);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        const UINT rowPitch = AlignUp(static_cast<UINT>(rowBytes), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
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

        ID3D12GraphicsCommandList* cmdList = renderer_->BeginImmediateCommandsEXT();

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
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* srcRow = mapped
                    + static_cast<std::size_t>(slice) * slicePitch
                    + static_cast<std::size_t>(row) * rowPitch;
                uint8_t* dstRow = out
                    + static_cast<std::size_t>(slice) * tightSliceBytes
                    + static_cast<std::size_t>(row) * rowBytes;
                std::memcpy(dstRow, srcRow, rowBytes);
            }
        }
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }
}
