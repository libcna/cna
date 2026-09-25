// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace CNA::Internal::Renderers::DirectX11
{
    /** @brief D3D11 byte buffer for storage, constants, indirect draws and transfers. */
    class D3D11IndirectBuffer final : public IStorageBufferRenderer
    {
    public:
        /**
         * @brief Allocates native buffers for the declared GPU roles.
         * @param device Owning D3D11 device.
         * @param context Immediate context used for transfers.
         * @param byteSize Logical allocation size.
         * @param usage Declared portable usage bits.
         * @param cpuAccess Declared portable CPU access bits.
         */
        D3D11IndirectBuffer(ID3D11Device* device, ID3D11DeviceContext* context,
                            std::size_t byteSize, std::uint32_t usage,
                            std::uint32_t cpuAccess);

        /**
         * @brief Uploads bytes from the beginning of the buffer.
         * @param data Source bytes.
         * @param byteSize Number of bytes to upload.
         */
        void SetData(const void* data, std::size_t byteSize) override;
        /**
         * @brief Reads bytes from the beginning of the buffer.
         * @param out Destination bytes.
         * @param byteSize Number of bytes to read.
         */
        void GetData(void* out, std::size_t byteSize) const override;
        /**
         * @brief Uploads an exact byte range.
         * @param byteOffset First destination byte.
         * @param data Source bytes.
         * @param byteSize Number of bytes to upload.
         * @return True when the complete range was accepted.
         */
        [[nodiscard]] bool SetDataRangeEXT(std::size_t byteOffset, const void* data,
                                           std::size_t byteSize) override;
        /**
         * @brief Reads an exact byte range from GPU memory.
         * @param byteOffset First source byte.
         * @param out Destination bytes.
         * @param byteSize Number of bytes to read.
         * @return True when the complete range was read.
         */
        [[nodiscard]] bool GetDataRangeEXT(std::size_t byteOffset, void* out,
                                           std::size_t byteSize) const override;
        /**
         * @brief Copies a byte range to another buffer on the same device.
         * @param destination Destination buffer.
         * @param sourceByteOffset First source byte.
         * @param destinationByteOffset First destination byte.
         * @param byteSize Number of bytes to copy.
         * @return True when the GPU copy was issued.
         */
        [[nodiscard]] bool CopyToEXT(IStorageBufferRenderer& destination,
                                     std::size_t sourceByteOffset,
                                     std::size_t destinationByteOffset,
                                     std::size_t byteSize) override;
        /**
         * @brief Returns the declared logical byte count.
         * @return Number of bytes visible to callers.
         */
        [[nodiscard]] std::size_t GetByteSize() const override { return byteSize_; }
        /**
         * @brief Returns the declared usage bits.
         * @return Portable usage mask.
         */
        [[nodiscard]] std::uint32_t GetUsageEXT() const override { return usage_; }
        /**
         * @brief Returns the declared CPU access bits.
         * @return Portable access mask.
         */
        [[nodiscard]] std::uint32_t GetCpuAccessEXT() const override { return cpuAccess_; }
        /**
         * @brief Returns an indirect-argument buffer with the latest GPU contents.
         * @return Device-owned D3D11 argument handle.
         */
        [[nodiscard]] ID3D11Buffer* GetBufferEXT() const;
        /**
         * @brief Returns the raw storage UAV, when Storage usage was declared.
         * @return Native unordered-access view, or null.
         */
        [[nodiscard]] ID3D11UnorderedAccessView* GetUnorderedAccessViewEXT() const
        {
            return storageUav_.Get();
        }
        /**
         * @brief Returns a constant buffer with the latest GPU contents.
         * @return Native constant-buffer handle, or null.
         */
        [[nodiscard]] ID3D11Buffer* GetConstantBufferEXT() const;
        /**
         * @brief Returns the device that owns the native buffer.
         * @return Device identity for cross-device validation.
         */
        [[nodiscard]] ID3D11Device* GetDeviceEXT() const { return device_.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indirectBuffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer_;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> storageUav_;
        std::size_t byteSize_ = 0;
        std::uint32_t usage_ = 0;
        std::uint32_t cpuAccess_ = 0;
    };
}
