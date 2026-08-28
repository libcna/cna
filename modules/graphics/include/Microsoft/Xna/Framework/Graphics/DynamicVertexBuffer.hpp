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
         */
        DynamicVertexBuffer(GraphicsDevice& device,
                            const VertexDeclaration& vertexDeclaration,
                            int vertexCount,
                            BufferUsage bufferUsage)
            : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage, true)
        {
        }

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
         * Direct2D, Skia). Families that cannot lose one never raise it.
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
         * where the elements are plain `Matrix` values — so there is no packing step and the
         * buffer's `VertexDeclaration` must describe exactly `sizeof(TVertex)` bytes.
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
            static_assert(std::is_trivially_copyable_v<TVertex>,
                          "DynamicVertexBuffer::SetData<T> requires a trivially-copyable vertex type");
            VertexBuffer::SetDataRawAtWithOptions(
                offsetInBytes, data, startIndex, elementCount, vertexStride, options);
        }

    private:
        /** @brief Set by a real renderer-reported device reset; cleared by the next write. */
        bool contentLost_ = false;
    };
}
