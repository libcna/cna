#pragma once

#include <cstdint>
#include <memory>

namespace CNA::Internal::Backends {
    class IIndexBufferBackend;
}

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice;

    /**
     * @brief Index buffer storing 16-bit indices.
     *
     * Mirrors `Microsoft.Xna.Framework.Graphics.IndexBuffer` (16-bit subset).
     *
     * @note Status: PARTIAL. Only 16-bit indices are supported in the early
     *       CNA 3D pipeline.
     */
    class IndexBuffer {
    public:
        /**
         * @brief Creates an empty index buffer with capacity for
         *        `indexCount` 16-bit indices.
         */
        IndexBuffer(GraphicsDevice& device, int indexCount);
        ~IndexBuffer();

        IndexBuffer(const IndexBuffer&) = delete;
        IndexBuffer& operator=(const IndexBuffer&) = delete;

        /** Uploads `count` 16-bit indices (replaces previous content). */
        void SetData(const std::uint16_t* indices, int count);

        [[nodiscard]] int IndexCount() const;

        /**
         * @brief Internal accessor used by the backend draw paths.
         * @note CNA-specific. Not part of the original XNA API.
         */
        [[nodiscard]] CNA::Internal::Backends::IIndexBufferBackend& GetBackend() const { return *backend_; }

    private:
        std::unique_ptr<CNA::Internal::Backends::IIndexBufferBackend> backend_;
    };
}
