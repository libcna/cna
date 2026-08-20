#pragma once

// plans/plan_dx.md Phase DX12 (DX-109): real D3D12 vertex/index buffer renderers via explicit
// upload-heap staging + CopyBufferRegion -- D3D12 has no implicit driver-managed Map/Unmap-onto-a-
// GPU-resident-resource path the way D3D11 does (D3D11Buffers.hpp/.cpp's own D3D11_USAGE_DYNAMIC +
// Map/Unmap convention). Each SetData()/SetDataWithOptions() call: (1) ensures a DEFAULT-heap
// GPU-resident ID3D12Resource is at least byteCount bytes (grows, never shrinks, mirroring
// D3D11VertexBufferRenderer's own capacity policy); (2) creates a fresh UPLOAD-heap staging resource
// sized exactly to byteCount, Map()s it (upload heaps are always CPU-writable, unlike DEFAULT-heap
// resources) and memcpy's the caller's data in; (3) records CopyBufferRegion on the renderer's
// shared command list, with D3D12ResourceStateTracker (DX-106) driving the
// GENERIC_READ/COMMON -> COPY_DEST -> GENERIC_READ transition around it; (4) executes + waits
// synchronously via the renderer's own ExecuteCommandListAndWaitEXT() (DX-102/DX-105).
//
// No persistent per-frame staging ring yet -- nothing above this layer (Clear()/Present()/draws) is
// real yet either (that's DX-111 onward), so there is no per-frame throughput requirement to design
// against today; a synchronous immediate-submit upload is the correct, honest scope for this task.
//
// SetDataOptions (Discard/NoOverwrite/None) has no real distinguishing effect at this synchronous-
// upload stage: every SetData() call already fully serializes with the GPU via
// ExecuteCommandListAndWaitEXT() before returning, so there is no in-flight GPU access for
// Discard/NoOverwrite to avoid racing with. The parameter is accepted (interface compatibility) but
// intentionally not distinguished -- a documented, honest simplification, not silently dropped
// semantics; revisit once real per-frame command-list pipelining exists.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    /// Real D3D12 vertex buffer renderer (DX-109).
    class D3D12VertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        D3D12VertexBufferRenderer(DirectX12Renderer* renderer, int vertex_capacity);

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        // REMED-GFX-DECL-GUARD: this renderer still selects its D3D12_INPUT_LAYOUT_DESC from the
        // shared D3DCommon stride table (REMED-GFX-217), but the declaration is remembered rather
        // than discarded so a draw can refuse one that table would silently reinterpret.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }
        /// The declaration this buffer carries, for REMED-GFX-DECL-GUARD's fidelity check.
        [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& GetDeclarationEXT() const
        {
            return declaration_;
        }

        /// Requested capacity in vertices at construction time (CNAEXT diagnostics).
        [[nodiscard]] int GetCapacityEXT() const { return capacity_; }
        /// Byte stride of the most recent SetData() call, 0 before the first call (CNAEXT).
        [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }
        /// Raw GPU-resident ID3D12Resource* (CNAEXT -- Phase DX-111's draw path / readback tests).
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const { return buffer_.Get(); }
        /// D3D12_VERTEX_BUFFER_VIEW for IASetVertexBuffers() (CNAEXT -- Phase DX-111).
        [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW GetViewEXT() const;

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void UploadAndCopy(const void* data, std::size_t byteCount);

        DirectX12Renderer* renderer_ = nullptr;
        ComPtr<ID3D12Resource> buffer_;
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        UINT byteWidth_ = 0;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    /// REMED-GFX-DECL-GUARD: throws `System::NotSupportedException` unless @p vb's declaration can
    /// be represented faithfully by the shared D3DCommon stride table's entry for its stride.
    /// Pure: creates nothing, queues nothing and leaves no partial native object behind, so a
    /// rejected draw cannot poison the device. An out-of-table stride is left to the table's own
    /// established rejection.
    inline void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& d3dVb = static_cast<const D3D12VertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            d3dVb.GetDeclarationEXT(), static_cast<int>(d3dVb.GetStrideEXT()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "DIRECTX12", route);
    }

    /// Real D3D12 index buffer renderer (DX-109). Supports both 16-bit (DXGI_FORMAT_R16_UINT) and
    /// 32-bit (DXGI_FORMAT_R32_UINT) indices, fixed at construction time -- same convention as
    /// D3D11IndexBufferRenderer.
    class D3D12IndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        D3D12IndexBufferRenderer(DirectX12Renderer* renderer, int index_capacity, bool thirtyTwoBit);

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] int GetCapacityEXT() const { return capacity_; }
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const { return buffer_.Get(); }
        /// DXGI_FORMAT_R16_UINT or DXGI_FORMAT_R32_UINT, for reference/tests (CNAEXT).
        [[nodiscard]] DXGI_FORMAT GetFormatEXT() const;
        /// D3D12_INDEX_BUFFER_VIEW for IASetIndexBuffer() (CNAEXT -- Phase DX-111).
        [[nodiscard]] D3D12_INDEX_BUFFER_VIEW GetViewEXT() const;

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void UploadAndCopy(const void* data, std::size_t byteCount, bool dataIsThirtyTwoBit);

        DirectX12Renderer* renderer_ = nullptr;
        ComPtr<ID3D12Resource> buffer_;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        UINT byteWidth_ = 0;
    };
}
