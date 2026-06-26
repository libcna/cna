// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"

namespace CNA::Internal::Backends
{
    class IIndexBufferBackend;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Index buffer storing 16-bit or 32-bit indices for indexed draw calls. */
    class IndexBuffer : public GraphicsResource
    {
    public:
        /**
         * @brief Creates a 16-bit index buffer with the given capacity.
         * @param device     The graphics device.
         * @param indexCount Number of indices the buffer can hold.
         */
        IndexBuffer(GraphicsDevice& device, int indexCount);

        /**
         * @brief Creates an index buffer with explicit element size and usage hint.
         * @param device           The graphics device.
         * @param indexElementSize Element size — SixteenBits or ThirtyTwoBits.
         * @param indexCount       Number of indices the buffer can hold.
         * @param bufferUsage      Usage hint for the buffer.
         */
        IndexBuffer(GraphicsDevice& device,
                    IndexElementSize indexElementSize,
                    int indexCount,
                    BufferUsage bufferUsage);

        /** @brief Destructor. */
        NOXNA ~IndexBuffer() override;

        /** @brief Copying is not allowed. */
        IndexBuffer(const IndexBuffer&) = delete;
        /** @brief Copy-assignment is not allowed. */
        IndexBuffer& operator=(const IndexBuffer&) = delete;

        /** @brief Returns the fully-qualified .NET type name of this object. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Uploads 16-bit index data to the buffer (replaces previous content).
         * @param indices Pointer to the source index array.
         * @param count   Number of indices to upload.
         */
        void SetData(const std::uint16_t* indices, int count);

        /**
         * @brief Uploads 32-bit index data to the buffer (replaces previous content).
         * @param indices Pointer to the source index array.
         * @param count   Number of indices to upload.
         */
        void SetData(const std::uint32_t* indices, int count);

        /**
         * @brief Returns the number of indices this buffer was created to hold.
         * @return The index capacity of the buffer.
         */
        [[nodiscard]] int getIndexCountProperty() const;

        /**
         * @brief Internal accessor used by the backend draw paths.
         */
        NOXNA [[nodiscard]] CNA::Internal::Backends::IIndexBufferBackend& GetBackend() const { return *backend_; }

    private:
        std::unique_ptr<CNA::Internal::Backends::IIndexBufferBackend> backend_;
    };
}
