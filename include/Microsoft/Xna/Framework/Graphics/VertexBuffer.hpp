// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"

namespace CNA::Internal::Backends
{
    class IVertexBufferBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /** @brief GPU vertex buffer for storing vertex data. */
    class VertexBuffer
    {
    public:
        /**
         * @brief Creates an empty vertex buffer with capacity for `vertexCount` vertices.
         * @param device      Owning graphics device.
         * @param vertexCount Number of vertices the buffer can hold.
         */
        VertexBuffer(GraphicsDevice& device, int vertexCount);

        /**
         * @brief Constructs a vertex buffer from a vertex declaration.
         *
         * Mirrors `VertexBuffer(GraphicsDevice, VertexDeclaration, int, BufferUsage)`.
         *
         * @param device            Owning graphics device.
         * @param vertexDeclaration Vertex layout description.
         * @param vertexCount       Number of vertices the buffer can hold.
         * @param bufferUsage       Usage hint.
         */
        VertexBuffer(GraphicsDevice& device,
                     const VertexDeclaration& vertexDeclaration,
                     int vertexCount,
                     BufferUsage bufferUsage);

        /** @brief Destructor. */
        NOXNA ~VertexBuffer();

        /** @brief Copying is not allowed. */
        VertexBuffer(const VertexBuffer&) = delete;
        /** @brief Copy-assignment is not allowed. */
        VertexBuffer& operator=(const VertexBuffer&) = delete;

        /**
         * @brief Uploads VertexPositionColor vertex data to the GPU buffer.
         * @param vertices Pointer to the source vertex array.
         * @param count    Number of vertices to upload.
         */
        void SetData(const VertexPositionColor* vertices, int count);
        /**
         * @brief Uploads VertexPositionColorTexture vertex data to the GPU buffer.
         * @param vertices Pointer to the source vertex array.
         * @param count    Number of vertices to upload.
         */
        void SetData(const VertexPositionColorTexture* vertices, int count);
        /**
         * @brief Uploads VertexPositionNormalTexture vertex data to the GPU buffer.
         * @param vertices Pointer to the source vertex array.
         * @param count    Number of vertices to upload.
         */
        void SetData(const VertexPositionNormalTexture* vertices, int count);
        /**
         * @brief Uploads VertexPositionTexture vertex data to the GPU buffer.
         * @param vertices Pointer to the source vertex array.
         * @param count    Number of vertices to upload.
         */
        void SetData(const VertexPositionTexture* vertices, int count);

        /**
         * @brief Returns the number of vertices this buffer was created to hold.
         * @return The vertex capacity of the buffer.
         */
        [[nodiscard]] int getVertexCountProperty() const;

        /**
         * @brief Uploads raw vertex data with an explicit per-vertex byte stride.
         *
         * Use this overload when uploading GPU-compact vertex layouts that have no
         * corresponding typed XNA vertex struct (e.g. the 52-byte skinned layout).
         *
         * @param data   Pointer to the raw vertex data.
         * @param count  Number of vertices.
         * @param stride Size of one vertex in bytes.
         */
        NOXNA void SetDataRaw(const void* data, int count, int stride);

        /**
         * @brief Internal accessor used by the backend draw paths.
         */
        NOXNA [[nodiscard]] CNA::Internal::Backends::IVertexBufferBackend& GetBackend() const { return *backend_; }

    private:
        std::unique_ptr<CNA::Internal::Backends::IVertexBufferBackend> backend_;
    };
}
