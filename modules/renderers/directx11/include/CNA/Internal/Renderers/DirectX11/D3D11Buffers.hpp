#pragma once

// plans/plan_dx.md Phase DIRECTX5 (DX-30/DX-31): real D3D11 vertex/index buffer renderers.
//
// Both renderers hold their own Microsoft::WRL::ComPtr<ID3D11Device>/ComPtr<ID3D11DeviceContext>
// (copied/AddRef'd from DirectX11Renderer, design decision 10) rather than a raw owner pointer
// with a manual disconnect dance (VulkanVertexBufferRenderer's own pattern) -- COM reference
// counting means the device/context stay alive for as long as any buffer references them,
// independent of renderer teardown order, which is simpler and just as correct for D3D11.
//
// GPU buffer storage is D3D11_USAGE_DYNAMIC + D3D11_CPU_ACCESS_WRITE, sized lazily from the first
// real SetData() call's (vertex_count/index_count * element size), growing (never shrinking) via
// Map/Unmap if a later SetData() call needs more bytes than the current allocation.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    /// Real D3D11 vertex buffer renderer (DX-30). SetData()/SetDataWithOptions() map the
    /// underlying ID3D11Buffer and copy vertex data in; XNA SetDataOptions::Discard maps to
    /// D3D11_MAP_WRITE_DISCARD, NoOverwrite maps to D3D11_MAP_WRITE_NO_OVERWRITE, and the
    /// options-less SetData()/SetDataOptions::None both use WRITE_DISCARD (always GPU-sync-safe,
    /// matching XNA's own "None may stall" allowance without actually forcing a stall).
    class D3D11VertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        D3D11VertexBufferRenderer(ID3D11Device* device, ID3D11DeviceContext* context, int vertex_capacity);

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        // REMED-GFX-DECL-GUARD: this renderer still selects its ID3D11InputLayout from the shared
        // D3DCommon stride table (REMED-GFX-217), but the declaration is remembered rather than
        // discarded so a draw can refuse one that table would silently reinterpret.
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
        /// Raw ID3D11Buffer* for draw-call binding (Phase DIRECTX8) (CNAEXT).
        [[nodiscard]] ID3D11Buffer* GetBufferEXT() const { return buffer_.Get(); }

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void Upload(const void* data, std::size_t byteCount, SetDataOptions options);

        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11Buffer> buffer_;
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
        const auto& d3dVb = static_cast<const D3D11VertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            d3dVb.GetDeclarationEXT(), static_cast<int>(d3dVb.GetStrideEXT()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "DIRECTX11", route);
    }

    /// Real D3D11 index buffer renderer (DX-31). Supports both 16-bit (DXGI_FORMAT_R16_UINT) and
    /// 32-bit (DXGI_FORMAT_R32_UINT) indices -- the bit width is fixed at construction time
    /// (matches IGraphicsRenderer::CreateIndexBuffer16 vs. CreateIndexBuffer32 being separate
    /// factory methods), same Map/Unmap update strategy as the vertex buffer above.
    class D3D11IndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        D3D11IndexBufferRenderer(ID3D11Device* device, ID3D11DeviceContext* context,
                                int index_capacity, bool thirtyTwoBit);

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] int GetCapacityEXT() const { return capacity_; }
        [[nodiscard]] ID3D11Buffer* GetBufferEXT() const { return buffer_.Get(); }
        /// DXGI_FORMAT_R16_UINT or DXGI_FORMAT_R32_UINT, for IASetIndexBuffer() (Phase DIRECTX8) (CNAEXT).
        [[nodiscard]] DXGI_FORMAT GetFormatEXT() const;

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void Upload(const void* data, std::size_t byteCount, SetDataOptions options, bool dataIsThirtyTwoBit);

        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11Buffer> buffer_;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        UINT byteWidth_ = 0;
    };
}
