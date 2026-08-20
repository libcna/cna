// plans/plan_dx.md Phase DX12 (DX-111, closing env_map3d).
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

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

        // Same combined "shader-readable, any stage" resting state D3D12Textures.cpp's own
        // kTextureShaderReadableState already documents and uses -- duplicated locally rather than
        // shared, matching this renderer's own established per-file small-helper-duplication
        // precedent (e.g. VertexCountForPrimitives in DirectX12Renderer.cpp).
        constexpr D3D12_RESOURCE_STATES kTextureShaderReadableState =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    D3D12TextureCubeRenderer::D3D12TextureCubeRenderer(
        DirectX12Renderer* renderer, int size, bool mipMap, int /*surfaceFormat*/)
        : renderer_(renderer), size_(size), mipLevels_(mipMap ? 1 : 1) // mip-chain generation not yet
                                                                      // implemented for this renderer
                                                                      // (matches D3D12TextureRenderer's
                                                                      // own level-0-only default when
                                                                      // no further levels are
                                                                      // explicitly uploaded) -- an
                                                                      // honest scope note, not a claim
                                                                      // of full mipMap support.
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(size_);
        desc.Height = static_cast<UINT>(size_);
        desc.DepthOrArraySize = 6;
        desc.MipLevels = static_cast<UINT16>(mipLevels_);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        HRESULT hr = renderer_->GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture_.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12TextureCubeRenderer: CreateCommittedResource failed, hr=" + FormatHr(hr));

        renderer_->GetResourceStateTrackerEXT().TrackResource(texture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        // upload -- reachable for every mip level above 0, since this renderer's constructor pins
        // mipLevels_ to 1 whatever `mipMap` says.
        if (level < 0 || level >= mipLevels_ || face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (data == nullptr) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        const UINT rowPitch = AlignUp(static_cast<UINT>(w) * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
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
                        src + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4,
                        static_cast<std::size_t>(w) * 4);
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
        srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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

    bool D3D12TextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        // REMED-GFX-130: see D3D12Texture3DRenderer::GetData -- silent returns fabricated a face.
        if (level < 0 || level >= mipLevels_ || face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (data == nullptr || dataLength < w * h * 4) return false;

        const UINT rowPitch = AlignUp(static_cast<UINT>(w) * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
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
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
            uint8_t* dstRow = out + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4;
            std::memcpy(dstRow, srcRow, static_cast<std::size_t>(w) * 4);
        }
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }
}
