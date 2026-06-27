// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief An index buffer whose content is expected to change frequently. */
    class DynamicIndexBuffer : public IndexBuffer
    {
    public:
        /**
         * @brief Constructs a DynamicIndexBuffer with the given element size, index count, and usage hint.
         * @param device           The graphics device.
         * @param indexElementSize Element size — SixteenBits or ThirtyTwoBits.
         * @param indexCount       Number of indices the buffer can hold.
         * @param bufferUsage      Usage hint for the buffer.
         */
        DynamicIndexBuffer(GraphicsDevice& device,
                           IndexElementSize indexElementSize,
                           int indexCount,
                           BufferUsage bufferUsage)
            : IndexBuffer(device, indexElementSize, indexCount, bufferUsage, true)
        {
        }

        /** @brief Returns false; content is never lost in CNA. */
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        /** @brief Raised when the index buffer content is lost (never raised in CNA). */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Uploads a slice of 16-bit indices with streaming semantics.
         *
         * The @p options hint is accepted for API conformance but is currently
         * ignored by all CNA backends — all writes go to the buffer beginning.
         *
         * @param data         Pointer to the source 16-bit index array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of indices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const std::uint16_t* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            IndexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads a slice of 32-bit indices with streaming semantics.
         *
         * The @p options hint is accepted for API conformance but is currently
         * ignored by all CNA backends — all writes go to the buffer beginning.
         *
         * @param data         Pointer to the source 32-bit index array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of indices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const std::uint32_t* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            IndexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }
    };
}
