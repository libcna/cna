// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::DirectX12
{
    /** @brief Native GPU buffer for the portable transfer roles. */
    class D3D12StorageBuffer final : public IStorageBufferRenderer,
                                     public D3DCommon::ID3DDeviceRecoverableEXT
    {
    public:
        /**
         * @brief Allocates a device-local buffer for declared transfer roles.
         * @param renderer Owning D3D12 renderer.
         * @param byteSize Logical buffer size.
         * @param usage Portable usage bitmask.
         * @param cpuAccess Portable direct CPU-access bitmask.
         */
        D3D12StorageBuffer(DirectX12Renderer* renderer, std::size_t byteSize,
                           std::uint32_t usage, std::uint32_t cpuAccess);
        /** @brief Releases the resource and unregisters it from device recovery. */
        ~D3D12StorageBuffer() override;

        /**
         * @brief Uploads a prefix through the frame upload ring.
         * @param data Source bytes.
         * @param byteSize Byte count.
         */
        void SetData(const void* data, std::size_t byteSize) override;
        /**
         * @brief Reads a prefix through a synchronous GPU readback.
         * @param out Destination bytes.
         * @param byteSize Byte count.
         */
        void GetData(void* out, std::size_t byteSize) const override;
        /**
         * @brief Uploads an exact range without waiting for an active GPU frame.
         * @param byteOffset Destination offset.
         * @param data Source bytes.
         * @param byteSize Byte count.
         * @return True when the entire range was accepted.
         */
        bool SetDataRangeEXT(std::size_t byteOffset, const void* data,
                             std::size_t byteSize) override;
        /**
         * @brief Reads an exact range, synchronizing only at this explicit CPU readback.
         * @param byteOffset Source offset.
         * @param out Destination bytes.
         * @param byteSize Byte count.
         * @return True when the entire range was read.
         */
        bool GetDataRangeEXT(std::size_t byteOffset, void* out,
                             std::size_t byteSize) const override;
        /**
         * @brief Records a device-local copy to another buffer.
         * @param destination Same-device destination buffer.
         * @param sourceByteOffset Source offset.
         * @param destinationByteOffset Destination offset.
         * @param byteSize Byte count.
         * @return True when the complete copy was queued.
         */
        bool CopyToEXT(IStorageBufferRenderer& destination,
                       std::size_t sourceByteOffset,
                       std::size_t destinationByteOffset,
                       std::size_t byteSize) override;
        /** @brief Returns the logical byte count. */
        [[nodiscard]] std::size_t GetByteSize() const override { return byteSize_; }
        /** @brief Returns the declared usage mask. */
        [[nodiscard]] std::uint32_t GetUsageEXT() const override { return usage_; }
        /** @brief Returns the declared CPU-access mask. */
        [[nodiscard]] std::uint32_t GetCpuAccessEXT() const override { return cpuAccess_; }
        /** @brief Returns the native resource for subsequent compute and indirect bindings. */
        [[nodiscard]] ID3D12Resource* GetResourceEXT() const noexcept { return buffer_.Get(); }
        /** @brief Returns the native device this buffer belongs to. */
        [[nodiscard]] ID3D12Device* GetDeviceEXT() const noexcept { return device_.Get(); }

        /** @brief Drops the old device allocation during device recreation or disposal. */
        void ReleaseDeviceResourcesEXT() noexcept override;
        /** @brief Reallocates the buffer and restores its last known CPU-written contents. */
        void RecreateDeviceResourcesEXT() override;

    private:
        void CreateResource();
        void UploadRange(std::size_t byteOffset, const void* data, std::size_t byteSize);

        D3D12RendererReference renderer_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
        std::size_t byteSize_ = 0;
        std::uint32_t usage_ = 0;
        std::uint32_t cpuAccess_ = 0;
        std::vector<std::uint8_t> recoveryShadow_;
    };
}
