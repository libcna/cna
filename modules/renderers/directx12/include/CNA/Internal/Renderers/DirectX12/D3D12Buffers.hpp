#pragma once

// plans/plan_dx.md DX-238: D3D12 vertex/index buffers retain GPU-resident DEFAULT resources while
// uploads come from the owning frame slot's persistently mapped ring. Every update receives a fresh
// non-overlapping source range and records CopyBufferRegion into the active frame list, so Discard
// never waits for an old mapping and NoOverwrite appends without touching in-flight upload bytes.
// The slot's fence gates ring reset; no SetData call creates a one-shot staging resource or submits.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <vector>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    /// Real D3D12 vertex buffer renderer (DX-109).
    class D3D12VertexBufferRenderer final : public IVertexBufferRenderer,
                                            public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        D3D12VertexBufferRenderer(DirectX12Renderer* renderer, int vertex_capacity);
        ~D3D12VertexBufferRenderer() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        // DX-221/DX-222: every ordinary and instanced draw translates this declaration into its
        // native input layout.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }
        /// The declaration this buffer carries for native layout translation and instancing checks.
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

        void ReleaseDeviceResourcesEXT() noexcept override;
        void RecreateDeviceResourcesEXT() override;

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void UploadAndCopy(const void* data, std::size_t byteCount, SetDataOptions options);

        D3D12RendererReference renderer_;
        ComPtr<ID3D12Resource> buffer_;
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        UINT byteWidth_ = 0;
        std::vector<std::uint8_t> cpuData_;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    /// Real D3D12 index buffer renderer (DX-109). Supports both 16-bit (DXGI_FORMAT_R16_UINT) and
    /// 32-bit (DXGI_FORMAT_R32_UINT) indices, fixed at construction time -- same convention as
    /// D3D11IndexBufferRenderer.
    class D3D12IndexBufferRenderer final : public IIndexBufferRenderer,
                                           public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        D3D12IndexBufferRenderer(DirectX12Renderer* renderer, int index_capacity, bool thirtyTwoBit);
        ~D3D12IndexBufferRenderer() override;

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

        void ReleaseDeviceResourcesEXT() noexcept override;
        void RecreateDeviceResourcesEXT() override;

    private:
        void EnsureCapacity(std::size_t requiredBytes);
        void UploadAndCopy(const void* data, std::size_t byteCount, bool dataIsThirtyTwoBit,
                           SetDataOptions options);

        D3D12RendererReference renderer_;
        ComPtr<ID3D12Resource> buffer_;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        UINT byteWidth_ = 0;
        std::vector<std::uint8_t> cpuData_;
    };
}
