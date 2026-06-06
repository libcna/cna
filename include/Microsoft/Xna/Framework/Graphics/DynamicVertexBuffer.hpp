#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class DynamicVertexBuffer : public VertexBuffer
    {
    public:
        DynamicVertexBuffer(GraphicsDevice& device,
                            const VertexDeclaration& vertexDeclaration,
                            int vertexCount,
                            BufferUsage bufferUsage)
            : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage)
        {
        }

        DynamicVertexBuffer(GraphicsDevice& device, int vertexCount)
            : VertexBuffer(device, vertexCount)
        {
        }

        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        // XNA 4.0 compliance: ContentLost event (never raised in CNA — we never lose content)
        System::EventHandler<System::EventArgs> ContentLost;

        void SetDataDiscard(const VertexPositionColor* vertices, int count)
        {
            SetData(vertices, count);
        }
    };
}
