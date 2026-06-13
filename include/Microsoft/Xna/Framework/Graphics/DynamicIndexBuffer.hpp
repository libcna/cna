// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
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
            : IndexBuffer(device, indexElementSize, indexCount, bufferUsage)
        {
        }

        /**
         * @brief Constructs a DynamicIndexBuffer with the given device and 16-bit index count.
         * @param device     The graphics device.
         * @param indexCount Number of indices the buffer can hold.
         */
        DynamicIndexBuffer(GraphicsDevice& device, int indexCount)
            : IndexBuffer(device, indexCount)
        {
        }

        /** @brief Returns false; content is never lost in CNA. */
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        /** @brief Raised when the index buffer content is lost (never raised in CNA). */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Uploads index data with Discard semantics (equivalent to SetData).
         * @param indices Pointer to the source 16-bit index array.
         * @param count   Number of indices to upload.
         */
        void SetDataDiscard(const std::uint16_t* indices, int count)
        {
            SetData(indices, count);
        }
    };
}
