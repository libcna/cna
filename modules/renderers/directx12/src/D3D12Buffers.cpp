// plans/plan_dx.md Phase DX12 (DX-109).
#include "CNA/Internal/Renderers/DirectX12/D3D12Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

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

        /// Creates a DEFAULT-heap (GPU-resident) buffer resource of exactly @p byteWidth bytes,
        /// initial state D3D12_RESOURCE_STATE_COMMON (the documented legal initial state for a
        /// DEFAULT-heap buffer that will immediately be transitioned by whoever uses it -- design
        /// decision 11's own device-lifetime-group discipline doesn't apply here, this is a plain
        /// standalone resource, not part of any of DirectX12Renderer's own lifetime groups).
        ComPtr<ID3D12Resource> CreateDefaultHeapBuffer(ID3D12Device* device, UINT byteWidth, const char* what)
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = byteWidth;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> resource;
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resource.GetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error(std::string(what) + ": CreateCommittedResource (DEFAULT heap) failed, hr=" + FormatHr(hr));
            return resource;
        }

    }

    // -------------------------------------------------------------------------
    // D3D12VertexBufferRenderer
    // -------------------------------------------------------------------------

    D3D12VertexBufferRenderer::D3D12VertexBufferRenderer(DirectX12Renderer* renderer, int vertex_capacity)
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12VertexBufferRenderer"),
          capacity_(vertex_capacity)
    {
        renderer_->RegisterRecoverableResourceEXT(this);
    }

    D3D12VertexBufferRenderer::~D3D12VertexBufferRenderer()
    {
        if (renderer_)
        {
            if (buffer_)
                renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
            renderer_->UnregisterRecoverableResourceEXT(this);
        }
    }

    void D3D12VertexBufferRenderer::EnsureCapacity(std::size_t requiredBytes)
    {
        if (buffer_ && requiredBytes <= byteWidth_) return;

        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * stride_;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        ComPtr<ID3D12Resource> replacement = CreateDefaultHeapBuffer(
            renderer_->GetDeviceEXT(), newByteWidth, "D3D12VertexBufferRenderer");
        if (buffer_)
        {
            renderer_->RetainFrameObjectEXT(buffer_.Get());
            renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
        }
        buffer_ = std::move(replacement);
        byteWidth_ = newByteWidth;
        renderer_->GetResourceStateTrackerEXT().TrackResource(buffer_.Get(), D3D12_RESOURCE_STATE_COMMON);
    }

    void D3D12VertexBufferRenderer::UploadAndCopy(
        const void* data, std::size_t byteCount, SetDataOptions options)
    {
        // D3D12 has no D3D11_MAP flag. A fresh ring allocation implements Discard; the same bump
        // allocator implements NoOverwrite by construction because no in-flight source range is
        // returned again before the frame fence. None is allowed to use the same safe fast path.
        switch (options)
        {
            case SetDataOptions::None:
            case SetDataOptions::Discard:
            case SetDataOptions::NoOverwrite:
                break;
        }
        auto upload = renderer_->AllocateFrameUploadEXT(byteCount, 16);
        std::memcpy(upload.mapped, data, byteCount);

        ID3D12GraphicsCommandList* cmdList = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(buffer_.Get());

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyBufferRegion(buffer_.Get(), 0, upload.resource, upload.offset, byteCount);
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
    }

    void D3D12VertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void D3D12VertexBufferRenderer::SetDataWithOptions(
        const void* data, int vertex_count, std::size_t stride_in_bytes, SetDataOptions options)
    {
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        EnsureCapacity(byteCount);
        UploadAndCopy(data, byteCount, options);
        cpuData_.assign(static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + byteCount);
        vertexCount_ = vertex_count;
    }

    void D3D12VertexBufferRenderer::ReleaseDeviceResourcesEXT() noexcept
    {
        if (renderer_ && buffer_)
            renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
        buffer_.Reset();
        byteWidth_ = 0;
    }

    void D3D12VertexBufferRenderer::RecreateDeviceResourcesEXT()
    {
        if (cpuData_.empty())
            return;
        EnsureCapacity(cpuData_.size());
        UploadAndCopy(cpuData_.data(), cpuData_.size(), SetDataOptions::Discard);
    }

    D3D12_VERTEX_BUFFER_VIEW D3D12VertexBufferRenderer::GetViewEXT() const
    {
        D3D12_VERTEX_BUFFER_VIEW view{};
        if (buffer_)
        {
            view.BufferLocation = buffer_->GetGPUVirtualAddress();
            view.SizeInBytes = byteWidth_;
            view.StrideInBytes = static_cast<UINT>(stride_);
        }
        return view;
    }

    // -------------------------------------------------------------------------
    // D3D12IndexBufferRenderer
    // -------------------------------------------------------------------------

    D3D12IndexBufferRenderer::D3D12IndexBufferRenderer(
        DirectX12Renderer* renderer, int index_capacity, bool thirtyTwoBit)
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12IndexBufferRenderer"),
          capacity_(index_capacity), thirtyTwoBit_(thirtyTwoBit)
    {
        renderer_->RegisterRecoverableResourceEXT(this);
    }

    D3D12IndexBufferRenderer::~D3D12IndexBufferRenderer()
    {
        if (renderer_)
        {
            if (buffer_)
                renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
            renderer_->UnregisterRecoverableResourceEXT(this);
        }
    }

    DXGI_FORMAT D3D12IndexBufferRenderer::GetFormatEXT() const
    {
        return thirtyTwoBit_ ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    }

    void D3D12IndexBufferRenderer::EnsureCapacity(std::size_t requiredBytes)
    {
        if (buffer_ && requiredBytes <= byteWidth_) return;

        const std::size_t elementSize = thirtyTwoBit_ ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * elementSize;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        ComPtr<ID3D12Resource> replacement = CreateDefaultHeapBuffer(
            renderer_->GetDeviceEXT(), newByteWidth, "D3D12IndexBufferRenderer");
        if (buffer_)
        {
            renderer_->RetainFrameObjectEXT(buffer_.Get());
            renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
        }
        buffer_ = std::move(replacement);
        byteWidth_ = newByteWidth;
        renderer_->GetResourceStateTrackerEXT().TrackResource(buffer_.Get(), D3D12_RESOURCE_STATE_COMMON);
    }

    void D3D12IndexBufferRenderer::UploadAndCopy(
        const void* data, std::size_t byteCount, bool dataIsThirtyTwoBit, SetDataOptions options)
    {
        if (dataIsThirtyTwoBit != thirtyTwoBit_)
        {
            throw std::runtime_error(
                thirtyTwoBit_
                    ? "D3D12IndexBufferRenderer: SetData16 called on a 32-bit index buffer"
                    : "D3D12IndexBufferRenderer: SetData32 called on a 16-bit index buffer");
        }

        EnsureCapacity(byteCount);
        switch (options)
        {
            case SetDataOptions::None:
            case SetDataOptions::Discard:
            case SetDataOptions::NoOverwrite:
                break;
        }
        auto upload = renderer_->AllocateFrameUploadEXT(byteCount, 16);
        std::memcpy(upload.mapped, data, byteCount);

        ID3D12GraphicsCommandList* cmdList = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(buffer_.Get());

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyBufferRegion(buffer_.Get(), 0, upload.resource, upload.offset, byteCount);
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);

        indexCount_ = static_cast<int>(byteCount / (dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t)));
    }

    void D3D12IndexBufferRenderer::ReleaseDeviceResourcesEXT() noexcept
    {
        if (renderer_ && buffer_)
            renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
        buffer_.Reset();
        byteWidth_ = 0;
    }

    void D3D12IndexBufferRenderer::RecreateDeviceResourcesEXT()
    {
        if (cpuData_.empty())
            return;
        EnsureCapacity(cpuData_.size());
        UploadAndCopy(cpuData_.data(), cpuData_.size(), thirtyTwoBit_, SetDataOptions::Discard);
    }

    void D3D12IndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D12IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        SetData32WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D12IndexBufferRenderer::SetData16WithOptions(
        const void* data, int index_count, SetDataOptions options)
    {
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * sizeof(std::uint16_t);
        UploadAndCopy(data, byteCount, false, options);
        cpuData_.assign(static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void D3D12IndexBufferRenderer::SetData32WithOptions(
        const void* data, int index_count, SetDataOptions options)
    {
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * sizeof(std::uint32_t);
        UploadAndCopy(data, byteCount, true, options);
        cpuData_.assign(static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + byteCount);
    }

    D3D12_INDEX_BUFFER_VIEW D3D12IndexBufferRenderer::GetViewEXT() const
    {
        D3D12_INDEX_BUFFER_VIEW view{};
        if (buffer_)
        {
            view.BufferLocation = buffer_->GetGPUVirtualAddress();
            view.SizeInBytes = byteWidth_;
            view.Format = GetFormatEXT();
        }
        return view;
    }
}
