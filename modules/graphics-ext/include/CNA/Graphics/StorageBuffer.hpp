// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; }
namespace CNA::Internal::Renderers { class IStorageBufferRenderer; }

namespace CNA::Graphics {

    /**
     * @brief A GPU buffer a compute shader reads and writes.
     *
     * plan_modern.md `MOD-1520`. Byte-oriented and non-template on purpose: a storage buffer holds
     * whatever its shader says it holds, and the whole implementation lives in one `.cpp` rather
     * than being instantiated into every translation unit that names an element type.
     * @ref StorageBufferT is the typed view over this, and is the only part that has to be a
     * template.
     */
    class StorageBuffer
    {
    public:
        /**
         * @brief Allocates a buffer of @p byteSize bytes.
         *
         * @param device   The device to allocate on.
         * @param byteSize The size in bytes; must be positive.
         * @throws std::invalid_argument If @p byteSize is zero.
         * @throws System::NotSupportedException If the renderer has no compute support, naming it.
         */
        StorageBuffer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                      std::size_t byteSize);

        /** @brief Releases the buffer. */
        ~StorageBuffer();

        StorageBuffer(const StorageBuffer&)            = delete;
        StorageBuffer& operator=(const StorageBuffer&) = delete;

        /**
         * @brief Uploads bytes into the buffer, from its beginning.
         *
         * @param data     The source bytes.
         * @param byteSize How many to upload; more than the buffer holds is refused rather than
         *                 silently truncated, because a truncated upload is a wrong answer that
         *                 looks like a right one.
         * @throws std::invalid_argument If @p data is null or @p byteSize exceeds the capacity.
         */
        void setBytes(const void* data, std::size_t byteSize);

        /**
         * @brief Reads bytes back out of the buffer, from its beginning.
         *
         * @param out      Receives the bytes.
         * @param byteSize How many to read.
         * @throws std::invalid_argument If @p out is null or @p byteSize exceeds the capacity.
         */
        void getBytes(void* out, std::size_t byteSize) const;

        /** @brief Returns the buffer's size in bytes. */
        [[nodiscard]] std::size_t getByteSize() const;

        /**
         * @brief Returns the renderer-side buffer, for the compute shader that binds it.
         *
         * @return The renderer object; never null for a constructed buffer.
         */
        [[nodiscard]] CNA::Internal::Renderers::IStorageBufferRenderer* getRendererEXT() const;

    private:
        std::unique_ptr<CNA::Internal::Renderers::IStorageBufferRenderer> renderer_;
        std::size_t byteSize_ = 0;
    };

    /**
     * @brief A typed view over a @ref StorageBuffer.
     *
     * plan_modern.md `MOD-1520`/`C7`. Header-only because it is a template and nothing else; every
     * byte of behaviour it has is the non-template buffer's.
     *
     * @tparam T The element type; must be trivially copyable, since the GPU sees only its bytes.
     */
    template<typename T>
    class StorageBufferT
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "a storage buffer's element type reaches the GPU as bytes, so it must be "
                      "trivially copyable");

    public:
        /**
         * @brief Allocates a buffer of @p elementCount elements.
         *
         * @param device       The device to allocate on.
         * @param elementCount How many elements; must be positive.
         */
        StorageBufferT(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                       const std::size_t elementCount)
            : buffer_(device, elementCount * sizeof(T)), elementCount_(elementCount)
        {
        }

        /**
         * @brief Uploads a whole vector of elements.
         *
         * @param data The elements; must not be longer than the buffer.
         * @throws std::invalid_argument If it is.
         */
        void setData(const std::vector<T>& data)
        {
            if (data.size() > elementCount_)
                throw std::invalid_argument(
                    "CNA::Graphics::StorageBufferT::setData: more elements than the buffer holds");
            buffer_.setBytes(data.data(), data.size() * sizeof(T));
        }

        /**
         * @brief Reads every element back.
         *
         * @return The elements, in buffer order.
         */
        [[nodiscard]] std::vector<T> getData() const
        {
            std::vector<T> data(elementCount_);
            buffer_.getBytes(data.data(), elementCount_ * sizeof(T));
            return data;
        }

        /** @brief Returns how many elements the buffer holds. */
        [[nodiscard]] std::size_t getElementCount() const { return elementCount_; }

        /** @brief Returns the untyped buffer, which is what a compute shader binds. */
        [[nodiscard]] StorageBuffer& getBuffer() { return buffer_; }

        /** @brief Returns the untyped buffer. */
        [[nodiscard]] const StorageBuffer& getBuffer() const { return buffer_; }

    private:
        StorageBuffer buffer_;
        std::size_t elementCount_ = 0;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
