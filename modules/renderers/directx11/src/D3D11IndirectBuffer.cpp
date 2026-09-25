// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX11/D3D11IndirectBuffer.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX11
{
    D3D11IndirectBuffer::D3D11IndirectBuffer(
        ID3D11Device* device, ID3D11DeviceContext* context, std::size_t byteSize,
        std::uint32_t usage, std::uint32_t cpuAccess)
        : device_(device), context_(context), byteSize_(byteSize), usage_(usage),
          cpuAccess_(cpuAccess)
    {
        if (!device || !context || byteSize == 0 ||
            byteSize > static_cast<std::size_t>(std::numeric_limits<UINT>::max() - 3))
            throw std::invalid_argument("D3D11 indirect buffer: invalid device or size");
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>((byteSize + 3u) & ~std::size_t{3});
        description.Usage = D3D11_USAGE_DEFAULT;
        description.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        if (FAILED(device_->CreateBuffer(&description, nullptr, buffer_.GetAddressOf())))
            throw std::runtime_error("D3D11 indirect buffer: native allocation failed");
    }

    void D3D11IndirectBuffer::SetData(const void* data, std::size_t byteSize)
    {
        if (!SetDataRangeEXT(0, data, byteSize))
            throw std::runtime_error("D3D11 indirect buffer: upload failed");
    }

    void D3D11IndirectBuffer::GetData(void* out, std::size_t byteSize) const
    {
        if (!GetDataRangeEXT(0, out, byteSize))
            throw std::runtime_error("D3D11 indirect buffer: readback failed");
    }

    bool D3D11IndirectBuffer::SetDataRangeEXT(
        std::size_t byteOffset, const void* data, std::size_t byteSize)
    {
        if ((cpuAccess_ & UINT32_C(0x02)) == 0 ||
            byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset ||
            (byteSize != 0 && data == nullptr))
            return false;
        if (byteSize == 0) return true;
        D3D11_BOX region{};
        region.left = static_cast<UINT>(byteOffset);
        region.right = static_cast<UINT>(byteOffset + byteSize);
        region.top = region.front = 0;
        region.bottom = region.back = 1;
        context_->UpdateSubresource(buffer_.Get(), 0, &region, data, 0, 0);
        return true;
    }

    bool D3D11IndirectBuffer::GetDataRangeEXT(
        std::size_t byteOffset, void* out, std::size_t byteSize) const
    {
        if ((cpuAccess_ & UINT32_C(0x01)) == 0 ||
            byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset ||
            (byteSize != 0 && out == nullptr))
            return false;
        if (byteSize == 0) return true;
        D3D11_BUFFER_DESC description{};
        buffer_->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        if (FAILED(device_->CreateBuffer(&description, nullptr, staging.GetAddressOf())))
            return false;
        context_->CopyResource(staging.Get(), buffer_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        std::memcpy(out, static_cast<const std::uint8_t*>(mapped.pData) + byteOffset,
                    byteSize);
        context_->Unmap(staging.Get(), 0);
        return true;
    }

    bool D3D11IndirectBuffer::CopyToEXT(
        IStorageBufferRenderer& destination, std::size_t sourceByteOffset,
        std::size_t destinationByteOffset, std::size_t byteSize)
    {
        auto* target = dynamic_cast<D3D11IndirectBuffer*>(&destination);
        if (!target || target->device_.Get() != device_.Get() ||
            (usage_ & UINT32_C(0x02)) == 0 ||
            (target->usage_ & UINT32_C(0x04)) == 0 ||
            sourceByteOffset > byteSize_ || byteSize > byteSize_ - sourceByteOffset ||
            destinationByteOffset > target->byteSize_ ||
            byteSize > target->byteSize_ - destinationByteOffset)
            return false;
        if (byteSize == 0) return true;
        D3D11_BOX region{};
        region.left = static_cast<UINT>(sourceByteOffset);
        region.right = static_cast<UINT>(sourceByteOffset + byteSize);
        region.top = region.front = 0;
        region.bottom = region.back = 1;
        context_->CopySubresourceRegion(target->buffer_.Get(), 0,
                                        static_cast<UINT>(destinationByteOffset), 0, 0,
                                        buffer_.Get(), 0, &region);
        return true;
    }
}
