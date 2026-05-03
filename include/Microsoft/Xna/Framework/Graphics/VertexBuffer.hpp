#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

namespace CNA::Internal::Backends {
    class IVertexBufferBackend;
}

namespace Microsoft::Xna::Framework::Graphics {

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
    class VertexBuffer {
    public:
        /**
         * @brief Creates an empty vertex buffer with capacity for
         *        `vertexCount` `VertexPositionColor` vertices.
         */
        VertexBuffer(GraphicsDevice& device, int vertexCount);
        ~VertexBuffer();

        VertexBuffer(const VertexBuffer&) = delete;
        VertexBuffer& operator=(const VertexBuffer&) = delete;

        /** Uploads `count` vertices into the buffer (replaces previous content). */
        void SetData(const VertexPositionColor* vertices, int count);

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
