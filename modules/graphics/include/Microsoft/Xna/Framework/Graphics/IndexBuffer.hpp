// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"

namespace CNA::Internal::Renderers
{
    class IIndexBufferRenderer;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Index buffer storing 16-bit or 32-bit indices for indexed draw calls. */
    class IndexBuffer : public GraphicsResource
    {
    public:
        /**
         * @brief Creates a 16-bit index buffer with the given capacity.
         *
         * Uses `IndexElementSize::SixteenBits` and `BufferUsage::None`.
         * Prefer the full constructor when element size or usage must be explicit.
         *
         * @param device     The graphics device.
         * @param indexCount Number of indices the buffer can hold.
         */
        CNAEXT IndexBuffer(GraphicsDevice& device, int indexCount);

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
        CNAEXT ~IndexBuffer() override;

        /** @brief Copying is not allowed. */
        IndexBuffer(const IndexBuffer&) = delete;
        /** @brief Copy-assignment is not allowed. */
        IndexBuffer& operator=(const IndexBuffer&) = delete;
        /** @brief Move-constructs an IndexBuffer, transferring GPU handle ownership. */
        IndexBuffer(IndexBuffer&&) noexcept;
        /** @brief Move-assigns an IndexBuffer, transferring GPU handle ownership. */
        IndexBuffer& operator=(IndexBuffer&&) noexcept;

        /** @brief Returns the fully-qualified .NET type name of this object. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        using GraphicsResource::Dispose;

        /**
         * @brief Returns the usage hint this buffer was created with.
         * @return The BufferUsage value passed to the constructor.
         */
        [[nodiscard]] BufferUsage getBufferUsageProperty() const { return bufferUsage_; }

        /**
         * @brief Returns the element size (16-bit or 32-bit) of indices in this buffer.
         * @return The IndexElementSize value passed to the constructor.
         */
        [[nodiscard]] IndexElementSize getIndexElementSizeProperty() const { return indexElementSize_; }

        /**
         * @brief Returns the number of indices this buffer was created to hold.
         * @return The index capacity of the buffer.
         */
        [[nodiscard]] int getIndexCountProperty() const { return indexCount_; }

        /**
         * @brief Uploads 16-bit index data to the buffer (replaces previous content).
         *
         * A zero @p count is a no-op and permits @p data to be null. A non-zero
         * upload requires a non-null pointer and must fit the logical index capacity.
         *
         * @param data  Pointer to the source index array, or null when @p count is zero.
         * @param count Number of indices to upload.
         */
        void SetData(const std::uint16_t* data, int count);

        /**
         * @brief Uploads a slice of 16-bit index data to the buffer.
         *
         * A zero @p elementCount is a no-op and permits @p data to be null.
         * The caller must provide a source range containing at least
         * `startIndex + elementCount` elements for a non-empty upload.
         *
         * @param data         Pointer to the source index array, or null for an empty range.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of indices to upload.
         */
        void SetData(const std::uint16_t* data, int startIndex, int elementCount);

        /**
         * @brief Reads back 16-bit index data previously uploaded via `SetData`.
         * @param data  Destination array to receive the index data.
         * @param count Number of indices to read.
         */
        void GetData(std::uint16_t* data, int count);

        /**
         * @brief Reads back a slice of 16-bit index data previously uploaded via `SetData`.
         * @param data         Destination array to receive the index data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of indices to read.
         */
        void GetData(std::uint16_t* data, int startIndex, int elementCount);

        /**
         * @brief Uploads 32-bit index data to the buffer (replaces previous content).
         *
         * A zero @p count is a no-op and permits @p data to be null. A non-zero
         * upload requires a non-null pointer and must fit the logical index capacity.
         *
         * @param data  Pointer to the source index array, or null when @p count is zero.
         * @param count Number of indices to upload.
         */
        void SetData(const std::uint32_t* data, int count);

        /**
         * @brief Uploads a slice of 32-bit index data to the buffer.
         *
         * A zero @p elementCount is a no-op and permits @p data to be null.
         * The caller must provide a source range containing at least
         * `startIndex + elementCount` elements for a non-empty upload.
         *
         * @param data         Pointer to the source index array, or null for an empty range.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of indices to upload.
         */
        void SetData(const std::uint32_t* data, int startIndex, int elementCount);

        /**
         * @brief Reads back 32-bit index data previously uploaded via `SetData`.
         * @param data  Destination array to receive the index data.
         * @param count Number of indices to read.
         */
        void GetData(std::uint32_t* data, int count);

        /**
         * @brief Reads back a slice of 32-bit index data previously uploaded via `SetData`.
         * @param data         Destination array to receive the index data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of indices to read.
         */
        void GetData(std::uint32_t* data, int startIndex, int elementCount);

