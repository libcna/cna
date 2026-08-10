// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstring>
#include <limits>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        std::unique_ptr<CNA::Internal::Backends::IIndexBufferBackend> CreateIndexBufferBackend(
            GraphicsDevice& device,
            IndexElementSize indexElementSize,
            int indexCount)
        {
            if (indexCount < 0)
                throw System::ArgumentOutOfRangeException(
                    "indexCount", std::to_string(indexCount),
                    "The index count must be non-negative.");
            if (indexElementSize != IndexElementSize::SixteenBits &&
                indexElementSize != IndexElementSize::ThirtyTwoBits)
            {
                throw System::ArgumentOutOfRangeException(
                    "indexElementSize", std::to_string(static_cast<int>(indexElementSize)),
                    "Index buffers support only sixteen-bit or thirty-two-bit elements.");
            }
            return indexElementSize == IndexElementSize::ThirtyTwoBits
                       ? device.GetBackend().CreateIndexBuffer32(indexCount)
                       : device.GetBackend().CreateIndexBuffer16(indexCount);
        }

        std::size_t CheckedByteCount(int elementCount,
                                     std::size_t elementSize,
                                     const char* parameterName)
        {
            if (elementCount < 0)
                throw System::ArgumentOutOfRangeException(
                    parameterName, std::to_string(elementCount),
                    "The element count must be non-negative.");
            const auto unsignedCount = static_cast<std::size_t>(elementCount);
            if (unsignedCount > std::numeric_limits<std::size_t>::max() / elementSize)
                throw System::ArgumentOutOfRangeException(
                    parameterName, std::to_string(elementCount),
                    "The requested byte range is too large.");
            return unsignedCount * elementSize;
        }

        std::size_t CheckedByteOffset(int startIndex, std::size_t elementSize)
        {
            if (startIndex < 0)
                throw System::ArgumentOutOfRangeException(
                    "startIndex", std::to_string(startIndex),
                    "The start index must be non-negative.");
            const auto unsignedStart = static_cast<std::size_t>(startIndex);
            if (unsignedStart > std::numeric_limits<std::size_t>::max() / elementSize)
                throw System::ArgumentOutOfRangeException(
                    "startIndex", std::to_string(startIndex),
                    "The requested byte offset is too large.");
            return unsignedStart * elementSize;
        }
    }

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
        , backend_(CreateIndexBufferBackend(device, indexElementSize, indexCount))
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
        SetDataInternal(data, 0, count, IndexElementSize::SixteenBits,
                        SetDataOptions::None, false);
    }

    void IndexBuffer::SetData(const std::uint16_t* data, int startIndex, int elementCount)
    {
        SetDataInternal(data, startIndex, elementCount, IndexElementSize::SixteenBits,
                        SetDataOptions::None, false);
    }

    void IndexBuffer::GetData(std::uint16_t* data, int count)
    {
        GetData(data, 0, count);
    }

    void IndexBuffer::GetData(std::uint16_t* data, int startIndex, int elementCount)
    {
        GetDataInternal(data, startIndex, elementCount, IndexElementSize::SixteenBits);
    }

    void IndexBuffer::SetData(const std::uint32_t* data, int count)
    {
        SetDataInternal(data, 0, count, IndexElementSize::ThirtyTwoBits,
                        SetDataOptions::None, false);
    }

    void IndexBuffer::SetData(const std::uint32_t* data, int startIndex, int elementCount)
    {
        SetDataInternal(data, startIndex, elementCount, IndexElementSize::ThirtyTwoBits,
                        SetDataOptions::None, false);
    }

    void IndexBuffer::GetData(std::uint32_t* data, int count)
    {
        GetData(data, 0, count);
    }

    void IndexBuffer::GetData(std::uint32_t* data, int startIndex, int elementCount)
    {
        GetDataInternal(data, startIndex, elementCount, IndexElementSize::ThirtyTwoBits);
    }

    void IndexBuffer::SetDataInternal(const void* data,
                                      int startIndex,
                                      int elementCount,
                                      IndexElementSize dataElementSize,
                                      SetDataOptions options,
                                      bool useOptions)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");

        const std::size_t elementSize =
            dataElementSize == IndexElementSize::ThirtyTwoBits
                ? sizeof(std::uint32_t)
                : sizeof(std::uint16_t);
        const std::size_t byteOffset = CheckedByteOffset(startIndex, elementSize);
        const std::size_t byteCount = CheckedByteCount(
            elementCount, elementSize, "elementCount");

        if (dataElementSize != indexElementSize_)
            throw System::ArgumentException(
                "The source index width does not match the IndexBuffer element size.", "data");

        // Empty ranges are a real no-op. In particular, return before pointer arithmetic,
        // backend dispatch, native allocation/write, or the CPU shadow's memcpy/assign path.
        if (elementCount == 0)
            return;
        if (data == nullptr)
            throw System::ArgumentNullException("data");
        if (elementCount > indexCount_)
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "The upload exceeds the IndexBuffer's logical capacity.");

        const auto* source = static_cast<const std::uint8_t*>(data) + byteOffset;
        if (dataElementSize == IndexElementSize::ThirtyTwoBits)
        {
            if (useOptions)
                backend_->SetData32WithOptions(source, elementCount, options);
            else
                backend_->SetData32(source, elementCount);
        }
        else
        {
            if (useOptions)
                backend_->SetData16WithOptions(source, elementCount, options);
            else
                backend_->SetData16(source, elementCount);
        }

        // Task 930: cache exactly the logical source bytes for a future GetData() call.
        cpuShadow_.assign(source, source + byteCount);
    }

    void IndexBuffer::GetDataInternal(void* data,
                                      int startIndex,
                                      int elementCount,
                                      IndexElementSize dataElementSize)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");

        const std::size_t elementSize =
            dataElementSize == IndexElementSize::ThirtyTwoBits
                ? sizeof(std::uint32_t)
                : sizeof(std::uint16_t);
        const std::size_t byteOffset = CheckedByteOffset(startIndex, elementSize);
        const std::size_t byteCount = CheckedByteCount(
            elementCount, elementSize, "elementCount");

        if (dataElementSize != indexElementSize_)
            throw System::ArgumentException(
                "The destination index width does not match the IndexBuffer element size.", "data");

        // Preserve the public null rule without allowing an empty operation to bypass range
        // validation: null is legal exactly when the requested range is empty.
        if (elementCount != 0 && data == nullptr)
            throw System::ArgumentNullException("data");
        if (byteOffset > cpuShadow_.size() ||
            byteCount > cpuShadow_.size() - byteOffset)
        {
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "This parameter must be a valid index within the array.");
        }

        // As with SetData, an empty range is valid with a null pointer and must not reach memcpy.
        if (elementCount == 0)
            return;
        std::memcpy(data, cpuShadow_.data() + byteOffset, byteCount);
    }

    void IndexBuffer::SetDataWithOptions(const std::uint16_t* data, int startIndex,
                                         int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, IndexElementSize::SixteenBits,
                        options, true);
    }

    void IndexBuffer::SetDataWithOptions(const std::uint32_t* data, int startIndex,
                                         int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, IndexElementSize::ThirtyTwoBits,
                        options, true);
    }
}
