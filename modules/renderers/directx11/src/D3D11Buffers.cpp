// plans/plan_dx.md Phase DIRECTX5 (DX-30/DX-31).
#include "CNA/Internal/Renderers/DirectX11/D3D11Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX11
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        /// XNA SetDataOptions -> D3D11_MAP: Discard = D3D11_MAP_WRITE_DISCARD (driver may return a
        /// fresh, unsynchronized region); NoOverwrite = D3D11_MAP_WRITE_NO_OVERWRITE (caller
        /// promises not to touch any in-flight range); None = also WRITE_DISCARD, since it is
        /// always GPU-sync-safe and XNA's own docs only say None *may* stall, never that it must --
        /// this renderer simply never stalls.
        D3D11_MAP MapTypeFor(SetDataOptions options)
        {
            return options == SetDataOptions::NoOverwrite
                ? D3D11_MAP_WRITE_NO_OVERWRITE
                : D3D11_MAP_WRITE_DISCARD;
        }
    }

    // -------------------------------------------------------------------------
    // D3D11VertexBufferRenderer
    // -------------------------------------------------------------------------

    D3D11VertexBufferRenderer::D3D11VertexBufferRenderer(
        DirectX11Renderer* owner, int vertex_capacity)
        : owner_(owner)
        , ownerLifetime_(owner ? owner->GetLifetimeTokenEXT() : std::weak_ptr<void>{})
        , device_(owner ? owner->GetDeviceEXT() : nullptr)
        , context_(owner ? owner->GetContextEXT() : nullptr)
        , capacity_(vertex_capacity)
    {
        if (owner_)
            owner_->RegisterRecoverableResourceEXT(this);
    }

    D3D11VertexBufferRenderer::~D3D11VertexBufferRenderer()
    {
        if (owner_ && !ownerLifetime_.expired())
            owner_->UnregisterRecoverableResourceEXT(this);
    }

    void D3D11VertexBufferRenderer::EnsureCapacity(std::size_t requiredBytes)
    {
        if (buffer_ && requiredBytes <= byteWidth_) return;

        // Never shrinks -- grow to the larger of what's actually requested and the
        // originally-declared vertex capacity at this call's stride, so a buffer created with a
        // generous capacity_ doesn't get needlessly reallocated on every SetData() at the same
        // stride.
        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * stride_;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = newByteWidth;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ComPtr<ID3D11Buffer> newBuffer;
        const HRESULT hr = device_->CreateBuffer(&desc, nullptr, newBuffer.GetAddressOf());
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D11VertexBufferRenderer: CreateBuffer failed, hr=" + FormatHr(hr));
        }
        buffer_ = newBuffer;
        byteWidth_ = newByteWidth;
    }

    void D3D11VertexBufferRenderer::Upload(const void* data, std::size_t byteCount, SetDataOptions options)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(buffer_.Get(), 0, MapTypeFor(options), 0, &mapped);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D11VertexBufferRenderer: Map failed, hr=" + FormatHr(hr));
        }
        std::memcpy(mapped.pData, data, byteCount);
        context_->Unmap(buffer_.Get(), 0);
    }

    void D3D11VertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void D3D11VertexBufferRenderer::SetDataWithOptions(
        const void* data, int vertex_count, std::size_t stride_in_bytes, SetDataOptions options)
    {
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        EnsureCapacity(byteCount);
        Upload(data, byteCount, options);
        cpuData_.assign(static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + byteCount);
        vertexCount_ = vertex_count;
    }

    void D3D11VertexBufferRenderer::ReleaseDeviceResourcesEXT() noexcept
    {
        buffer_.Reset();
        context_.Reset();
        device_.Reset();
        byteWidth_ = 0;
    }

    void D3D11VertexBufferRenderer::RecreateDeviceResourcesEXT()
    {
        device_ = owner_->GetDeviceEXT();
        context_ = owner_->GetContextEXT();
        if (cpuData_.empty())
            return;
        EnsureCapacity(cpuData_.size());
        Upload(cpuData_.data(), cpuData_.size(), SetDataOptions::None);
    }

    // -------------------------------------------------------------------------
    // D3D11IndexBufferRenderer
    // -------------------------------------------------------------------------

    D3D11IndexBufferRenderer::D3D11IndexBufferRenderer(
        DirectX11Renderer* owner, int index_capacity, bool thirtyTwoBit)
        : owner_(owner)
        , ownerLifetime_(owner ? owner->GetLifetimeTokenEXT() : std::weak_ptr<void>{})
        , device_(owner ? owner->GetDeviceEXT() : nullptr)
        , context_(owner ? owner->GetContextEXT() : nullptr)
        , capacity_(index_capacity)
        , thirtyTwoBit_(thirtyTwoBit)
    {
        if (owner_)
            owner_->RegisterRecoverableResourceEXT(this);
    }

    D3D11IndexBufferRenderer::~D3D11IndexBufferRenderer()
    {
        if (owner_ && !ownerLifetime_.expired())
            owner_->UnregisterRecoverableResourceEXT(this);
    }

    DXGI_FORMAT D3D11IndexBufferRenderer::GetFormatEXT() const
    {
        return thirtyTwoBit_ ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    }

    void D3D11IndexBufferRenderer::EnsureCapacity(std::size_t requiredBytes)
    {
        if (buffer_ && requiredBytes <= byteWidth_) return;

        const std::size_t elementSize = thirtyTwoBit_ ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * elementSize;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = newByteWidth;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ComPtr<ID3D11Buffer> newBuffer;
        const HRESULT hr = device_->CreateBuffer(&desc, nullptr, newBuffer.GetAddressOf());
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D11IndexBufferRenderer: CreateBuffer failed, hr=" + FormatHr(hr));
        }
        buffer_ = newBuffer;
        byteWidth_ = newByteWidth;
    }

    void D3D11IndexBufferRenderer::Upload(
        const void* data, std::size_t byteCount, SetDataOptions options, bool dataIsThirtyTwoBit)
    {
        if (dataIsThirtyTwoBit != thirtyTwoBit_)
        {
            throw std::runtime_error(
                thirtyTwoBit_
                    ? "D3D11IndexBufferRenderer: SetData16 called on a 32-bit index buffer"
                    : "D3D11IndexBufferRenderer: SetData32 called on a 16-bit index buffer");
        }

        EnsureCapacity(byteCount);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(buffer_.Get(), 0, MapTypeFor(options), 0, &mapped);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D11IndexBufferRenderer: Map failed, hr=" + FormatHr(hr));
        }
        std::memcpy(mapped.pData, data, byteCount);
        context_->Unmap(buffer_.Get(), 0);
        indexCount_ = static_cast<int>(byteCount / (dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t)));
    }

    void D3D11IndexBufferRenderer::ReleaseDeviceResourcesEXT() noexcept
    {
        buffer_.Reset();
        context_.Reset();
        device_.Reset();
        byteWidth_ = 0;
    }

    void D3D11IndexBufferRenderer::RecreateDeviceResourcesEXT()
    {
        device_ = owner_->GetDeviceEXT();
        context_ = owner_->GetContextEXT();
        if (cpuData_.empty())
            return;
        EnsureCapacity(cpuData_.size());
        Upload(cpuData_.data(), cpuData_.size(), SetDataOptions::None, thirtyTwoBit_);
    }

    void D3D11IndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D11IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        SetData32WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D11IndexBufferRenderer::SetData16WithOptions(const void* data, int index_count, SetDataOptions options)
    {
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * sizeof(std::uint16_t);
        Upload(data, byteCount, options, false);
        cpuData_.assign(static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + byteCount);
    }

    void D3D11IndexBufferRenderer::SetData32WithOptions(const void* data, int index_count, SetDataOptions options)
    {
        const std::size_t byteCount = static_cast<std::size_t>(index_count) * sizeof(std::uint32_t);
        Upload(data, byteCount, options, true);
        cpuData_.assign(static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + byteCount);
    }
}
