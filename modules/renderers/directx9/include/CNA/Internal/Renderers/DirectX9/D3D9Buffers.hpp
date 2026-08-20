#pragma once

// plans/plan_dx9.md Phase D9-4 (D9-40/D9-41): real D3D9 vertex/index buffer renderers.
//
// design decision 2: dynamic (mutable, SetData()-driven) buffers live in D3DPOOL_DEFAULT (D3D9
// forbids D3DUSAGE_DYNAMIC with D3DPOOL_MANAGED) -- unlike D3DPOOL_MANAGED user resources, these do
// NOT survive a device Reset() automatically; they register with the owning DirectX9Renderer
// (D3D9DefaultPoolResourceEXT) and release themselves before Reset(), recreating lazily on next
// use -- real, authentic D3D9/XNA behavior (a DYNAMIC VertexBuffer's content does not survive
// DeviceReset in real XNA either).
//
// Sized lazily from the first real SetData() call (IGraphicsRenderer::CreateVertexBuffer() takes no
// stride parameter), growing (never shrinking) via a fresh Lock/Unlock if a later SetData() call
// needs more bytes than the current allocation -- same discipline D3D11Buffers.hpp already
// established for this project.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "D3D9DefaultPoolResourceEXT.hpp"

#include <d3d9.h>
#include <wrl/client.h>

#include <cstddef>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    class DirectX9Renderer;

    /// Real D3D9 vertex buffer renderer (D9-40). SetData()/SetDataWithOptions() Lock() the
    /// underlying IDirect3DVertexBuffer9 and copy vertex data in; XNA SetDataOptions::Discard maps
    /// to D3DLOCK_DISCARD, NoOverwrite maps to D3DLOCK_NOOVERWRITE, and the options-less
    /// SetData()/SetDataOptions::None both use D3DLOCK_DISCARD (always GPU-sync-safe, matching
    /// XNA's own "None may stall" allowance without actually forcing a stall -- same choice
    /// D3D11VertexBufferRenderer already made).
    class D3D9VertexBufferRenderer final : public IVertexBufferRenderer, public ID3D9DefaultPoolResourceEXT
    {
    public:
        D3D9VertexBufferRenderer(DirectX9Renderer& owner, IDirect3DDevice9* device, int vertex_capacity);
        ~D3D9VertexBufferRenderer() override;

        D3D9VertexBufferRenderer(const D3D9VertexBufferRenderer&) = delete;
        D3D9VertexBufferRenderer& operator=(const D3D9VertexBufferRenderer&) = delete;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        // REMED-GFX-DECL-GUARD: this renderer still selects its IDirect3DVertexDeclaration9 from
        // the shared D3DCommon stride table (REMED-GFX-217), but the declaration is remembered
        // rather than discarded so a draw can refuse one that table would silently reinterpret.
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

        void ReleaseDefaultPoolResourceEXT() override;

        /// Requested capacity in vertices at construction time (CNAEXT diagnostics).
        [[nodiscard]] int GetCapacityEXT() const { return capacity_; }
        /// Byte stride of the most recent SetData() call, 0 before the first call (CNAEXT).
        [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }
        /// Raw IDirect3DVertexBuffer9* for draw-call binding (Phase D9-8) (CNAEXT).
        [[nodiscard]] IDirect3DVertexBuffer9* GetBufferEXT() const { return buffer_.Get(); }

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void Upload(const void* data, std::size_t byteCount, SetDataOptions options);

        DirectX9Renderer& owner_;
        ComPtr<IDirect3DDevice9> device_;
        ComPtr<IDirect3DVertexBuffer9> buffer_;
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
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            d3dVb.GetDeclarationEXT(), static_cast<int>(d3dVb.GetStrideEXT()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "DIRECTX9", route);
    }

    /// Real D3D9 index buffer renderer (D9-41). Supports both 16-bit (D3DFMT_INDEX16) and 32-bit
    /// (D3DFMT_INDEX32) indices -- the bit width is fixed at construction time (matches
    /// IGraphicsRenderer::CreateIndexBuffer16 vs. CreateIndexBuffer32 being separate factory
    /// methods), same Lock/Unlock update strategy as the vertex buffer above. D3DCAPS9::MaxVertexIndex
    /// gates real 32-bit support (D9-3's own spike confirmed 0xFFFFFF/16777215 on this machine, i.e.
    /// genuinely 32-bit-capable) -- feeds Phase D9-10's Reach/HiDef floor, not re-checked here.
    class D3D9IndexBufferRenderer final : public IIndexBufferRenderer, public ID3D9DefaultPoolResourceEXT
    {
    public:
        D3D9IndexBufferRenderer(DirectX9Renderer& owner, IDirect3DDevice9* device,
                               int index_capacity, bool thirtyTwoBit);
        ~D3D9IndexBufferRenderer() override;

        D3D9IndexBufferRenderer(const D3D9IndexBufferRenderer&) = delete;
        D3D9IndexBufferRenderer& operator=(const D3D9IndexBufferRenderer&) = delete;

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        void ReleaseDefaultPoolResourceEXT() override;

        [[nodiscard]] int GetCapacityEXT() const { return capacity_; }
        [[nodiscard]] IDirect3DIndexBuffer9* GetBufferEXT() const { return buffer_.Get(); }
        /// D3DFMT_INDEX16 or D3DFMT_INDEX32, for SetIndices() (Phase D9-8) (CNAEXT).
        [[nodiscard]] D3DFORMAT GetFormatEXT() const;

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void Upload(const void* data, std::size_t byteCount, SetDataOptions options, bool dataIsThirtyTwoBit);

        DirectX9Renderer& owner_;
        ComPtr<IDirect3DDevice9> device_;
        ComPtr<IDirect3DIndexBuffer9> buffer_;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        UINT byteWidth_ = 0;
    };
}
