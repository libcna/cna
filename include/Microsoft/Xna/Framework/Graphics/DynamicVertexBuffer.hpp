// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
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
            : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage, true)
        {
        }

        /** @brief Returns false; content is never lost in CNA. */
        [[nodiscard]] bool getIsContentLostProperty() const { return false; }

        /** @brief Raised when the vertex buffer content is lost (never raised in CNA). */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Uploads VertexPositionColor vertices with streaming semantics.
         *
         * Most CNA backends honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionColor* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads VertexPositionColorTexture vertices with streaming semantics.
         *
         * Most CNA backends honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionColorTexture* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads VertexPositionNormalTexture vertices with streaming semantics.
         *
         * Most CNA backends honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionNormalTexture* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads VertexPositionTexture vertices with streaming semantics.
         *
         * Most CNA backends honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionTexture* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }
    };
}
