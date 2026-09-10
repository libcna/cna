// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        constexpr IndexElementSize CanonicalizeIndexElementSize(
            IndexElementSize indexElementSize) noexcept
        {
            return indexElementSize == IndexElementSize::SixteenBits
                       ? IndexElementSize::SixteenBits
                       : IndexElementSize::ThirtyTwoBits;
        }

        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> CreateIndexBufferRenderer(
            GraphicsDevice& device,
            IndexElementSize indexElementSize,
            int indexCount)
        {
            if (indexCount <= 0)
                throw System::ArgumentOutOfRangeException(
                    "indexCount", std::to_string(indexCount),
                    "The index count must be greater than zero.");
            indexElementSize = CanonicalizeIndexElementSize(indexElementSize);
            if (indexElementSize == IndexElementSize::ThirtyTwoBits &&
                device.getGraphicsProfileProperty() == GraphicsProfile::Reach)
            {
                throw System::NotSupportedException(
                    "Thirty-two-bit index buffers are not supported by the Reach graphics profile.");
            }

            constexpr std::int64_t maximumBufferBytes = 67'108'863;
            const std::int64_t elementBytes =
                indexElementSize == IndexElementSize::ThirtyTwoBits ? 4 : 2;
            if (static_cast<std::int64_t>(indexCount) * elementBytes > maximumBufferBytes)
            {
                throw System::NotSupportedException(
                    "The index buffer exceeds the active graphics profile limit of 67108863 bytes.");
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
        , indexElementSize_(CanonicalizeIndexElementSize(indexElementSize))
        , bufferUsage_(bufferUsage)
        , indexCount_(indexCount)
    {
    }

    IndexBuffer::~IndexBuffer()
    {
        if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
            graphicsDevice_->DetachDestroyedIndexBuffer(this);
        Dispose(false);
    }
    IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept
        : GraphicsResource(std::move(other))
        , renderer_(std::move(other.renderer_))
        , indexElementSize_(other.indexElementSize_)
        , bufferUsage_(other.bufferUsage_)
        , indexCount_(other.indexCount_)
        , cpuShadow_(std::move(other.cpuShadow_))
    {
        if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
            graphicsDevice_->TransferMovedIndexBuffer(&other, this);
        other.indexCount_ = 0;
    }

    IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept
    {
        if (this != &other)
        {
            GraphicsDevice* const previousDevice = graphicsDevice_;
            const bool previousDeviceAlive = !graphicsDeviceLifetime_.expired();
            if (previousDevice != nullptr && previousDeviceAlive &&
                previousDevice != other.graphicsDevice_)
            {
                previousDevice->DetachDestroyedIndexBuffer(this);
            }
            GraphicsResource::operator=(std::move(other));
            renderer_ = std::move(other.renderer_);
            indexElementSize_ = other.indexElementSize_;
            bufferUsage_ = other.bufferUsage_;
            indexCount_ = other.indexCount_;
            cpuShadow_ = std::move(other.cpuShadow_);
            if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
                graphicsDevice_->TransferMovedIndexBuffer(&other, this);
            other.indexCount_ = 0;
        }
        return *this;
    }

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

        SetDataBytesAtInternal(offsetInBytes, data, startIndex, elementCount, elementSize,
                               SetDataOptions::None, false);
    }

    void IndexBuffer::SetDataBytesAtInternal(const int offsetInBytes,
                                             const void* const data,
                                             const int startIndex,
                                             const int elementCount,
                                             const std::size_t elementSize,
                                             SetDataOptions options,
                                             const bool useOptions)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        if (offsetInBytes < 0)
            throw System::ArgumentOutOfRangeException(
                "offsetInBytes", std::to_string(offsetInBytes),
                "This parameter must not be negative.");

        const std::size_t sourceByteOffset = CheckedByteOffset(startIndex, elementSize);
        const std::size_t byteCount =
            CheckedByteCount(elementCount, elementSize, "elementCount");
        if (elementCount == 0)
            return;
        if (data == nullptr)
            throw System::ArgumentNullException("data");
        ThrowIfSetDataResourceInUse(options, useOptions);

        const std::size_t nativeElementSize =
            indexElementSize_ == IndexElementSize::ThirtyTwoBits
                ? sizeof(std::uint32_t)
                : sizeof(std::uint16_t);
        const std::size_t capacity =
            CheckedByteCount(indexCount_, nativeElementSize, "indexCount");
        const std::size_t destinationOffset = static_cast<std::size_t>(offsetInBytes);
        if (destinationOffset > capacity || byteCount > capacity - destinationOffset)
            throw System::InvalidOperationException(
                "The data is not the correct size for this IndexBuffer.");

        const auto* source = static_cast<const std::uint8_t*>(data) + sourceByteOffset;
        if (destinationOffset == 0 && elementSize == nativeElementSize)
        {
            if (auto* const losable =
                    dynamic_cast<CNA::Internal::Graphics::IContentLosable*>(this))
            {
                losable->ClearContentLostEXT();
            }
            if (indexElementSize_ == IndexElementSize::ThirtyTwoBits)
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
            cpuShadow_.assign(source, source + byteCount);
            cpuShadow_.resize(capacity, 0U);
            return;
        }

        // The renderer contract has no byte-window operation. Compose the exact XNA-visible bytes
        // in the shared shadow, then upload the whole native index buffer without forwarding a
        // NoOverwrite promise that a whole-buffer transfer could not truthfully make.
        if (cpuShadow_.size() < capacity)
            cpuShadow_.resize(capacity, 0U);
        std::copy(source, source + byteCount,
                  cpuShadow_.begin() + static_cast<std::ptrdiff_t>(destinationOffset));

        if (auto* const losable = dynamic_cast<CNA::Internal::Graphics::IContentLosable*>(this))
            losable->ClearContentLostEXT();
        if (indexElementSize_ == IndexElementSize::ThirtyTwoBits)
            renderer_->SetData32(cpuShadow_.data(), indexCount_);
        else
            renderer_->SetData16(cpuShadow_.data(), indexCount_);
    }

    void IndexBuffer::SetDataInternal(const void* data,
                                      int startIndex,
                                      int elementCount,
                                      IndexElementSize dataElementSize,
                                      SetDataOptions options,
                                      bool useOptions)
    {
        const std::size_t elementSize =
            dataElementSize == IndexElementSize::ThirtyTwoBits
                ? sizeof(std::uint32_t)
                : sizeof(std::uint16_t);
        SetDataBytesAtInternal(
            0, data, startIndex, elementCount, elementSize, options, useOptions);
    }

    void IndexBuffer::ThrowIfSetDataResourceInUse(SetDataOptions options,
                                                  bool useOptions) const
    {
        if (useOptions &&
            (options == SetDataOptions::Discard || options == SetDataOptions::NoOverwrite))
        {
            return;
        }
        GraphicsDevice* const device = getGraphicsDeviceProperty();
        if (device != nullptr && device->GetIndexBuffer() == this)
            throw System::InvalidOperationException("The index buffer resource is in use.");
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
        GetDataBytesAtInternal(0, data, startIndex, elementCount, elementSize);
    }

    void IndexBuffer::GetDataBytesAtInternal(const int offsetInBytes,
                                             void* const data,
                                             const int startIndex,
                                             const int elementCount,
                                             const std::size_t elementSize) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("IndexBuffer");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        if (offsetInBytes < 0)
            throw System::ArgumentOutOfRangeException(
                "offsetInBytes", std::to_string(offsetInBytes),
                "This parameter must not be negative.");

        const std::size_t destinationByteOffset = CheckedByteOffset(startIndex, elementSize);
        const std::size_t byteCount =
            CheckedByteCount(elementCount, elementSize, "elementCount");

        // Preserve the public null rule without allowing an empty operation to bypass range
        // validation: null is legal exactly when the requested range is empty.
        if (elementCount != 0 && data == nullptr)
            throw System::ArgumentNullException("data");
        const std::size_t sourceByteOffset = static_cast<std::size_t>(offsetInBytes);
        if (sourceByteOffset > cpuShadow_.size() ||
            byteCount > cpuShadow_.size() - sourceByteOffset)
        {
            throw System::InvalidOperationException(
                "The data is not the correct size for this IndexBuffer.");
        }

        // As with SetData, an empty range is valid with a null pointer and must not reach memcpy.
        if (elementCount == 0)
            return;
        std::memcpy(static_cast<std::uint8_t*>(data) + destinationByteOffset,
                    cpuShadow_.data() + sourceByteOffset, byteCount);
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
