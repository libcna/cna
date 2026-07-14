// plan_dx.md Phase DX13 (DX-117); DX-144 (mip-chain generation).
#include "CNA/Internal/Backends/D3D12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DFormatMapping.hpp"

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

        /// Mirrors D3D11RenderTargetBackend's own CalculateMipLevels() exactly (D3D11RenderTargets.cpp)
        /// -- full mip chain down to 1x1.
        int CalculateMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1)
            {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                ++levels;
            }
            return levels;
        }

        /// DX-144: reads back subresource `subresource` (a single mip level, array slice 0) of
        /// `resource` as a w*h RGBA8 byte buffer, via a READBACK-heap CopyTextureRegion -- the same
        /// synchronous readback discipline D3D12Buffers.cpp/D3D12Textures.cpp already establish.
        /// Returns an empty vector on any failure (honest bail-out, not a silently wrong result).
        std::vector<uint8_t> ReadbackSubresourceRGBA8(
            D3D12GraphicsBackend* owner, ID3D12Device* device, ID3D12Resource* resource,
            UINT subresource, int w, int h)
        {
            const D3D12_RESOURCE_DESC desc = resource->GetDesc();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
            UINT numRows = 0; UINT64 rowBytes = 0, totalBytes = 0;
            device->GetCopyableFootprints(&desc, subresource, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);
            if (totalBytes == 0) return {};

            D3D12_HEAP_PROPERTIES rbHeap{};
            rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bufDesc{};
            bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufDesc.Width = totalBytes;
            bufDesc.Height = 1;
            bufDesc.DepthOrArraySize = 1;
            bufDesc.MipLevels = 1;
            bufDesc.Format = DXGI_FORMAT_UNKNOWN;
            bufDesc.SampleDesc.Count = 1;
            bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> rb;
            if (FAILED(device->CreateCommittedResource(&rbHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                       IID_PPV_ARGS(rb.GetAddressOf()))))
                return {};

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = rb.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = fp;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = resource;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = subresource;

            ID3D12CommandAllocator* allocator = owner->GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = owner->GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);
            auto& tracker = owner->GetResourceStateTrackerEXT();
            const D3D12_RESOURCE_STATES prior = tracker.GetTrackedStateEXT(resource);
            tracker.TransitionTo(cmdList, resource, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            tracker.TransitionTo(cmdList, resource, prior);
            if (FAILED(cmdList->Close())) return {};
            owner->ExecuteCommandListAndWaitEXT(cmdList);

            uint8_t* mapped = nullptr;
            const D3D12_RANGE rr{0, static_cast<SIZE_T>(totalBytes)};
            if (FAILED(rb->Map(0, &rr, reinterpret_cast<void**>(&mapped)))) return {};
            std::vector<uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
            for (int row = 0; row < h; ++row)
                std::memcpy(out.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4,
                            mapped + fp.Offset + static_cast<std::size_t>(row) * fp.Footprint.RowPitch,
                            static_cast<std::size_t>(w) * 4);
            const D3D12_RANGE wr{0, 0};
            rb->Unmap(0, &wr);
            return out;
        }

        /// DX-144: uploads a w*h RGBA8 byte buffer into subresource `subresource` of `resource`, via
        /// an UPLOAD-heap CopyTextureRegion -- mirrors D3D12TextureBackend::UploadRegion() exactly
        /// (D3D12Textures.cpp), just against an arbitrary render-target resource instead of a
        /// texture. Silently returns on failure (caller treats a still-missing mip as an honest gap,
        /// not a crash).
        void UploadSubresourceRGBA8(
            D3D12GraphicsBackend* owner, ID3D12Device* device, ID3D12Resource* resource,
            UINT subresource, const uint8_t* rgba, int w, int h)
        {
            const UINT rowPitch = (static_cast<UINT>(w) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                                 & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
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
            HRESULT hr = device->CreateCommittedResource(
                &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.GetAddressOf()));
            if (FAILED(hr)) return;

            uint8_t* mapped = nullptr;
            const D3D12_RANGE readRange{0, 0};
            hr = staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
            if (FAILED(hr)) return;
            for (int row = 0; row < h; ++row)
                std::memcpy(mapped + static_cast<std::size_t>(row) * rowPitch,
                            rgba + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4,
                            static_cast<std::size_t>(w) * 4);
            staging->Unmap(0, nullptr);

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = resource;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = subresource;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = staging.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = 0;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            src.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
            src.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = rowPitch;

            ID3D12CommandAllocator* allocator = owner->GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = owner->GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);

            auto& tracker = owner->GetResourceStateTrackerEXT();
            const D3D12_RESOURCE_STATES prior = tracker.GetTrackedStateEXT(resource);
            tracker.TransitionTo(cmdList, resource, D3D12_RESOURCE_STATE_COPY_DEST);
            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            tracker.TransitionTo(cmdList, resource, prior);

            hr = cmdList->Close();
            if (FAILED(hr)) return;
            owner->ExecuteCommandListAndWaitEXT(cmdList);
        }

        /// DX-144: a simple 2x2 box filter, clamping the second sample to the last row/column for
        /// odd source dimensions. For a SOLID-color source (this row's own established test
        /// methodology, see D3D11's DX-144 closure note), any weighting reproduces the exact same
        /// solid color regardless of the exact edge-clamp behavior -- this sidesteps needing to
        /// replicate D3D11's own GenerateMips() box-filter kernel bit-for-bit.
        std::vector<uint8_t> BoxFilterDownsample(const std::vector<uint8_t>& src, int srcW, int srcH, int dstW, int dstH)
        {
            std::vector<uint8_t> dst(static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH) * 4);
            for (int y = 0; y < dstH; ++y)
            {
                const int sy0 = std::min(srcH - 1, y * 2);
                const int sy1 = std::min(srcH - 1, y * 2 + 1);
                for (int x = 0; x < dstW; ++x)
                {
                    const int sx0 = std::min(srcW - 1, x * 2);
                    const int sx1 = std::min(srcW - 1, x * 2 + 1);
                    for (int c = 0; c < 4; ++c)
                    {
                        const int sum = src[(static_cast<std::size_t>(sy0) * srcW + sx0) * 4 + c]
                                      + src[(static_cast<std::size_t>(sy0) * srcW + sx1) * 4 + c]
                                      + src[(static_cast<std::size_t>(sy1) * srcW + sx0) * 4 + c]
                                      + src[(static_cast<std::size_t>(sy1) * srcW + sx1) * 4 + c];
                        dst[(static_cast<std::size_t>(y) * dstW + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
                    }
                }
            }
            return dst;
        }
    }

    // -------------------------------------------------------------------------
    // D3D12RenderTargetBackend
    // -------------------------------------------------------------------------

    D3D12RenderTargetBackend::D3D12RenderTargetBackend(
        D3D12GraphicsBackend* owner, ID3D12Device* device, int w, int h, int depthFormat, bool mipMap)
        : owner_(owner), device_(device), width_(w), height_(h)
        , mipMap_(mipMap), levelCount_(mipMap ? CalculateMipLevels(w, h) : 1)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC colorDesc{};
        colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        colorDesc.Width = static_cast<UINT64>(w);
        colorDesc.Height = static_cast<UINT>(h);
        colorDesc.DepthOrArraySize = 1;
        colorDesc.MipLevels = static_cast<UINT16>(levelCount_);
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = 1;
        colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear, IID_PPV_ARGS(colorResource_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12RenderTargetBackend: CreateCommittedResource(color) failed, hr=" + FormatHr(hr));

        owner_->GetResourceStateTrackerEXT().TrackResource(colorResource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        rtv_ = owner_->AllocateRtvDescriptorEXT();
        // Explicit MipSlice=0 (rather than a null desc) -- required once levelCount_ > 1, since an
        // RTV can only ever target exactly one mip level and a null desc's inference is not
        // guaranteed for a multi-mip resource.
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        device_->CreateRenderTargetView(colorResource_.Get(), &rtvDesc, rtv_);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(levelCount_);
        owner_->AllocateCbvSrvUavDescriptorEXT(srvCpu_, srvGpu_);
        device_->CreateShaderResourceView(colorResource_.Get(), &srvDesc, srvCpu_);

        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(depthFormat);
        hasDepth_ = depthDxgiFormat != DXGI_FORMAT_UNKNOWN;
        if (hasDepth_)
        {
            D3D12_RESOURCE_DESC depthDesc{};
            depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            depthDesc.Width = static_cast<UINT64>(w);
            depthDesc.Height = static_cast<UINT>(h);
            depthDesc.DepthOrArraySize = 1;
            depthDesc.MipLevels = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format = depthDxgiFormat;
            depthClear.DepthStencil.Depth = 1.0f;

            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetBackend: CreateCommittedResource(depth) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            dsv_ = owner_->AllocateDsvDescriptorEXT();
            dsvFormat_ = depthDxgiFormat; // DX-146: needed by BindAsRenderTarget()
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = depthDxgiFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsv_);
        }
    }

    void D3D12RenderTargetBackend::BindAsRenderTarget()
    {
        if (owner_)
        {
            // DX-146: pass this target's own DSV too. DX-117 created the depth resource+DSV but
            // never bound them, so a render target with a real depth buffer silently gave every draw
            // NO depth buffer (depth test and every ClearDepth*/ClearStencil* variant were inert
            // against it). Found by DX-146's own depth/stencil pixel proofs.
            owner_->BindOffscreenColorTargetEXT(colorResource_.Get(), rtv_,
                                                DXGI_FORMAT_R8G8B8A8_UNORM, width_, height_,
                                                dsv_, dsvFormat_);
        }
    }

    void D3D12RenderTargetBackend::UnbindAsRenderTarget()
    {
        GenerateMipsEXT();
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    void D3D12RenderTargetBackend::GenerateMipsEXT()
    {
        if (!mipMap_ || levelCount_ <= 1 || !owner_) return;

        int srcW = width_, srcH = height_;
        for (int level = 1; level < levelCount_; ++level)
        {
            const int dstW = std::max(1, srcW / 2);
            const int dstH = std::max(1, srcH / 2);

            const auto srcPixels = ReadbackSubresourceRGBA8(
                owner_, device_.Get(), colorResource_.Get(), static_cast<UINT>(level - 1), srcW, srcH);
            if (srcPixels.empty()) return; // honest bail-out -- leaves remaining levels undefined, not wrong

            const auto dstPixels = BoxFilterDownsample(srcPixels, srcW, srcH, dstW, dstH);
            UploadSubresourceRGBA8(
                owner_, device_.Get(), colorResource_.Get(), static_cast<UINT>(level),
                dstPixels.data(), dstW, dstH);

            srcW = dstW; srcH = dstH;
        }
    }

    // -------------------------------------------------------------------------
    // D3D12RenderTargetCubeBackend
    // -------------------------------------------------------------------------

    D3D12RenderTargetCubeBackend::D3D12RenderTargetCubeBackend(
        D3D12GraphicsBackend* owner, ID3D12Device* device, int size, int depthFormat)
        : owner_(owner), device_(device), size_(size)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC colorDesc{};
        colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        colorDesc.Width = static_cast<UINT64>(size_);
        colorDesc.Height = static_cast<UINT>(size_);
        colorDesc.DepthOrArraySize = 6;
        colorDesc.MipLevels = 1;
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = 1;
        colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear, IID_PPV_ARGS(colorResource_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12RenderTargetCubeBackend: CreateCommittedResource(color) failed, hr=" + FormatHr(hr));

        owner_->GetResourceStateTrackerEXT().TrackResource(colorResource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        for (UINT face = 0; face < 6; ++face)
        {
            rtv_[face] = owner_->AllocateRtvDescriptorEXT();
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.FirstArraySlice = face;
            rtvDesc.Texture2DArray.ArraySize = 1;
            device_->CreateRenderTargetView(colorResource_.Get(), &rtvDesc, rtv_[face]);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        owner_->AllocateCbvSrvUavDescriptorEXT(srvCpu_, srvGpu_);
        device_->CreateShaderResourceView(colorResource_.Get(), &srvDesc, srvCpu_);

        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(depthFormat);
        hasDepth_ = depthDxgiFormat != DXGI_FORMAT_UNKNOWN;
        if (hasDepth_)
        {
            D3D12_RESOURCE_DESC depthDesc{};
            depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            depthDesc.Width = static_cast<UINT64>(size_);
            depthDesc.Height = static_cast<UINT>(size_);
            depthDesc.DepthOrArraySize = 1;
            depthDesc.MipLevels = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format = depthDxgiFormat;
            depthClear.DepthStencil.Depth = 1.0f;

            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetCubeBackend: CreateCommittedResource(depth) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            dsv_ = owner_->AllocateDsvDescriptorEXT();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = depthDxgiFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsv_);
        }
    }

    void D3D12RenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        activeFace_ = face;
        if (owner_)
        {
            owner_->BindOffscreenColorTargetEXT(colorResource_.Get(), rtv_[face],
                                                DXGI_FORMAT_R8G8B8A8_UNORM, size_, size_);
        }
    }

    void D3D12RenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        activeFace_ = -1;
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }
}
