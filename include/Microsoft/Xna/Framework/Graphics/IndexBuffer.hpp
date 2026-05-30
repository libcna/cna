#pragma once

#include <cstdint>
#include <memory>

#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"

namespace CNA::Internal::Backends
{
    class IIndexBufferBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /**
     * @brief Index buffer storing 16-bit indices.
     *
     * Mirrors `Microsoft.Xna.Framework.Graphics.IndexBuffer` (16-bit subset).
     *
     * @note Status: PARTIAL. Only 16-bit indices are supported in the early
     *       CNA 3D pipeline.
     */
    class IndexBuffer
    {
    public:
        /**
         * @brief Creates an empty index buffer with capacity for
         *        `indexCount` 16-bit indices.
         *
         * @note Status: IMPLEMENTED. CNA convenience overload that always
         *       allocates a 16-bit index buffer.
         */
        IndexBuffer(GraphicsDevice& device, int indexCount);

        /**
         * @brief XNA 4.0-shaped constructor.
         *
         * Mirrors `IndexBuffer(GraphicsDevice, IndexElementSize,
         * int indexCount, BufferUsage)`.
         *
         * @param device          Owning graphics device.
         * @param indexElementSize Element size. Only `SixteenBits` is
         *                         currently implemented; passing
         *                         `ThirtyTwoBits` throws with a clear
         *                         message.
         * @param indexCount      Number of indices the buffer can hold.
         * @param bufferUsage     Usage hint (ignored by the current backend).
         *
         * @note Status: PARTIAL. Only `IndexElementSize::SixteenBits` is
         *       supported.
         */
        IndexBuffer(GraphicsDevice& device,
                    IndexElementSize indexElementSize,
                    int indexCount,
                    BufferUsage bufferUsage);

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
