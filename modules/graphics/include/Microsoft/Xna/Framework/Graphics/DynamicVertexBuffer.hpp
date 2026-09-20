// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

#include "CNA/Internal/Graphics/IContentLosable.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief A vertex buffer whose content is expected to change frequently. */
    class DynamicVertexBuffer : public VertexBuffer,
            public CNA::Internal::Graphics::IContentLosable
    {
    public:
        /**
         * @brief Constructs a DynamicVertexBuffer with the given declaration, vertex count, and usage hint.
         * @param device            The graphics device.
         * @param vertexDeclaration Layout description for the vertex type.
         * @param vertexCount       Capacity in vertices.
         * @param bufferUsage       Usage hint for the buffer.
         * @throws System::ArgumentOutOfRangeException if @p vertexCount is not positive.
         * @throws System::ObjectDisposedException if @p vertexDeclaration is disposed.
         * @throws System::ArgumentException if a usage index is outside the XNA device range.
         * @throws System::NotSupportedException if the declaration or buffer size exceeds the
         *         active profile.
         */
        DynamicVertexBuffer(GraphicsDevice& device,
                            const VertexDeclaration& vertexDeclaration,
                            int vertexCount,
                            BufferUsage bufferUsage)
            : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage, true)
        {
        }

        /**
         * @brief Creates a dynamic vertex buffer whose layout comes from a vertex structure type.
         *
         * The documented `(GraphicsDevice, Type, Int32, BufferUsage)` constructor. The type is
         * resolved exactly as `VertexBuffer`'s own Type constructor resolves it -- see that
         * constructor for the registry and the refusals -- and the buffer is created dynamic.
         *
         * @param device      The graphics device.
         * @param vertexType  The vertex structure type.
         * @param vertexCount Capacity in vertices.
         * @param bufferUsage Usage hint for the buffer.
         * @throws System::ArgumentException if @p vertexType is not a registered vertex structure.
         * @throws System::ArgumentOutOfRangeException if @p vertexCount is not positive.
         * @throws System::NotSupportedException if the declaration or buffer size exceeds the
         *         active profile.
         */
        DynamicVertexBuffer(GraphicsDevice& device,
                            const System::Type& vertexType,
                            int vertexCount,
                            BufferUsage bufferUsage);

        /**
         * @brief Whether this buffer's contents were lost to a device reset.
         *
         * True from the moment a renderer reports a real device reset until the buffer is written
         * again with `SetData`. Renderers whose API cannot lose a device never set it.
         */
        [[nodiscard]] bool getIsContentLostProperty() const { return contentLost_; }

        /** @brief Marks the content lost and raises ContentLost. */
        CNAEXT void NotifyContentLostEXT() override
        {
            contentLost_ = true;
            ContentLost.Raise(this, System::EventArgs::Empty);
        }

        /** @brief Clears the lost flag; called when the buffer is written with SetData. */
        CNAEXT void ClearContentLostEXT() noexcept override { contentLost_ = false; }

        /**
         * @brief Raised when this vertex buffer's content is lost to a device reset.
         *
         * Raised for real on the renderers whose API can lose a device (DirectX9,
         * Direct2D). Families that cannot lose one never raise it.
         */
        System::EventHandler<System::EventArgs> ContentLost;

        // C++ name lookup stops at the first scope that declares the name, so the overloads below
        // would hide every VertexBuffer::SetData. XNA's DynamicVertexBuffer inherits them all --
        // a game restoring a lost buffer calls the whole-array form on the dynamic object.
        using VertexBuffer::SetData;

        /**
         * @brief Uploads VertexPositionColor vertices with streaming semantics.
         *
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionColor* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads VertexPositionColorTexture vertices with streaming semantics.
         *
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionColorTexture* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads VertexPositionNormalTexture vertices with streaming semantics.
         *
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionNormalTexture* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads VertexPositionTexture vertices with streaming semantics.
         *
         * Most CNA renderers honor @p options as a real GPU mapping hint (buffer orphaning for
         * `Discard`, an unsynchronized write for `NoOverwrite`); a few still ignore it and always
         * behave like `Discard`. Either way the destination write always starts at the buffer's
         * own beginning — @p startIndex only selects where reading from @p data begins.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        void SetData(const VertexPositionTexture* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            VertexBuffer::SetDataWithOptions(data, startIndex, elementCount, options);
        }

        /**
         * @brief Uploads vertices of an application-defined XNA vertex type with streaming semantics.
         *
         * This is the C++ equivalent of XNA's generic
         * `SetData<T>(T[] data, int startIndex, int elementCount, SetDataOptions options)`.
         * A game supplies its own type here — a per-instance transform stream is the usual case,
         * where the elements are plain `Matrix` values — so there is no packing step. As in XNA,
         * the type determines the contiguous transfer span but need not equal the declaration's
         * drawing stride, provided the span fits the buffer's byte capacity.
         *
         * The built-in XNA vertex types keep their dedicated overloads above, which pack the C++
         * object into the compact GPU stream first.
         *
         * @tparam TVertex Application-defined, trivially-copyable vertex type.
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint (Discard / NoOverwrite / None).
         */
        template<typename TVertex>
        void SetData(const TVertex* data,
                     int startIndex,
                     int elementCount,
                     SetDataOptions options)
        {
            static_assert(std::is_trivially_copyable_v<TVertex>,
                          "DynamicVertexBuffer::SetData<T> requires a trivially-copyable vertex type");
            VertexBuffer::SetDataRawWithOptions(
                data, startIndex, elementCount, static_cast<int>(sizeof(TVertex)), options);
        }

        /**
         * @brief Uploads vertices into a window of this buffer, with streaming semantics.
         *
         * XNA's
         * `SetData<T>(int offsetInBytes, T[] data, int startIndex, int elementCount, int vertexStride, SetDataOptions options)`.
         * This is the overload a particle system needs: it writes only the newly created particles,
         * at the position the circular queue has reached, instead of re-sending everything.
         *
         * @p options is accepted for conformance and not forwarded to the driver -- CNA composes a
         * windowed write in a CPU shadow and uploads the buffer whole, which cannot keep a
         * `NoOverwrite` promise. The contents end up correct; only the cost differs from XNA's.
         *
         * @tparam TVertex Application-defined, trivially-copyable vertex type.
         * @param offsetInBytes Byte offset into this buffer, a multiple of @p vertexStride.
         * @param data          Pointer to the source vertex array.
         * @param startIndex    Index of the first element to read from @p data.
         * @param elementCount  Number of vertices to upload.
         * @param vertexStride  Size of one vertex in bytes.
         * @param options       Streaming hint (Discard / NoOverwrite / None).
         */
        template<typename TVertex>
        void SetData(int offsetInBytes,
                     const TVertex* data,
                     int startIndex,
                     int elementCount,
                     int vertexStride,
                     SetDataOptions options)
        {
            if constexpr (std::is_same_v<TVertex, VertexPositionColor> ||
                          std::is_same_v<TVertex, VertexPositionColorTexture> ||
                          std::is_same_v<TVertex, VertexPositionNormalTexture> ||
                          std::is_same_v<TVertex, VertexPositionTexture>)
            {
                VertexBuffer::SetDataAtInternal(
                    offsetInBytes, data, startIndex, elementCount, vertexStride, options, true);
            }
            else
            {
                static_assert(std::is_trivially_copyable_v<TVertex>,
                              "DynamicVertexBuffer::SetData<T> requires a trivially-copyable vertex type");
                VertexBuffer::SetDataElementsAtInternal(
                    offsetInBytes, data, startIndex, elementCount, sizeof(TVertex),
                    vertexStride, options, true);
            }
        }

        /**
         * @brief Uploads raw vertex bytes with a streaming hint, for a stride known only at run time.
         *
         * The streaming counterpart of VertexBuffer::SetDataRaw(), and it exists for the same
         * reason: a caller-defined layout whose stride is data rather than a C++ type's size
         * cannot instantiate `SetData<TVertex>`, because that overload derives the transfer span
         * from `sizeof(TVertex)`. The contract is the raw one — exactly `count * stride`
         * contiguous bytes read from @p data — not the generic overload's tightly-packed elements
         * written at @p stride spacing.
         *
         * @param data    Pointer to the raw vertex data; at least `count * stride` readable bytes.
         *                May be null only when @p count is zero, which uploads nothing.
         * @param count   Number of vertices.
         * @param stride  Size of one vertex in bytes.
         * @param options Streaming hint (Discard / NoOverwrite / None).
         */
        CNAEXT void SetDataRawWithOptionsEXT(
            const void* data, int count, int stride, SetDataOptions options)
        {
            VertexBuffer::SetDataRawWithOptions(data, 0, count, stride, options);
        }

        /**
         * @brief Writes raw vertex bytes into a window of this buffer, with a streaming hint.
         *
         * SetDataRawWithOptionsEXT() windowed, the way VertexBuffer::SetDataRawAtEXT() windows
         * VertexBuffer::SetDataRaw(). Anything outside the window keeps whatever it held, and
         * @p offsetInBytes must land on a vertex boundary.
         *
         * @p options is accepted for conformance and not forwarded: a windowed write is composed
         * in the CPU shadow and uploaded whole, which cannot keep a `NoOverwrite` promise.
         *
         * @param offsetInBytes Byte offset into **this buffer**, a multiple of @p stride.
         * @param data    Pointer to the raw vertex data; at least `count * stride` readable bytes.
         *                May be null only when @p count is zero, which uploads nothing.
         * @param count   Number of vertices to write.
         * @param stride  Size of one vertex in bytes.
         * @param options Streaming hint; see above.
         */
        CNAEXT void SetDataRawAtWithOptionsEXT(
            int offsetInBytes, const void* data, int count, int stride, SetDataOptions options)
        {
            VertexBuffer::SetDataRawAtWithOptions(offsetInBytes, data, 0, count, stride, options);
        }

    private:
        /** @brief Set by a real renderer-reported device reset; cleared by the next write. */
        bool contentLost_ = false;
    };
}
