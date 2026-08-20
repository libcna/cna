// plans/plan_dx9.md Phase D9-4 (D9-40/D9-41).
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX9
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        /// XNA SetDataOptions -> D3DLOCK flags: Discard = D3DLOCK_DISCARD (driver may return a
        /// fresh, unsynchronized region); NoOverwrite = D3DLOCK_NOOVERWRITE (caller promises not to
        /// touch any in-flight range); None = also DISCARD, since it is always GPU-sync-safe and
        /// XNA's own docs only say None *may* stall, never that it must -- same choice
        /// D3D11VertexBufferRenderer already made for its own MapTypeFor().
        DWORD LockFlagsFor(SetDataOptions options)
        {
            return options == SetDataOptions::NoOverwrite ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD;
        }
    }

    // -------------------------------------------------------------------------
    // D3D9VertexBufferRenderer
    // -------------------------------------------------------------------------

    D3D9VertexBufferRenderer::D3D9VertexBufferRenderer(
        DirectX9Renderer& owner, IDirect3DDevice9* device, int vertex_capacity)
        : owner_(owner), device_(device), capacity_(vertex_capacity)
    {
        owner_.RegisterDefaultPoolResourceEXT(this);
    }

    D3D9VertexBufferRenderer::~D3D9VertexBufferRenderer()
    {
        owner_.UnregisterDefaultPoolResourceEXT(this);
    }

    void D3D9VertexBufferRenderer::ReleaseDefaultPoolResourceEXT()
    {
        buffer_.Reset();
        byteWidth_ = 0;
    }

    void D3D9VertexBufferRenderer::EnsureCapacity(std::size_t requiredBytes)
    {
        if (buffer_ && requiredBytes <= byteWidth_) return;

        // Never shrinks -- grow to the larger of what's actually requested and the
        // originally-declared vertex capacity at this call's stride, so a buffer created with a
        // generous capacity_ doesn't get needlessly reallocated on every SetData() at the same
        // stride (same discipline D3D11VertexBufferRenderer::EnsureCapacity() already established).
        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * stride_;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        ComPtr<IDirect3DVertexBuffer9> newBuffer;
        // design decision 2: D3DUSAGE_DYNAMIC requires D3DPOOL_DEFAULT (D3D9 forbids DYNAMIC with
        // POOL_MANAGED) -- this buffer does NOT survive Reset() automatically; see
        // ReleaseDefaultPoolResourceEXT() and DirectX9Renderer::PerformResetRecovery().
        const HRESULT hr = device_->CreateVertexBuffer(
            newByteWidth, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
            newBuffer.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D9VertexBufferRenderer: CreateVertexBuffer failed, hr=" + FormatHr(hr));
        }
        buffer_ = newBuffer;
        byteWidth_ = newByteWidth;
    }

    void D3D9VertexBufferRenderer::Upload(const void* data, std::size_t byteCount, SetDataOptions options)
    {
        void* locked = nullptr;
        const HRESULT hr = buffer_->Lock(0, static_cast<UINT>(byteCount), &locked, LockFlagsFor(options));
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D9VertexBufferRenderer: Lock failed, hr=" + FormatHr(hr));
        }
        std::memcpy(locked, data, byteCount);
        buffer_->Unlock();
    }

    void D3D9VertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void D3D9VertexBufferRenderer::SetDataWithOptions(
        const void* data, int vertex_count, std::size_t stride_in_bytes, SetDataOptions options)
    {
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        // A freshly-(re)created buffer (EnsureCapacity() just allocated a new object -- the
        // common case right after ReleaseDefaultPoolResourceEXT()'s device-lost recovery, or the
        // very first SetData() call) has nothing "in flight" to preserve -- D3DLOCK_DISCARD is
        // always correct there regardless of what the caller requested; NOOVERWRITE only makes
        // sense against an existing, already-uploaded-to buffer.
        const bool wasFresh = !buffer_;
        EnsureCapacity(byteCount);
        Upload(data, byteCount, wasFresh ? SetDataOptions::Discard : options);
        vertexCount_ = vertex_count;
    }

    // -------------------------------------------------------------------------
    // D3D9IndexBufferRenderer
    // -------------------------------------------------------------------------

    D3D9IndexBufferRenderer::D3D9IndexBufferRenderer(
        DirectX9Renderer& owner, IDirect3DDevice9* device, int index_capacity, bool thirtyTwoBit)
        : owner_(owner), device_(device), capacity_(index_capacity), thirtyTwoBit_(thirtyTwoBit)
    {
        owner_.RegisterDefaultPoolResourceEXT(this);
    }

    D3D9IndexBufferRenderer::~D3D9IndexBufferRenderer()
    {
        owner_.UnregisterDefaultPoolResourceEXT(this);
    }

    void D3D9IndexBufferRenderer::ReleaseDefaultPoolResourceEXT()
    {
        buffer_.Reset();
        byteWidth_ = 0;
    }

    D3DFORMAT D3D9IndexBufferRenderer::GetFormatEXT() const
    {
        return thirtyTwoBit_ ? D3DFMT_INDEX32 : D3DFMT_INDEX16;
    }

    void D3D9IndexBufferRenderer::EnsureCapacity(std::size_t requiredBytes)
    {
        if (buffer_ && requiredBytes <= byteWidth_) return;

        const std::size_t elementSize = thirtyTwoBit_ ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * elementSize;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        ComPtr<IDirect3DIndexBuffer9> newBuffer;
        const HRESULT hr = device_->CreateIndexBuffer(
            newByteWidth, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, GetFormatEXT(), D3DPOOL_DEFAULT,
            newBuffer.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D9IndexBufferRenderer: CreateIndexBuffer failed, hr=" + FormatHr(hr));
        }
        buffer_ = newBuffer;
        byteWidth_ = newByteWidth;
    }

    void D3D9IndexBufferRenderer::Upload(
        const void* data, std::size_t byteCount, SetDataOptions options, bool dataIsThirtyTwoBit)
    {
        if (dataIsThirtyTwoBit != thirtyTwoBit_)
        {
            throw std::runtime_error(
                thirtyTwoBit_
                    ? "D3D9IndexBufferRenderer: SetData16 called on a 32-bit index buffer"
                    : "D3D9IndexBufferRenderer: SetData32 called on a 16-bit index buffer");
        }

        // See D3D9VertexBufferRenderer::SetDataWithOptions()'s own comment: a freshly-(re)created
        // buffer always uses DISCARD, regardless of what the caller requested.
        const bool wasFresh = !buffer_;
        EnsureCapacity(byteCount);
        void* locked = nullptr;
        const DWORD flags = LockFlagsFor(wasFresh ? SetDataOptions::Discard : options);
        const HRESULT hr = buffer_->Lock(0, static_cast<UINT>(byteCount), &locked, flags);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D9IndexBufferRenderer: Lock failed, hr=" + FormatHr(hr));
        }
        std::memcpy(locked, data, byteCount);
        buffer_->Unlock();
        indexCount_ = static_cast<int>(byteCount / (dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t)));
    }

    void D3D9IndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D9IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        SetData32WithOptions(data, index_count, SetDataOptions::None);
    }

    void D3D9IndexBufferRenderer::SetData16WithOptions(const void* data, int index_count, SetDataOptions options)
    {
        Upload(data, static_cast<std::size_t>(index_count) * sizeof(std::uint16_t), options, false);
    }

    void D3D9IndexBufferRenderer::SetData32WithOptions(const void* data, int index_count, SetDataOptions options)
    {
        Upload(data, static_cast<std::size_t>(index_count) * sizeof(std::uint32_t), options, true);
    }
}
