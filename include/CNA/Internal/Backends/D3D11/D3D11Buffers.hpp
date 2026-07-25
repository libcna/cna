#pragma once

// plan_dx.md Phase DX5 (DX-30/DX-31): real D3D11 vertex/index buffer backends.
//
// Both backends hold their own Microsoft::WRL::ComPtr<ID3D11Device>/ComPtr<ID3D11DeviceContext>
// (copied/AddRef'd from D3D11GraphicsBackend, design decision 10) rather than a raw owner pointer
// with a manual disconnect dance (VulkanVertexBufferBackend's own pattern) -- COM reference
// counting means the device/context stay alive for as long as any buffer references them,
// independent of backend teardown order, which is simpler and just as correct for D3D11.
//
// GPU buffer storage is D3D11_USAGE_DYNAMIC + D3D11_CPU_ACCESS_WRITE, sized lazily from the first
// real SetData() call's (vertex_count/index_count * element size), growing (never shrinking) via
// Map/Unmap if a later SetData() call needs more bytes than the current allocation.

#include "../Common/IGraphicsBackend.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>

namespace CNA::Internal::Backends::D3D11
{
    using Microsoft::WRL::ComPtr;

    /// Real D3D11 vertex buffer backend (DX-30). SetData()/SetDataWithOptions() map the
    /// underlying ID3D11Buffer and copy vertex data in; XNA SetDataOptions::Discard maps to
    /// D3D11_MAP_WRITE_DISCARD, NoOverwrite maps to D3D11_MAP_WRITE_NO_OVERWRITE, and the
    /// options-less SetData()/SetDataOptions::None both use WRITE_DISCARD (always GPU-sync-safe,
    /// matching XNA's own "None may stall" allowance without actually forcing a stall).
    class D3D11VertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        D3D11VertexBufferBackend(ID3D11Device* device, ID3D11DeviceContext* context, int vertex_capacity);

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /// Requested capacity in vertices at construction time (NOXNA diagnostics).
        [[nodiscard]] int GetCapacityEXT() const { return capacity_; }
        /// Byte stride of the most recent SetData() call, 0 before the first call (NOXNA).
        [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }
        /// Raw ID3D11Buffer* for draw-call binding (Phase DX8) (NOXNA).
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
    };

    /// Real D3D11 index buffer backend (DX-31). Supports both 16-bit (DXGI_FORMAT_R16_UINT) and
    /// 32-bit (DXGI_FORMAT_R32_UINT) indices -- the bit width is fixed at construction time
    /// (matches IGraphicsBackend::CreateIndexBuffer16 vs. CreateIndexBuffer32 being separate
    /// factory methods), same Map/Unmap update strategy as the vertex buffer above.
    class D3D11IndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        D3D11IndexBufferBackend(ID3D11Device* device, ID3D11DeviceContext* context,
                                int index_capacity, bool thirtyTwoBit);

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] int GetCapacityEXT() const { return capacity_; }
        [[nodiscard]] ID3D11Buffer* GetBufferEXT() const { return buffer_.Get(); }
        /// DXGI_FORMAT_R16_UINT or DXGI_FORMAT_R32_UINT, for IASetIndexBuffer() (Phase DX8) (NOXNA).
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
