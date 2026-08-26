// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/IContentLosable.hpp"
#include <cstdint>
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief An index buffer whose content is expected to change frequently. */
    class DynamicIndexBuffer : public IndexBuffer,
            public CNA::Internal::Graphics::IContentLosable
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
        /**
         * @brief Whether this index buffer's contents were lost to a device reset.
         *
         * True only from the moment a renderer reported a real reset until the content is
         * written again. Renderers whose API cannot lose a device never set it.
         */
        [[nodiscard]] bool getIsContentLostProperty() const { return contentLost_; }

        CNAEXT void NotifyContentLostEXT() override
        {
            contentLost_ = true;
            ContentLost.Raise(this, System::EventArgs::Empty);
        }

        /** @brief Clears the lost flag once the content has been written again. */
        CNAEXT void ClearContentLostEXT() noexcept { contentLost_ = false; }

        /** @brief Raised when the index buffer content is lost (never raised in CNA). */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Uploads a slice of 16-bit indices with streaming semantics.
         *
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
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
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
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

    private:
        /** @brief Set by a real renderer-reported device reset; cleared by the next write. */
        bool contentLost_ = false;
    };
}
