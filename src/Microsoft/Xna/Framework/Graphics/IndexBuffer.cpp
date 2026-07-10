// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstring>

namespace Microsoft::Xna::Framework::Graphics
{
    IndexBuffer::IndexBuffer(GraphicsDevice& device, int indexCount)
        : IndexBuffer(device, IndexElementSize::SixteenBits, indexCount, BufferUsage::None, false)
    {
    }

    IndexBuffer::IndexBuffer(GraphicsDevice& device,
                             IndexElementSize indexElementSize,
                             int indexCount,
                             BufferUsage bufferUsage)
        : IndexBuffer(device, indexElementSize, indexCount, bufferUsage, false)
    {
    }

    IndexBuffer::IndexBuffer(GraphicsDevice& device,
                             IndexElementSize indexElementSize,
                             int indexCount,
                             BufferUsage bufferUsage,
                             bool /*dynamic*/)
        : GraphicsResource(&device)
        , backend_(indexElementSize == IndexElementSize::ThirtyTwoBits
                       ? device.GetBackend().CreateIndexBuffer32(indexCount)
                       : device.GetBackend().CreateIndexBuffer16(indexCount))
        , indexElementSize_(indexElementSize)
        , bufferUsage_(bufferUsage)
        , indexCount_(indexCount)
    {
    }

    IndexBuffer::~IndexBuffer() = default;
    IndexBuffer::IndexBuffer(IndexBuffer&&) noexcept = default;
    IndexBuffer& IndexBuffer::operator=(IndexBuffer&&) noexcept = default;

    void IndexBuffer::Dispose(bool disposing)
    {
        backend_.reset();
        GraphicsResource::Dispose(disposing);
    }

    GetTypeNameCPP(IndexBuffer, "Microsoft.Xna.Framework.Graphics.IndexBuffer")

    void IndexBuffer::SetData(const std::uint16_t* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        backend_->SetData16(data, count);

        // Task 930: cache the raw index bytes for a future GetData() call.
        cpuShadow_.resize(static_cast<std::size_t>(count) * sizeof(std::uint16_t));
        std::memcpy(cpuShadow_.data(), data, cpuShadow_.size());
    }

    void IndexBuffer::SetData(const std::uint16_t* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void IndexBuffer::GetData(std::uint16_t* data, int count)
    {
        GetData(data, 0, count);
    }

    void IndexBuffer::GetData(std::uint16_t* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        const std::size_t byteOffset = static_cast<std::size_t>(startIndex) * sizeof(std::uint16_t);
        const std::size_t byteCount  = static_cast<std::size_t>(elementCount) * sizeof(std::uint16_t);
        if (byteOffset + byteCount > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        std::memcpy(data, cpuShadow_.data() + byteOffset, byteCount);
    }

    void IndexBuffer::SetData(const std::uint32_t* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        backend_->SetData32(data, count);

        cpuShadow_.resize(static_cast<std::size_t>(count) * sizeof(std::uint32_t));
        std::memcpy(cpuShadow_.data(), data, cpuShadow_.size());
    }

    void IndexBuffer::SetData(const std::uint32_t* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void IndexBuffer::GetData(std::uint32_t* data, int count)
    {
        GetData(data, 0, count);
    }

    void IndexBuffer::GetData(std::uint32_t* data, int startIndex, int elementCount)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        const std::size_t byteOffset = static_cast<std::size_t>(startIndex) * sizeof(std::uint32_t);
        const std::size_t byteCount  = static_cast<std::size_t>(elementCount) * sizeof(std::uint32_t);
        if (byteOffset + byteCount > cpuShadow_.size())
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        std::memcpy(data, cpuShadow_.data() + byteOffset, byteCount);
    }

    void IndexBuffer::SetDataWithOptions(const std::uint16_t* data, int startIndex,
                                         int elementCount, SetDataOptions options)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        backend_->SetData16WithOptions(data + startIndex, elementCount, options);
    }

    void IndexBuffer::SetDataWithOptions(const std::uint32_t* data, int startIndex,
                                         int elementCount, SetDataOptions options)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        backend_->SetData32WithOptions(data + startIndex, elementCount, options);
    }
}
