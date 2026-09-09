// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics { class GraphicsDevice; }
namespace CNA::Internal::Renderers { class IStorageBufferRenderer; }

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /** @brief Declares every GPU role for which a storage buffer is created. */
    enum class StorageBufferUsage : std::uint32_t
    {
        /** @brief No GPU role is declared. This value is not valid by itself. */
        None = 0,
        /** @brief Compute or graphics shaders may access the buffer as storage. */
        Storage = UINT32_C(1) << 0,
        /** @brief The buffer may be the source of a GPU-side copy. */
        TransferSource = UINT32_C(1) << 1,
        /** @brief The buffer may be the destination of a GPU-side copy. */
        TransferDestination = UINT32_C(1) << 2,
        /** @brief The buffer may supply indirect draw arguments. */
        IndirectArguments = UINT32_C(1) << 3,
        /** @brief The buffer may be consumed as vertex data by a compatible extension path. */
        Vertex = UINT32_C(1) << 4,
        /** @brief The buffer may be consumed as index data by a compatible extension path. */
        Index = UINT32_C(1) << 5
    };

    /**
     * @brief Combines two storage-buffer usage flags.
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The combined mask.
     */
    [[nodiscard]] constexpr StorageBufferUsage operator|(
        StorageBufferUsage left, StorageBufferUsage right) noexcept
    {
        return static_cast<StorageBufferUsage>(static_cast<std::uint32_t>(left) |
                                               static_cast<std::uint32_t>(right));
    }

    /**
     * @brief Intersects two storage-buffer usage masks.
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The flags present in both masks.
     */
    [[nodiscard]] constexpr StorageBufferUsage operator&(
        StorageBufferUsage left, StorageBufferUsage right) noexcept
    {
        return static_cast<StorageBufferUsage>(static_cast<std::uint32_t>(left) &
                                               static_cast<std::uint32_t>(right));
    }

    /** @brief Declares which direct CPU transfer operations a storage buffer accepts. */
    enum class StorageBufferCpuAccess : std::uint32_t
    {
        /** @brief Direct CPU upload and readback are both refused. */
        None = 0,
        /** @brief CPU readback through @ref StorageBuffer::getBytes is allowed. */
        Read = UINT32_C(1) << 0,
        /** @brief CPU upload through @ref StorageBuffer::setBytes is allowed. */
        Write = UINT32_C(1) << 1
    };

    /**
     * @brief Combines two storage-buffer CPU-access flags.
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The combined mask.
     */
    [[nodiscard]] constexpr StorageBufferCpuAccess operator|(
        StorageBufferCpuAccess left, StorageBufferCpuAccess right) noexcept
    {
        return static_cast<StorageBufferCpuAccess>(static_cast<std::uint32_t>(left) |
                                                   static_cast<std::uint32_t>(right));
    }

    /**
     * @brief Intersects two storage-buffer CPU-access masks.
     * @param left First flag or mask.
     * @param right Second flag or mask.
     * @return The flags present in both masks.
     */
    [[nodiscard]] constexpr StorageBufferCpuAccess operator&(
        StorageBufferCpuAccess left, StorageBufferCpuAccess right) noexcept
    {
        return static_cast<StorageBufferCpuAccess>(static_cast<std::uint32_t>(left) &
                                                   static_cast<std::uint32_t>(right));
    }

    /** @brief Immutable size, GPU usage and CPU-access description for a storage buffer. */
    class StorageBufferDescriptor final
    {
    public:
        /**
         * @brief Creates and intrinsically validates a storage-buffer description.
         * @param byteSize Positive allocation size in bytes.
         * @param usage Non-empty set of declared GPU roles.
         * @param cpuAccess Declared direct CPU read/write operations.
         * @throws std::invalid_argument If the size, usage or CPU-access mask is invalid.
         */
        StorageBufferDescriptor(
            std::size_t byteSize, StorageBufferUsage usage,
            StorageBufferCpuAccess cpuAccess);

        /**
         * @brief Returns the immutable allocation size.
         * @return Positive size in bytes.
         */
        [[nodiscard]] std::size_t getByteSize() const noexcept;

        /**
         * @brief Returns the immutable GPU-role mask.
         * @return Usage supplied at construction.
         */
        [[nodiscard]] StorageBufferUsage getUsage() const noexcept;

        /**
         * @brief Returns the immutable direct CPU-access mask.
         * @return CPU access supplied at construction.
         */
        [[nodiscard]] StorageBufferCpuAccess getCpuAccess() const noexcept;

    private:
        std::size_t byteSize_;
        StorageBufferUsage usage_;
        StorageBufferCpuAccess cpuAccess_;
    };

    /**
     * @brief A tracked GPU byte buffer with immutable usage and CPU-access intent.
     *
     * The renderer chooses native memory and synchronization from the descriptor. GPU-only
     * buffers expose no mapping or native handle: callers initialize them by copying from a
     * CPU-writable transfer source and read them through a CPU-readable transfer destination.
     */
    class StorageBuffer final
        : public Microsoft::Xna::Framework::Graphics::GraphicsResource
    {
    public:
        /** @brief Makes the inherited parameterless disposal operation publicly visible. */
        using Microsoft::Xna::Framework::Graphics::GraphicsResource::Dispose;

        /**
         * @brief Allocates a buffer with the legacy compatible usage and CPU access.
         *
         * The compatible default declares storage, both transfer roles, indirect arguments and
         * CPU read/write access, preserving every operation accepted before descriptors existed.
         *
         * @param device The device to allocate on.
         * @param byteSize The size in bytes; must be positive.
         * @throws std::invalid_argument If @p byteSize is zero.
         * @throws System::NotSupportedException If the renderer has no compute support, naming it.
         */
        StorageBuffer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                      std::size_t byteSize);

        /**
         * @brief Allocates a buffer from an immutable descriptor.
         * @param device The device to allocate on.
         * @param descriptor Size, GPU roles and direct CPU access.
         * @throws System::ObjectDisposedException If @p device is disposed.
         * @throws System::NotSupportedException If compute or the requested descriptor is not
         *         implemented by the renderer, or a known device limit is exceeded.
         */
        StorageBuffer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                      const StorageBufferDescriptor& descriptor);

        /** @brief Releases the renderer record before its owning device is destroyed. */
        CNAEXT ~StorageBuffer() override;

        /** @brief Copy construction is disabled because a tracked resource has one identity. */
        StorageBuffer(const StorageBuffer&) = delete;

        /** @brief Copy assignment is disabled because a tracked resource has one identity. */
        StorageBuffer& operator=(const StorageBuffer&) = delete;

        /** @brief Move construction is disabled because device tracking stores this object's address. */
        StorageBuffer(StorageBuffer&&) = delete;

        /** @brief Move assignment is disabled because device tracking stores this object's address. */
        StorageBuffer& operator=(StorageBuffer&&) = delete;

        /**
         * @brief Uploads bytes into the buffer from its beginning.
         * @param data Source bytes, or null only for an empty upload.
         * @param byteSize Number of bytes to upload.
         */
        void setBytes(const void* data, std::size_t byteSize);

        /**
         * @brief Uploads bytes into an exact buffer range.
         * @param byteOffset First destination byte.
         * @param data Source bytes, or null only when @p byteSize is zero.
         * @param byteSize Number of bytes to upload.
         * @throws System::NotSupportedException If CPU write access was not declared or the
         *         renderer refuses the complete transfer.
         * @throws std::invalid_argument If the pointer or range is invalid.
         */
        void setBytes(std::size_t byteOffset, const void* data, std::size_t byteSize);

        /**
         * @brief Reads bytes from the beginning of the buffer.
         * @param out Destination bytes, or null only for an empty readback.
         * @param byteSize Number of bytes to read.
         */
        void getBytes(void* out, std::size_t byteSize) const;

        /**
         * @brief Reads bytes from an exact buffer range.
         * @param byteOffset First source byte.
         * @param out Destination bytes, or null only when @p byteSize is zero.
         * @param byteSize Number of bytes to read.
         * @throws System::NotSupportedException If CPU read access was not declared or the
         *         renderer refuses the complete transfer.
         * @throws std::invalid_argument If the pointer or range is invalid.
         */
        void getBytes(std::size_t byteOffset, void* out, std::size_t byteSize) const;

        /**
         * @brief Copies an exact byte range to another buffer on the same device.
         * @param destination Destination buffer.
         * @param sourceByteOffset First source byte.
         * @param destinationByteOffset First destination byte.
         * @param byteSize Number of bytes to copy.
         * @throws System::NotSupportedException If transfer-source/destination usage was not
         *         declared or the renderer refuses the complete GPU-side copy.
         * @throws std::invalid_argument If devices or ranges are incompatible, including an
         *         overlapping same-buffer copy.
         */
        void copyTo(StorageBuffer& destination, std::size_t sourceByteOffset,
                    std::size_t destinationByteOffset, std::size_t byteSize) const;

        /**
         * @brief Returns the immutable creation description.
         * @return Description retained by this resource.
         * @throws System::ObjectDisposedException If the resource is disposed.
         */
        [[nodiscard]] const StorageBufferDescriptor& getDescriptor() const;

        /**
         * @brief Returns the buffer size in bytes.
         * @return Positive allocated byte count.
         * @throws System::ObjectDisposedException If the resource is disposed.
         */
        [[nodiscard]] std::size_t getByteSize() const;

        /**
         * @brief Returns the renderer-side record for existing graphics-device extension routes.
         * @return Renderer object; never null for a live buffer.
         * @throws System::ObjectDisposedException If the resource is disposed.
         */
        [[nodiscard]] CNA::Internal::Renderers::IStorageBufferRenderer* getRendererEXT() const;

        /**
         * @brief Returns the fully qualified CNA type name.
         * @return `"CNA.Graphics.StorageBuffer"`.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /**
         * @brief Releases the internal renderer record before base-class disposal.
         * @param disposing True for explicit/device disposal; false during destruction.
         */
        void Dispose(bool disposing) override;

    private:
        struct Prepared
        {
            StorageBufferDescriptor descriptor;
            std::shared_ptr<CNA::Internal::Renderers::IStorageBufferRenderer> renderer;
        };

        static Prepared prepareLegacy(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            std::size_t byteSize);
        static Prepared prepare(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const StorageBufferDescriptor& descriptor);

        StorageBuffer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                      Prepared prepared);

        StorageBufferDescriptor descriptor_;
        std::shared_ptr<CNA::Internal::Renderers::IStorageBufferRenderer> renderer_;
    };

    /**
     * @brief A typed view over a @ref StorageBuffer.
     * @tparam T Trivially-copyable element type whose bytes reach the GPU unchanged.
     */
    template<typename T>
    class StorageBufferT
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "a storage buffer's element type reaches the GPU as bytes, so it must be "
                      "trivially copyable");

    public:
        /**
         * @brief Allocates a legacy-compatible buffer of @p elementCount elements.
         * @param device Device that owns the buffer.
         * @param elementCount Positive element count.
         */
        StorageBufferT(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                       const std::size_t elementCount)
            : buffer_(device, checkedByteSize(elementCount)), elementCount_(elementCount)
        {
        }

        /**
         * @brief Allocates a typed buffer with explicit usage and CPU access.
         * @param device Device that owns the buffer.
         * @param elementCount Positive element count.
         * @param usage Immutable GPU-role mask.
         * @param cpuAccess Immutable direct CPU-access mask.
         */
        StorageBufferT(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                       const std::size_t elementCount, const StorageBufferUsage usage,
                       const StorageBufferCpuAccess cpuAccess)
            : buffer_(device, StorageBufferDescriptor(
                                  checkedByteSize(elementCount), usage, cpuAccess)),
              elementCount_(elementCount)
        {
        }

        /**
         * @brief Uploads a whole vector of elements.
         * @param data Elements; must not be longer than the buffer.
         * @throws std::invalid_argument If the vector is too long.
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
         * @return Elements in buffer order.
         */
        [[nodiscard]] std::vector<T> getData() const
        {
            std::vector<T> data(elementCount_);
            buffer_.getBytes(data.data(), elementCount_ * sizeof(T));
            return data;
        }

        /**
         * @brief Returns how many elements the buffer holds.
         * @return Positive element count.
         */
        [[nodiscard]] std::size_t getElementCount() const { return elementCount_; }

        /**
         * @brief Returns the mutable untyped buffer.
         * @return Underlying tracked byte buffer.
         */
        [[nodiscard]] StorageBuffer& getBuffer() { return buffer_; }

        /**
         * @brief Returns the untyped buffer.
         * @return Underlying tracked byte buffer.
         */
        [[nodiscard]] const StorageBuffer& getBuffer() const { return buffer_; }

    private:
        [[nodiscard]] static std::size_t checkedByteSize(const std::size_t elementCount)
        {
            if (elementCount > static_cast<std::size_t>(-1) / sizeof(T))
                throw std::invalid_argument(
                    "CNA::Graphics::StorageBufferT: element byte size overflows size_t");
            return elementCount * sizeof(T);
        }

        StorageBuffer buffer_;
        std::size_t elementCount_ = 0;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
