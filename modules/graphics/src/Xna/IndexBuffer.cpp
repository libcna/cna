// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> CreateIndexBufferRenderer(
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
                       ? device.GetRenderer().CreateIndexBuffer32(indexCount)
                       : device.GetRenderer().CreateIndexBuffer16(indexCount);
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
        , renderer_(CreateIndexBufferRenderer(device, indexElementSize, indexCount))
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
        renderer_.reset();
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

    void IndexBuffer::SetDataAtEXT(const int offsetInBytes, const std::uint16_t* const data,
                                   const int startIndex, const int elementCount)
    {
        SetDataAtInternal(
            offsetInBytes, data, startIndex, elementCount, IndexElementSize::SixteenBits);
    }

    void IndexBuffer::SetDataAtEXT(const int offsetInBytes, const std::uint32_t* const data,
                                   const int startIndex, const int elementCount)
    {
        SetDataAtInternal(
            offsetInBytes, data, startIndex, elementCount, IndexElementSize::ThirtyTwoBits);
    }

    void IndexBuffer::SetDataAtInternal(const int offsetInBytes,
                                        const void* const data,
                                        const int startIndex,
                                        const int elementCount,
                                        const IndexElementSize dataElementSize)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        if (dataElementSize != indexElementSize_)
            throw System::ArgumentException(
                "The source index width does not match the IndexBuffer element size.", "data");
        if (offsetInBytes < 0)
            throw System::ArgumentOutOfRangeException(
                "offsetInBytes", std::to_string(offsetInBytes),
                "This parameter must not be negative.");

        const std::size_t elementSize =
            dataElementSize == IndexElementSize::ThirtyTwoBits
                ? sizeof(std::uint32_t)
                : sizeof(std::uint16_t);
        if (static_cast<std::size_t>(offsetInBytes) % elementSize != 0)
            throw System::ArgumentException(
                "The destination offset must fall on an index boundary.", "offsetInBytes");

        const std::size_t sourceByteOffset = CheckedByteOffset(startIndex, elementSize);
        const std::size_t windowBytes =
            CheckedByteCount(elementCount, elementSize, "elementCount");
        if (elementCount == 0)
            return;
        if (data == nullptr)
            throw System::ArgumentNullException("data");

        const std::size_t capacity =
            CheckedByteCount(indexCount_, elementSize, "indexCount");
        if (windowBytes > capacity - static_cast<std::size_t>(offsetInBytes))
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "The windowed upload exceeds the IndexBuffer's logical capacity.");

        // The shadow is where a window can be composed at all: the renderer contract replaces
        // whole-buffer contents. Growing it to the buffer's full capacity is what makes indices
        // never written by any upload read as zero rather than as whatever a shorter earlier
        // upload happened to leave behind.
        if (cpuShadow_.size() < capacity)
            cpuShadow_.resize(capacity, 0U);

        const auto* source = static_cast<const std::uint8_t*>(data) + sourceByteOffset;
        std::copy(source, source + windowBytes,
                  cpuShadow_.begin() + static_cast<std::ptrdiff_t>(offsetInBytes));

        const int uploadCount = static_cast<int>(capacity / elementSize);
        if (dataElementSize == IndexElementSize::ThirtyTwoBits)
            renderer_->SetData32(cpuShadow_.data(), uploadCount);
        else
            renderer_->SetData16(cpuShadow_.data(), uploadCount);
    }

    void IndexBuffer::SetDataInternal(const void* data,
                                      int startIndex,
                                      int elementCount,
                                      IndexElementSize dataElementSize,
                                      SetDataOptions options,
                                      bool useOptions)
    {
        // CABI-15: see VertexBuffer::UploadValidatedData -- one clear on the write path.
        if (auto* const losable = dynamic_cast<CNA::Internal::Graphics::IContentLosable*>(this)) {
            static_cast<DynamicIndexBuffer*>(losable)->ClearContentLostEXT();
        }
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
        // renderer dispatch, native allocation/write, or the CPU shadow's memcpy/assign path.
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
                renderer_->SetData32WithOptions(source, elementCount, options);
            else
                renderer_->SetData32(source, elementCount);
        }
        else
        {
            if (useOptions)
                renderer_->SetData16WithOptions(source, elementCount, options);
            else
                renderer_->SetData16(source, elementCount);
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
