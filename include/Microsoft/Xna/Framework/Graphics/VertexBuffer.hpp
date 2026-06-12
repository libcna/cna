// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <vector>

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

    /**
     * @brief GPU vertex buffer for `VertexPositionColor` data.
     *
     * Mirrors the public surface of `Microsoft.Xna.Framework.Graphics.VertexBuffer`
     * for the minimal `VertexPositionColor` use-case.
     *
     * @note Status: PARTIAL. Only `VertexPositionColor` is supported; the
     *       generic `VertexDeclaration`/`SetData<T>` from XNA is not
     *       reproduced. Backed by EasyGL only; other backends throw.
     */
    class VertexBuffer
    {
    public:
        /**
         * @brief Creates an empty vertex buffer with capacity for
         *        `vertexCount` `VertexPositionColor` vertices.
         *
         * @note Status: IMPLEMENTED. CNA-only convenience overload kept for
         *       compatibility with earlier code; the EasyGL backend always
         *       interprets the storage as `VertexPositionColor`.
         */
        VertexBuffer(GraphicsDevice& device, int vertexCount);

        /**
         * @brief XNA 4.0-shaped constructor.
         *
         * Mirrors `VertexBuffer(GraphicsDevice, VertexDeclaration,
         * int vertexCount, BufferUsage)`.
         *
         * @param device         Owning graphics device.
         * @param vertexDeclaration Layout description (currently only used
         *                       for source-level XNA compatibility — the
         *                       EasyGL backend interprets the buffer as
         *                       `VertexPositionColor`).
         * @param vertexCount    Number of vertices the buffer can hold.
         * @param bufferUsage    Usage hint (ignored by the current backend).
         *
         * @note Status: PARTIAL. Only the `VertexPositionColor` declaration
         *       is honored at draw time; the `BufferUsage` hint is stored
         *       but otherwise ignored.
         */
        VertexBuffer(GraphicsDevice& device,
                     const VertexDeclaration& vertexDeclaration,
                     int vertexCount,
                     BufferUsage bufferUsage);

        ~VertexBuffer();

        VertexBuffer(const VertexBuffer&) = delete;
        VertexBuffer& operator=(const VertexBuffer&) = delete;

        void SetData(const VertexPositionColor* vertices, int count);
        void SetData(const VertexPositionColorTexture* vertices, int count);
        void SetData(const VertexPositionNormalTexture* vertices, int count);
        void SetData(const VertexPositionTexture* vertices, int count);

        /** Number of vertices currently stored. */
        [[nodiscard]] int VertexCount() const;

        /**
         * @brief Internal accessor used by the backend draw paths.
         * @note CNA-specific. Not part of the original XNA API.
         */
        [[nodiscard]] CNA::Internal::Backends::IVertexBufferBackend& GetBackend() const { return *backend_; }

    private:
        std::unique_ptr<CNA::Internal::Backends::IVertexBufferBackend> backend_;
    };
}
