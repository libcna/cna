// DX9 Phase D9-4 (D9-40/D9-41).
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"
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
        /// XNA's own docs only say None *may* stall, never that it must.
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

    void D3D9VertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        declaration_.Remember(vertexDeclaration);
        nativeDeclaration_.Reset();
    }

    IDirect3DVertexDeclaration9* D3D9VertexBufferRenderer::GetNativeVertexDeclarationEXT() const
    {
        if (declaration_.IsEmpty()) return nullptr;
        if (nativeDeclaration_) return nativeDeclaration_.Get();

        const auto elements = TranslateVertexElementsD3D9(declaration_.GetElements());
        const HRESULT hr = device_->CreateVertexDeclaration(
            elements.data(), nativeDeclaration_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D9VertexBufferRenderer: CreateVertexDeclaration failed, hr=" + FormatHr(hr));
        return nativeDeclaration_.Get();
    }

    void D3D9VertexBufferRenderer::ReleaseDefaultPoolResourceEXT()
    {
        // A D3DPOOL_MANAGED buffer survives Reset() with its contents; only the dynamic one is
        // a losable default-pool object.
        if (!dynamic_) return;
        buffer_.Reset();
        byteWidth_ = 0;
    }

    void D3D9VertexBufferRenderer::EnsureCapacity(std::size_t requiredBytes, bool dynamic)
    {
        if (buffer_ && dynamic == dynamic_ && requiredBytes <= byteWidth_) return;

        // Never shrinks -- grow to the larger of what's actually requested and the
        // originally-declared vertex capacity at this call's stride, so a buffer created with a
        // generous capacity_ doesn't get needlessly reallocated on every SetData() at the same
        // stride.
        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * stride_;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        ComPtr<IDirect3DVertexBuffer9> newBuffer;
        // A static (XNA VertexBuffer) upload lives in D3DPOOL_MANAGED, which keeps its contents
        // across every Reset() -- the resize at the first Present() included. D3DUSAGE_DYNAMIC
        // requires D3DPOOL_DEFAULT, so a dynamic upload does not survive Reset(); see
        // ReleaseDefaultPoolResourceEXT() and DirectX9Renderer::PerformResetRecovery().
        const HRESULT hr = device_->CreateVertexBuffer(
            newByteWidth, dynamic ? (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY) : D3DUSAGE_WRITEONLY, 0,
            dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
            newBuffer.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D9VertexBufferRenderer: CreateVertexBuffer failed, hr=" + FormatHr(hr));
        }
        buffer_ = newBuffer;
        byteWidth_ = newByteWidth;
        dynamic_ = dynamic;
    }

    void D3D9VertexBufferRenderer::Upload(const void* data, std::size_t byteCount, SetDataOptions options)
    {
        void* locked = nullptr;
        // DISCARD/NOOVERWRITE are only legal on a D3DUSAGE_DYNAMIC buffer.
        const DWORD flags = dynamic_ ? LockFlagsFor(options) : 0;
        const HRESULT hr = buffer_->Lock(0, static_cast<UINT>(byteCount), &locked, flags);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D9VertexBufferRenderer: Lock failed, hr=" + FormatHr(hr));
        }
        std::memcpy(locked, data, byteCount);
        buffer_->Unlock();
    }

    void D3D9VertexBufferRenderer::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        // A buffer that has already been streamed with options stays dynamic; the whole buffer
        // is replaced either way.
        SetDataInternal(data, vertex_count, stride_in_bytes, SetDataOptions::None, dynamic_);
    }

    void D3D9VertexBufferRenderer::SetDataWithOptions(
        const void* data, int vertex_count, std::size_t stride_in_bytes, SetDataOptions options)
    {
        SetDataInternal(data, vertex_count, stride_in_bytes, options, true);
    }

    void D3D9VertexBufferRenderer::SetDataInternal(
        const void* data, int vertex_count, std::size_t stride_in_bytes, SetDataOptions options,
        bool dynamic)
    {
        stride_ = stride_in_bytes;
        const std::size_t byteCount = static_cast<std::size_t>(vertex_count) * stride_in_bytes;
        // A freshly-(re)created buffer (EnsureCapacity() just allocated a new object -- the
        // common case right after ReleaseDefaultPoolResourceEXT()'s device-lost recovery, or the
        // very first SetData() call) has nothing "in flight" to preserve -- D3DLOCK_DISCARD is
        // always correct there regardless of what the caller requested; NOOVERWRITE only makes
        // sense against an existing, already-uploaded-to buffer.
        const bool wasFresh = !buffer_ || dynamic != dynamic_;
        EnsureCapacity(byteCount, dynamic);
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
        if (!dynamic_) return;
        buffer_.Reset();
        byteWidth_ = 0;
    }

    D3DFORMAT D3D9IndexBufferRenderer::GetFormatEXT() const
    {
        return thirtyTwoBit_ ? D3DFMT_INDEX32 : D3DFMT_INDEX16;
    }

    void D3D9IndexBufferRenderer::EnsureCapacity(std::size_t requiredBytes, bool dynamic)
    {
        if (buffer_ && dynamic == dynamic_ && requiredBytes <= byteWidth_) return;

        const std::size_t elementSize = thirtyTwoBit_ ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::size_t capacityBytes = static_cast<std::size_t>(capacity_) * elementSize;
        UINT newByteWidth = static_cast<UINT>(requiredBytes > capacityBytes ? requiredBytes : capacityBytes);
        if (newByteWidth == 0) newByteWidth = static_cast<UINT>(requiredBytes);

        ComPtr<IDirect3DIndexBuffer9> newBuffer;
        // Same pool split as D3D9VertexBufferRenderer::EnsureCapacity().
        const HRESULT hr = device_->CreateIndexBuffer(
            newByteWidth, dynamic ? (D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY) : D3DUSAGE_WRITEONLY,
            GetFormatEXT(), dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
            newBuffer.ReleaseAndGetAddressOf(), nullptr);
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D9IndexBufferRenderer: CreateIndexBuffer failed, hr=" + FormatHr(hr));
        }
        buffer_ = newBuffer;
        byteWidth_ = newByteWidth;
        dynamic_ = dynamic;
    }

    void D3D9IndexBufferRenderer::Upload(
        const void* data, std::size_t byteCount, SetDataOptions options, bool dataIsThirtyTwoBit,
        bool dynamic)
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
        const bool wasFresh = !buffer_ || dynamic != dynamic_;
        EnsureCapacity(byteCount, dynamic);
        void* locked = nullptr;
        const DWORD flags = dynamic_ ? LockFlagsFor(wasFresh ? SetDataOptions::Discard : options) : 0;
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
        Upload(data, static_cast<std::size_t>(index_count) * sizeof(std::uint16_t),
               SetDataOptions::None, false, dynamic_);
    }

    void D3D9IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        Upload(data, static_cast<std::size_t>(index_count) * sizeof(std::uint32_t),
               SetDataOptions::None, true, dynamic_);
    }

    void D3D9IndexBufferRenderer::SetData16WithOptions(const void* data, int index_count, SetDataOptions options)
    {
        Upload(data, static_cast<std::size_t>(index_count) * sizeof(std::uint16_t), options, false, true);
    }

    void D3D9IndexBufferRenderer::SetData32WithOptions(const void* data, int index_count, SetDataOptions options)
    {
        Upload(data, static_cast<std::size_t>(index_count) * sizeof(std::uint32_t), options, true, true);
    }
}
