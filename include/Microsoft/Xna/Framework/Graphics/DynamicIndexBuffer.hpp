// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// An index buffer whose content is expected to change frequently.
    class DynamicIndexBuffer : public IndexBuffer
    {
    public:
        /// Constructs a DynamicIndexBuffer with the given element size, index count, and usage hint.
        DynamicIndexBuffer(GraphicsDevice& device,
                           IndexElementSize indexElementSize,
                           int indexCount,
                           BufferUsage bufferUsage)
            : IndexBuffer(device, indexElementSize, indexCount, bufferUsage)
        {
        }

        /// Constructs a DynamicIndexBuffer with the given device and index count.
        DynamicIndexBuffer(GraphicsDevice& device, int indexCount)
            : IndexBuffer(device, indexCount)
        {
        }

        /// Gets whether the index buffer content has been lost.
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        // XNA 4.0 compliance: ContentLost event (never raised in CNA — we never lose content)
        /// Raised when the index buffer content is lost (never raised in CNA).
        System::EventHandler<System::EventArgs> ContentLost;

        /// Uploads index data using SetDataOptions::Discard semantics.
        void SetDataDiscard(const std::uint16_t* indices, int count)
        {
            SetData(indices, count);
        }
    };
}
