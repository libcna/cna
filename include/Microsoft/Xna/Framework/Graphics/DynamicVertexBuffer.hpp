// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// A vertex buffer whose content is expected to change frequently.
    class DynamicVertexBuffer : public VertexBuffer
    {
    public:
        /// Constructs a DynamicVertexBuffer with the given declaration, vertex count, and usage hint.
        DynamicVertexBuffer(GraphicsDevice& device,
                            const VertexDeclaration& vertexDeclaration,
                            int vertexCount,
                            BufferUsage bufferUsage)
            : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage)
        {
        }

        /// Constructs a DynamicVertexBuffer with the given device and vertex count.
        DynamicVertexBuffer(GraphicsDevice& device, int vertexCount)
            : VertexBuffer(device, vertexCount)
        {
        }

        /// Gets whether the vertex buffer content has been lost.
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        // XNA 4.0 compliance: ContentLost event (never raised in CNA — we never lose content)
        /// Raised when the vertex buffer content is lost (never raised in CNA).
        System::EventHandler<System::EventArgs> ContentLost;

        /// Uploads vertex data using SetDataOptions::Discard semantics.
        void SetDataDiscard(const VertexPositionColor* vertices, int count)
        {
            SetData(vertices, count);
        }
    };
}
