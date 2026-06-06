#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class DynamicIndexBuffer : public IndexBuffer
    {
    public:
        DynamicIndexBuffer(GraphicsDevice& device,
                           IndexElementSize indexElementSize,
                           int indexCount,
                           BufferUsage bufferUsage)
            : IndexBuffer(device, indexElementSize, indexCount, bufferUsage)
        {
        }

        DynamicIndexBuffer(GraphicsDevice& device, int indexCount)
            : IndexBuffer(device, indexCount)
        {
        }

        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        // XNA 4.0 compliance: ContentLost event (never raised in CNA — we never lose content)
        System::EventHandler<System::EventArgs> ContentLost;

        void SetDataDiscard(const std::uint16_t* indices, int count)
        {
            SetData(indices, count);
        }
    };
}
