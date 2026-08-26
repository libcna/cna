// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/IContentLosable.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief A vertex buffer whose content is expected to change frequently. */
    class DynamicVertexBuffer : public VertexBuffer,
            public CNA::Internal::Graphics::IContentLosable
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

        /**
         * @brief Whether this buffer's contents were lost to a device reset.
         *
         * True from the moment a renderer reports a real device reset until the buffer is written
         * again with `SetData`. Renderers whose API cannot lose a device never set it.
         */
        [[nodiscard]] bool getIsContentLostProperty() const { return contentLost_; }

        /** @brief Marks the content lost and raises ContentLost. */
        CNAEXT void NotifyContentLostEXT() override
        {
            contentLost_ = true;
            ContentLost.Raise(this, System::EventArgs::Empty);
        }

        /** @brief Clears the lost flag; called when the buffer is written with SetData. */
        CNAEXT void ClearContentLostEXT() noexcept override { contentLost_ = false; }

        /**
         * @brief Raised when this vertex buffer's content is lost to a device reset.
         *
         * Raised for real on the renderers whose API can lose a device (DirectX9,
         * Direct2D, Skia). Families that cannot lose one never raise it.
         */
        System::EventHandler<System::EventArgs> ContentLost;

        /**
         * @brief Uploads VertexPositionColor vertices with streaming semantics.
         *
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
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
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
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
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
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
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
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

    private:
        /** @brief Set by a real renderer-reported device reset; cleared by the next write. */
        bool contentLost_ = false;
    };
}