        /**
         * @brief Internal accessor used by the renderer draw paths.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::IIndexBufferRenderer& GetRenderer() const { return *renderer_; }

        /**
         * @brief Returns true while the GPU index buffer handle is allocated.
         *
         * Becomes false immediately after `Dispose()` is called.
         */
        CNAEXT [[nodiscard]] bool HasRenderer() const { return renderer_ != nullptr; }

        /**
         * @brief Uploads indices into a window of this buffer, leaving the rest alone.
         *
         * XNA's `SetData(int offsetInBytes, T[] data, int startIndex, int elementCount)` overload.
         * The `startIndex` the other overloads take indexes the **caller's array**; this offset
         * indexes **this buffer**, which is what lets one slice of a large dynamic index buffer be
         * rewritten per frame instead of the whole thing.
         *
         * **Documented deviation, about cost rather than about result.** The renderer contract
         * underneath replaces a buffer's whole contents, so the window is applied to the CPU shadow
         * and the whole shadow is re-uploaded. The indices end up exactly where XNA puts them; what
         * a caller does not get is a smaller transfer. Anything outside the window keeps whatever
         * it held, and indices never written by any upload read as zero.
         *
         * @param offsetInBytes Byte offset into **this buffer**, a multiple of the element size.
         * @param data          Source indices, of this buffer's own element width.
         * @param startIndex    First element of @p data to read.
         * @param elementCount  Number of indices to write; zero uploads nothing.
         * @throws System::ArgumentException if the width does not match or the offset is unaligned.
         * @throws System::ArgumentOutOfRangeException if the window leaves this buffer's capacity.
         */
        CNAEXT void SetDataAtEXT(int offsetInBytes, const std::uint16_t* data,
                                 int startIndex, int elementCount);

        /** @copydoc SetDataAtEXT(int, const std::uint16_t*, int, int) */
        CNAEXT void SetDataAtEXT(int offsetInBytes, const std::uint32_t* data,
                                 int startIndex, int elementCount);

    protected:
        /**
         * @brief Uploads 16-bit indices with a streaming hint.
         * @param data         Source index array.
         * @param startIndex   First element to read from @p data.
         * @param elementCount Number of indices to upload.
         * @param options      Streaming hint passed to the renderer.
         */
        void SetDataWithOptions(const std::uint16_t* data, int startIndex,
                                int elementCount, SetDataOptions options);

        /**
         * @brief Uploads 32-bit indices with a streaming hint.
         * @param data         Source index array.
         * @param startIndex   First element to read from @p data.
         * @param elementCount Number of indices to upload.
         * @param options      Streaming hint passed to the renderer.
         */
        void SetDataWithOptions(const std::uint32_t* data, int startIndex,
                                int elementCount, SetDataOptions options);

        /**
         * @brief Protected constructor used by DynamicIndexBuffer to pass the dynamic flag.
         *
         * The @p dynamic hint is accepted for XNA API conformance but is currently
         * ignored by all CNA renderers — static and dynamic IBOs use the same GPU path.
         *
         * @param device           The graphics device.
         * @param indexElementSize Element size — SixteenBits or ThirtyTwoBits.
         * @param indexCount       Number of indices the buffer can hold.
         * @param bufferUsage      Usage hint for the buffer.
         * @param dynamic          True when the buffer content will be updated frequently.
         */
        IndexBuffer(GraphicsDevice& device,
                    IndexElementSize indexElementSize,
                    int indexCount,
                    BufferUsage bufferUsage,
                    bool dynamic);

        /** @brief Releases the GPU index buffer handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    private:
        void SetDataAtInternal(int offsetInBytes,
                               const void* data,
                               int startIndex,
                               int elementCount,
                               IndexElementSize dataElementSize);

        void SetDataInternal(const void* data,
                             int startIndex,
                             int elementCount,
                             IndexElementSize dataElementSize,
                             SetDataOptions options,
                             bool useOptions);
        void GetDataInternal(void* data,
                             int startIndex,
                             int elementCount,
                             IndexElementSize dataElementSize);

        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> renderer_;
        IndexElementSize indexElementSize_{IndexElementSize::SixteenBits};
        BufferUsage bufferUsage_{BufferUsage::None};
        int indexCount_{0};
        // Task 930: CPU-side shadow of the most recent SetData call's raw index bytes, enabling
        // GetData() without a real per-renderer GPU readback path (see VertexBuffer's own
        // identical cpuShadow_ precedent for the full rationale).
        std::vector<std::uint8_t> cpuShadow_;
    };
}
