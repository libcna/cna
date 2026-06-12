// SPDX-License-Identifier: MS-PL
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

    /// Index buffer storing 16-bit or 32-bit indices.
    class IndexBuffer
    {
    public:
        /// Creates a 16-bit index buffer with capacity for @p indexCount indices.
        IndexBuffer(GraphicsDevice& device, int indexCount);

        /// XNA 4.0: IndexBuffer(GraphicsDevice, IndexElementSize, int, BufferUsage).
        /// Both SixteenBits and ThirtyTwoBits are supported.
        IndexBuffer(GraphicsDevice& device,
                    IndexElementSize indexElementSize,
                    int indexCount,
                    BufferUsage bufferUsage);

        ~IndexBuffer();

        IndexBuffer(const IndexBuffer&) = delete;
        IndexBuffer& operator=(const IndexBuffer&) = delete;

        /// Uploads @p count 16-bit indices (replaces previous content).
        void SetData(const std::uint16_t* indices, int count);

        /// Uploads @p count 32-bit indices (replaces previous content).
        void SetData(const std::uint32_t* indices, int count);

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
