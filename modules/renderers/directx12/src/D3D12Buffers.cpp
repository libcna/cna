// plans/plan_dx.md Phase DX12 (DX-109).
#include "CNA/Internal/Renderers/DirectX12/D3D12Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

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

        /// Creates an UPLOAD-heap (CPU-writable) staging buffer of exactly @p byteCount bytes,
        /// Map()s it, and memcpy's @p data in. Returns the still-mapped-and-unmapped resource --
        /// callers record a CopyBufferRegion off it, then let the ComPtr go out of scope once the
        /// copy has been submitted+waited-on (synchronous upload, see this file's own header
        /// comment for why that's the correct scope for this task).
        ComPtr<ID3D12Resource> CreateAndFillUploadBuffer(ID3D12Device* device, const void* data,
                                                         std::size_t byteCount, const char* what)
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = static_cast<UINT64>(byteCount);
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> staging;
            HRESULT hr = device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(staging.GetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error(std::string(what) + ": CreateCommittedResource (UPLOAD heap) failed, hr=" + FormatHr(hr));

            void* mapped = nullptr;
            const D3D12_RANGE readRange{0, 0}; // we never read this resource back on the CPU
            hr = staging->Map(0, &readRange, &mapped);
            if (FAILED(hr))
                throw std::runtime_error(std::string(what) + ": staging Map failed, hr=" + FormatHr(hr));
            std::memcpy(mapped, data, byteCount);
            staging->Unmap(0, nullptr);
            return staging;
        }
    }

    // -------------------------------------------------------------------------
    // D3D12VertexBufferRenderer
    // -------------------------------------------------------------------------

    D3D12VertexBufferRenderer::D3D12VertexBufferRenderer(DirectX12Renderer* renderer, int vertex_capacity)
        : renderer_(renderer), capacity_(vertex_capacity)
    {
    }

    void D3D12VertexBufferRenderer::EnsureCapacity(std::size_t requiredBytes)
    {
        if (buffer_ && requiredBytes <= byteWidth_) return;

        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * stride_;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        buffer_ = CreateDefaultHeapBuffer(renderer_->GetDeviceEXT(), newByteWidth, "D3D12VertexBufferRenderer");
        byteWidth_ = newByteWidth;
        // Fresh (or freshly reallocated) resource -- (re)register it at its real, documented
        // initial state; TrackResource() overwriting a prior entry for an old, now-released
        // resource pointer is a non-issue since that pointer's ComPtr just dropped its last
        // reference above.
        renderer_->GetResourceStateTrackerEXT().TrackResource(buffer_.Get(), D3D12_RESOURCE_STATE_COMMON);
    }

    void D3D12VertexBufferRenderer::UploadAndCopy(const void* data, std::size_t byteCount)
    {
        ComPtr<ID3D12Resource> staging =
            CreateAndFillUploadBuffer(renderer_->GetDeviceEXT(), data, byteCount, "D3D12VertexBufferRenderer");

        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyBufferRegion(buffer_.Get(), 0, staging.Get(), 0, byteCount);
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12VertexBufferRenderer: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList); // synchronous -- staging is safe to release after this
    }

    void D3D12VertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void D3D12VertexBufferRenderer::SetDataWithOptions(
        const void* data, int vertex_count, std::size_t stride_in_bytes, SetDataOptions /*options*/)
    {
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        EnsureCapacity(byteCount);
        UploadAndCopy(data, byteCount);
        vertexCount_ = vertex_count;
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
        : renderer_(renderer), capacity_(index_capacity), thirtyTwoBit_(thirtyTwoBit)
    {
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

        buffer_ = CreateDefaultHeapBuffer(renderer_->GetDeviceEXT(), newByteWidth, "D3D12IndexBufferRenderer");
        byteWidth_ = newByteWidth;
        renderer_->GetResourceStateTrackerEXT().TrackResource(buffer_.Get(), D3D12_RESOURCE_STATE_COMMON);
    }

    void D3D12IndexBufferRenderer::UploadAndCopy(const void* data, std::size_t byteCount, bool dataIsThirtyTwoBit)
    {
        if (dataIsThirtyTwoBit != thirtyTwoBit_)
        {
            throw std::runtime_error(
                thirtyTwoBit_
                    ? "D3D12IndexBufferRenderer: SetData16 called on a 32-bit index buffer"
                    : "D3D12IndexBufferRenderer: SetData32 called on a 16-bit index buffer");
        }

        EnsureCapacity(byteCount);
        ComPtr<ID3D12Resource> staging =
            CreateAndFillUploadBuffer(renderer_->GetDeviceEXT(), data, byteCount, "D3D12IndexBufferRenderer");

        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer_->GetResourceStateTrackerEXT();
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyBufferRegion(buffer_.Get(), 0, staging.Get(), 0, byteCount);
        tracker.TransitionTo(cmdList, buffer_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12IndexBufferRenderer: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList);

        indexCount_ = static_cast<int>(byteCount / (dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t)));
    }

    void D3D12IndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D12IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        SetData32WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D12IndexBufferRenderer::SetData16WithOptions(const void* data, int index_count, SetDataOptions /*options*/)
    {
        UploadAndCopy(data, static_cast<std::size_t>(index_count) * sizeof(std::uint16_t), false);
    }

    void D3D12IndexBufferRenderer::SetData32WithOptions(const void* data, int index_count, SetDataOptions /*options*/)
    {
        UploadAndCopy(data, static_cast<std::size_t>(index_count) * sizeof(std::uint32_t), true);
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
