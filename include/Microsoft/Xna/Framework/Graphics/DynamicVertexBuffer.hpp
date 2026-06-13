// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief A vertex buffer whose content is expected to change frequently. */
    class DynamicVertexBuffer : public VertexBuffer
    {
    public:
        /**
         * @brief Constructs a DynamicVertexBuffer with the given declaration, vertex count, and usage hint.
         * @param device            The graphics device.
         * @param vertexDeclaration Layout description for the vertex type.
         * @param vertexCount       Capacity in vertices.
         * @param bufferUsage       Usage hint for the buffer.
         */
        DynamicVertexBuffer(GraphicsDevice& device,
                            const VertexDeclaration& vertexDeclaration,
                            int vertexCount,
                            BufferUsage bufferUsage)
            : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage)
        {
        }

        /**
         * @brief Constructs a DynamicVertexBuffer with the given device and vertex count.
         * @param device      The graphics device.
         * @param vertexCount Capacity in vertices.
         */
        DynamicVertexBuffer(GraphicsDevice& device, int vertexCount)
            : VertexBuffer(device, vertexCount)
        {
        }

        /** @brief Returns false; content is never lost in CNA. */
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        /** @brief Raised when the vertex buffer content is lost (never raised in CNA). */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Uploads vertex data with Discard semantics (equivalent to SetData).
         * @param vertices Pointer to the source vertex array.
         * @param count    Number of vertices to upload.
         */
        void SetDataDiscard(const VertexPositionColor* vertices, int count)
        {
            SetData(vertices, count);
        }
    };
}
