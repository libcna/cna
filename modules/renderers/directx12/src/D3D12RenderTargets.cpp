// plans/plan_dx.md Phase DX13 (DX-117); DX-144 (mip-chain generation).
#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

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

        /// REMED-GFX-134: D3D12's own placed-footprint row-pitch alignment, same helper
        /// D3D12TextureCube.cpp already keeps for the plain-cube readback.
        UINT AlignUp(UINT value, UINT alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        /// Mirrors D3D11RenderTargetRenderer's own CalculateMipLevels() exactly (D3D11RenderTargets.cpp)
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

        /// MSAA follow-up: real, device-queried MSAA support, mirroring D3D11's own
        /// ClampMultiSampleCount() (D3D11RenderTargets.cpp) exactly -- never assumes a requested
        /// sample count is supported. Returns 0 (no MSAA) if requestedCount <= 1 or the device
        /// reports zero quality levels for it.
        int ClampMultiSampleCount(ID3D12Device* device, DXGI_FORMAT format, int requestedCount)
        {
            if (requestedCount <= 1) return 0;
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS data{};
            data.Format = format;
            data.SampleCount = static_cast<UINT>(requestedCount);
            data.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &data, sizeof(data)))
                || data.NumQualityLevels == 0)
            {
                return 0;
            }
            return requestedCount;
        }

        /// DX-144: reads back subresource `subresource` (a single mip level, array slice 0) of
        /// `resource` as a w*h RGBA8 byte buffer, via a READBACK-heap CopyTextureRegion -- the same
        /// synchronous readback discipline D3D12Buffers.cpp/D3D12Textures.cpp already establish.
        /// Returns an empty vector on any failure (honest bail-out, not a silently wrong result).
        std::vector<uint8_t> ReadbackSubresourceRGBA8(
            DirectX12Renderer* owner, ID3D12Device* device, ID3D12Resource* resource,
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
        /// an UPLOAD-heap CopyTextureRegion -- mirrors D3D12TextureRenderer::UploadRegion() exactly
        /// (D3D12Textures.cpp), just against an arbitrary render-target resource instead of a
        /// texture. Silently returns on failure (caller treats a still-missing mip as an honest gap,
        /// not a crash).
        void UploadSubresourceRGBA8(
            DirectX12Renderer* owner, ID3D12Device* device, ID3D12Resource* resource,
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
    // D3D12RenderTargetRenderer
    // -------------------------------------------------------------------------

    D3D12RenderTargetRenderer::D3D12RenderTargetRenderer(
        DirectX12Renderer* owner, ID3D12Device* device, int w, int h, int depthFormat, bool mipMap,
        int multiSampleCount)
        : owner_(owner), device_(device), width_(w), height_(h)
        , appliedMultiSampleCount_(ClampMultiSampleCount(device, DXGI_FORMAT_R8G8B8A8_UNORM, multiSampleCount))
    {
        isMsaa_ = appliedMultiSampleCount_ > 0;
        // Mutually exclusive on the same attachment, same rationale D3D11RenderTargetRenderer's own
        // DX-45 already established -- a full mip chain needs a single-sample source.
        mipMap_ = mipMap && !isMsaa_;
        levelCount_ = mipMap_ ? CalculateMipLevels(w, h) : 1;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC colorDesc{};
        colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        colorDesc.Width = static_cast<UINT64>(w);
        colorDesc.Height = static_cast<UINT>(h);
        colorDesc.DepthOrArraySize = 1;
        colorDesc.MipLevels = static_cast<UINT16>(levelCount_);
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = isMsaa_ ? static_cast<UINT>(appliedMultiSampleCount_) : 1;
        colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear, IID_PPV_ARGS(colorResource_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12RenderTargetRenderer: CreateCommittedResource(color) failed, hr=" + FormatHr(hr));

        owner_->GetResourceStateTrackerEXT().TrackResource(colorResource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        heaps_ = owner_->GetDescriptorHeapsEXT();
        rtv_ = owner_->AllocateRtvDescriptorEXT();
        // Explicit MipSlice=0 (rather than a null desc) -- required once levelCount_ > 1, since an
        // RTV can only ever target exactly one mip level and a null desc's inference is not
        // guaranteed for a multi-mip resource. TEXTURE2DMS has no MipSlice field at all (MSAA
        // resources never have mips, enforced above by the mutual-exclusion rule).
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (isMsaa_)
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        }
        else
        {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
        }
        device_->CreateRenderTargetView(colorResource_.Get(), &rtvDesc, rtv_);

        if (isMsaa_)
        {
            // The MSAA resource itself is never sampled directly -- ResolveSubresource() into this
            // separate single-sample resource on UnbindAsRenderTarget() (mirrors
            // D3D11RenderTargetRenderer's own resolveTexture_/DX-45 design exactly). Created in
            // COMMON, a legal generic initial state for CreateCommittedResource, and tracked from
            // there -- ResolveMsaaEXT() transitions it to RESOLVE_DEST before the first resolve.
            D3D12_RESOURCE_DESC resolveDesc = colorDesc;
            resolveDesc.SampleDesc.Count = 1;
            resolveDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resolveDesc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resolveResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetRenderer: CreateCommittedResource(resolve) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(resolveResource_.Get(), D3D12_RESOURCE_STATE_COMMON);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // always single-sample -- see GetSampleableColorResourceEXT()
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(levelCount_);
        srvIndex_ = owner_->CreateCbvSrvUavDescriptorEXT(
            [&](D3D12_CPU_DESCRIPTOR_HANDLE cpu)
            {
                device_->CreateShaderResourceView(isMsaa_ ? resolveResource_.Get() : colorResource_.Get(),
                                                  &srvDesc, cpu);
            });

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
            depthDesc.SampleDesc.Count = colorDesc.SampleDesc.Count; // MSAA depth matches MSAA color
            depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format = depthDxgiFormat;
            depthClear.DepthStencil.Depth = 1.0f;

            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetRenderer: CreateCommittedResource(depth) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            dsv_ = owner_->AllocateDsvDescriptorEXT();
            dsvFormat_ = depthDxgiFormat; // DX-146: needed by BindAsRenderTarget()
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = depthDxgiFormat;
            dsvDesc.ViewDimension = isMsaa_ ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsv_);
        }
    }

    D3D12RenderTargetRenderer::~D3D12RenderTargetRenderer()
    {
        if (!heaps_) return;
        heaps_->cbvSrvUav.Free(srvIndex_);
        heaps_->rtv.Free(rtv_);
        if (hasDepth_) heaps_->dsv.Free(dsv_);
    }

    void D3D12RenderTargetRenderer::BindAsRenderTarget()
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

    void D3D12RenderTargetRenderer::UnbindAsRenderTarget()
    {
        ResolveMsaaEXT();
        GenerateMipsEXT();
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    void D3D12RenderTargetRenderer::ResolveMsaaEXT()
    {
        if (!isMsaa_ || !resolveResource_ || !owner_) return;

        // The "any shader stage can read this" resting state every other real texture/resolved
        // resource in this renderer settles into once its content is ready (D3D12Textures.cpp's own
        // kTextureShaderReadableState) -- ResolveSubresource() needs the two resources in
        // RESOLVE_SOURCE/RESOLVE_DEST specifically, D3D12's own explicit-transition requirement
        // D3D11's identical ResolveSubresource() call never needed.
        constexpr D3D12_RESOURCE_STATES kShaderReadableState =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

        ID3D12CommandAllocator* allocator = owner_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = owner_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = owner_->GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorColorState = tracker.GetTrackedStateEXT(colorResource_.Get());
        tracker.TransitionTo(cmdList, colorResource_.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        tracker.TransitionTo(cmdList, resolveResource_.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cmdList->ResolveSubresource(resolveResource_.Get(), 0, colorResource_.Get(), 0,
                                    DXGI_FORMAT_R8G8B8A8_UNORM);
        // Color goes back to whatever it was (RENDER_TARGET -- the only state BindAsRenderTarget()
        // ever leaves it in); the resolve target settles into the real shader-readable resting
        // state so it's immediately valid to sample/read back without a further transition. Its
        // own pre-resolve state (COMMON at first, RESOLVE_DEST here) is irrelevant once resolved.
        tracker.TransitionTo(cmdList, colorResource_.Get(), priorColorState);
        tracker.TransitionTo(cmdList, resolveResource_.Get(), kShaderReadableState);

        if (FAILED(cmdList->Close())) return;
        owner_->ExecuteCommandListAndWaitEXT(cmdList);
    }

    bool D3D12RenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level >= levelCount_)
            throw System::NotSupportedException(
                "D3D12RenderTargetRenderer::GetData: this render target has " +
                std::to_string(levelCount_) + " mip level(s); level " + std::to_string(level) +
                " was requested.");

        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        // 64-bit throughout, so a rectangle near INT_MAX is rejected rather than wrapping.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(levelW) || bottom > static_cast<std::int64_t>(levelH))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(levelW) + "x" +
                    std::to_string(levelH) + " mip level.");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");

        ID3D12Resource* const source = GetSampleableColorResourceEXT();
        if (!owner_ || !device_ || !source || data == nullptr)
            return false;

        const std::vector<uint8_t> levelPixels = ReadbackSubresourceRGBA8(
            owner_, device_.Get(), source, static_cast<UINT>(level), levelW, levelH);
        // ReadbackSubresourceRGBA8's own honest bail-out: empty means the copy never completed.
        if (levelPixels.size() < static_cast<std::size_t>(levelW) * levelH * 4)
            return false;

        auto* dst = static_cast<std::uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                        levelPixels.data() +
                            (static_cast<std::size_t>(y + row) * levelW + x) * 4u,
                        rowBytes);
        return true;
    }

    void D3D12RenderTargetRenderer::GenerateMipsEXT()
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
    // D3D12RenderTargetCubeRenderer
    // -------------------------------------------------------------------------

    D3D12RenderTargetCubeRenderer::D3D12RenderTargetCubeRenderer(
        DirectX12Renderer* owner, ID3D12Device* device, int size, int depthFormat, bool mipMap,
        int multiSampleCount)
        : owner_(owner), device_(device), size_(size)
        , appliedMultiSampleCount_(ClampMultiSampleCount(device, DXGI_FORMAT_R8G8B8A8_UNORM, multiSampleCount))
    {
        isMsaa_ = appliedMultiSampleCount_ > 0;
        // Mutually exclusive on the same attachment, same rationale D3D12RenderTargetRenderer's own
        // DX-117 MSAA follow-up already established for the 2D leg.
        mipMap_ = mipMap && !isMsaa_;
        levelCount_ = mipMap_ ? CalculateMipLevels(size, size) : 1;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC colorDesc{};
        colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        colorDesc.Width = static_cast<UINT64>(size_);
        colorDesc.Height = static_cast<UINT>(size_);
        colorDesc.DepthOrArraySize = 6;
        colorDesc.MipLevels = static_cast<UINT16>(levelCount_);
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = isMsaa_ ? static_cast<UINT>(appliedMultiSampleCount_) : 1;
        colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear, IID_PPV_ARGS(colorResource_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12RenderTargetCubeRenderer: CreateCommittedResource(color) failed, hr=" + FormatHr(hr));

        owner_->GetResourceStateTrackerEXT().TrackResource(colorResource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        for (UINT face = 0; face < 6; ++face)
        {
            if (!heaps_) heaps_ = owner_->GetDescriptorHeapsEXT();
            rtv_[face] = owner_->AllocateRtvDescriptorEXT();
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (isMsaa_)
            {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
                rtvDesc.Texture2DMSArray.FirstArraySlice = face;
                rtvDesc.Texture2DMSArray.ArraySize = 1;
            }
            else
            {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.MipSlice = 0;
                rtvDesc.Texture2DArray.FirstArraySlice = face;
                rtvDesc.Texture2DArray.ArraySize = 1;
            }
            device_->CreateRenderTargetView(colorResource_.Get(), &rtvDesc, rtv_[face]);
        }

        if (isMsaa_)
        {
            // D3D12_SRV_DIMENSION_TEXTURECUBE has no multisampled variant -- the MSAA color
            // resource above is RTV-only, never sampled directly. This separate single-sample
            // resource is what the real TextureCube SRV below targets, ResolveSubresource()'d from
            // the active face only on UnbindAsRenderTarget() (mirrors the 2D leg's own
            // resolveResource_/ResolveMsaaEXT() design, DX-117 follow-up).
            D3D12_RESOURCE_DESC resolveDesc = colorDesc;
            resolveDesc.SampleDesc.Count = 1;
            resolveDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resolveDesc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resolveResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetCubeRenderer: CreateCommittedResource(resolve) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(resolveResource_.Get(), D3D12_RESOURCE_STATE_COMMON);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(levelCount_);
        srvIndex_ = owner_->CreateCbvSrvUavDescriptorEXT(
            [&](D3D12_CPU_DESCRIPTOR_HANDLE cpu)
            {
                device_->CreateShaderResourceView(isMsaa_ ? resolveResource_.Get() : colorResource_.Get(),
                                                  &srvDesc, cpu);
            });

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
            depthDesc.SampleDesc.Count = colorDesc.SampleDesc.Count; // MSAA depth matches MSAA color
            depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format = depthDxgiFormat;
            depthClear.DepthStencil.Depth = 1.0f;

            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetCubeRenderer: CreateCommittedResource(depth) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            dsv_ = owner_->AllocateDsvDescriptorEXT();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = depthDxgiFormat;
            dsvDesc.ViewDimension = isMsaa_ ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsv_);
        }
    }

    D3D12RenderTargetCubeRenderer::~D3D12RenderTargetCubeRenderer()
    {
        if (!heaps_) return;
        heaps_->cbvSrvUav.Free(srvIndex_);
        for (D3D12_CPU_DESCRIPTOR_HANDLE face : rtv_) heaps_->rtv.Free(face);
        if (hasDepth_) heaps_->dsv.Free(dsv_);
    }

    void D3D12RenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        activeFace_ = face;
        if (owner_)
        {
            owner_->BindOffscreenColorTargetEXT(colorResource_.Get(), rtv_[face],
                                                DXGI_FORMAT_R8G8B8A8_UNORM, size_, size_);
        }
    }

    void D3D12RenderTargetCubeRenderer::ResolveMsaaEXT()
    {
        if (!isMsaa_ || !resolveResource_ || !owner_ || activeFace_ < 0) return;

        constexpr D3D12_RESOURCE_STATES kShaderReadableState =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

        ID3D12CommandAllocator* allocator = owner_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = owner_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        // Only the currently-active face -- matches GenerateMipsEXT()'s own existing "only one
        // face is ever the active draw target at a time" convention. The MSAA source has no mips
        // (subresource = face); the resolve destination's base mip level uses the standard
        // mip + face*levelCount_ formula (levelCount_ is always 1 here since mipMap_ is forced
        // false when isMsaa_).
        const UINT srcSubresource = static_cast<UINT>(activeFace_);
        const UINT dstSubresource = static_cast<UINT>(activeFace_) * static_cast<UINT>(levelCount_);

        auto& tracker = owner_->GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorColorState = tracker.GetTrackedStateEXT(colorResource_.Get());
        tracker.TransitionTo(cmdList, colorResource_.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        tracker.TransitionTo(cmdList, resolveResource_.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cmdList->ResolveSubresource(resolveResource_.Get(), dstSubresource, colorResource_.Get(), srcSubresource,
                                    DXGI_FORMAT_R8G8B8A8_UNORM);
        tracker.TransitionTo(cmdList, colorResource_.Get(), priorColorState);
        tracker.TransitionTo(cmdList, resolveResource_.Get(), kShaderReadableState);

        if (FAILED(cmdList->Close())) return;
        owner_->ExecuteCommandListAndWaitEXT(cmdList);
    }

    bool D3D12RenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
        // REMED-GFX-134: closes the refusal this class inherited from ITextureCubeRenderer's
        // `return false` default. Same readback-heap mechanism as D3D12TextureCubeRenderer::GetData.
        if (!owner_ || data == nullptr) return false;
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        // Always the resolved single-sample resource: a multisampled one cannot be the source of a
        // CopyTextureRegion, and UnbindAsRenderTarget has already resolved into this one per face.
        ID3D12Resource* source = GetSampleableColorResourceEXT();
        if (source == nullptr) return false;

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
        HRESULT hr = device_->CreateCommittedResource(
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

        // DX-144's documented convention: face `f`'s mip `m` is subresource m + f * levelCount_.
        const UINT subresource =
            static_cast<UINT>(level) + static_cast<UINT>(face) * static_cast<UINT>(levelCount_);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = source;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = subresource;

        D3D12_BOX srcBox{};
        srcBox.left = static_cast<UINT>(x);
        srcBox.top = static_cast<UINT>(y);
        srcBox.front = 0;
        srcBox.right = static_cast<UINT>(x + w);
        srcBox.bottom = static_cast<UINT>(y + h);
        srcBox.back = 1;

        ID3D12CommandAllocator* allocator = owner_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = owner_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = owner_->GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(source);
        tracker.TransitionTo(cmdList, source, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);
        tracker.TransitionTo(cmdList, source, priorState); // restore -- this is a read-only readback

        hr = cmdList->Close();
        if (FAILED(hr)) return false;
        owner_->ExecuteCommandListAndWaitEXT(cmdList);

        std::uint8_t* mapped = nullptr;
        const D3D12_RANGE mapRange{0, static_cast<SIZE_T>(readbackBufferSize)};
        if (FAILED(readback->Map(0, &mapRange, reinterpret_cast<void**>(&mapped)))) return false;

        auto* out = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const std::uint8_t* srcRow = mapped + static_cast<std::size_t>(row) * rowPitch;
            std::uint8_t* dstRow = out + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4;
            std::memcpy(dstRow, srcRow, static_cast<std::size_t>(w) * 4);
        }
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return true;
    }

    void D3D12RenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        // DX-152/DX-144: resolve MSAA, then generate the active face's mip chain (mutually
        // exclusive in practice -- isMsaa_ forces mipMap_ false -- but ordered the same way the
        // 2D leg orders ResolveMsaaEXT()/GenerateMipsEXT()), BEFORE clearing activeFace_.
        ResolveMsaaEXT();
        GenerateMipsEXT();
        activeFace_ = -1;
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    void D3D12RenderTargetCubeRenderer::GenerateMipsEXT()
    {
        if (!mipMap_ || levelCount_ <= 1 || !owner_ || activeFace_ < 0) return;

        // Only the face that was actually just drawn to gets its chain regenerated -- mirrors
        // D3D11RenderTargetCubeRenderer's own single-active-face convention (one shared
        // depth/color resource, only one face is ever the current draw target at a time).
        const UINT face = static_cast<UINT>(activeFace_);
        int srcW = size_, srcH = size_;
        for (int level = 1; level < levelCount_; ++level)
        {
            const int dstW = std::max(1, srcW / 2);
            const int dstH = std::max(1, srcH / 2);

            // Standard D3D12 texture-array/mip subresource-index formula: mip + arraySlice*mipLevels.
            const UINT srcSubresource = static_cast<UINT>(level - 1) + face * static_cast<UINT>(levelCount_);
            const UINT dstSubresource = static_cast<UINT>(level) + face * static_cast<UINT>(levelCount_);

            const auto srcPixels = ReadbackSubresourceRGBA8(
                owner_, device_.Get(), colorResource_.Get(), srcSubresource, srcW, srcH);
            if (srcPixels.empty()) return; // honest bail-out -- leaves remaining levels undefined, not wrong

            const auto dstPixels = BoxFilterDownsample(srcPixels, srcW, srcH, dstW, dstH);
            UploadSubresourceRGBA8(
                owner_, device_.Get(), colorResource_.Get(), dstSubresource,
                dstPixels.data(), dstW, dstH);

            srcW = dstW; srcH = dstH;
        }
    }
}
