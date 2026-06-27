// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/ObjectDisposedException.hpp"

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
    }

    void IndexBuffer::SetData(const std::uint16_t* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
    }

    void IndexBuffer::SetData(const std::uint32_t* data, int count)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        backend_->SetData32(data, count);
    }

    void IndexBuffer::SetData(const std::uint32_t* data, int startIndex, int elementCount)
    {
        SetData(data + startIndex, elementCount);
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
