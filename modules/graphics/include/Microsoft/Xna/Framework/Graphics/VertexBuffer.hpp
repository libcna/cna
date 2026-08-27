// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

namespace CNA::Internal::Renderers
{
    class IVertexBufferRenderer;
}

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief GPU vertex buffer for storing vertex data.
     *
     * CNA's current typed and raw overloads upload at destination byte offset zero. A validated
     * zero-element upload is a no-op and may use a null source pointer; a real upload requires a
     * non-null source and must fit the buffer's logical vertex capacity. Native allocation
     * padding never changes that public capacity.
     */
    class VertexBuffer : public GraphicsResource
    {
    public:
        /**
         * @brief Creates an empty vertex buffer with capacity for @p vertexCount vertices.
         *
         * Uses a default (empty) VertexDeclaration and `BufferUsage::None`.
         * Prefer the full constructor when vertex layout metadata is needed.
         *
         * @param device      Owning graphics device.
         * @param vertexCount Number of vertices the buffer can hold.
         */
        CNAEXT VertexBuffer(GraphicsDevice& device, int vertexCount);

        /**
         * @brief Constructs a vertex buffer from a vertex declaration.
         *
         * Mirrors `VertexBuffer(GraphicsDevice, VertexDeclaration, int, BufferUsage)`.
         *
         * @param device            Owning graphics device.
         * @param vertexDeclaration Vertex layout description.
         * @param vertexCount       Number of vertices the buffer can hold.
         * @param bufferUsage       Usage hint.
         */
        VertexBuffer(GraphicsDevice& device,
                     const VertexDeclaration& vertexDeclaration,
                     int vertexCount,
                     BufferUsage bufferUsage);

        /** @brief Destructor. */
        CNAEXT ~VertexBuffer() override;

        /** @brief Copying is not allowed. */
        VertexBuffer(const VertexBuffer&) = delete;
        /** @brief Copy-assignment is not allowed. */
        VertexBuffer& operator=(const VertexBuffer&) = delete;
        /** @brief Move-constructs a VertexBuffer, transferring GPU handle ownership. */
        VertexBuffer(VertexBuffer&&) noexcept;
        /** @brief Move-assigns a VertexBuffer, transferring GPU handle ownership. */
        VertexBuffer& operator=(VertexBuffer&&) noexcept;

        /** @brief Returns the fully-qualified .NET type name of this object. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        using GraphicsResource::Dispose;

        /**
         * @brief Returns the usage hint this buffer was created with.
         * @return The BufferUsage value passed to the constructor.
         */
        [[nodiscard]] BufferUsage getBufferUsageProperty() const { return bufferUsage_; }

        /**
         * @brief Returns the vertex declaration describing the layout of each vertex.
         * @return Const reference to the stored VertexDeclaration.
         */
        [[nodiscard]] const VertexDeclaration& getVertexDeclarationProperty() const { return vertexDeclaration_; }

        /**
         * @brief Returns the number of vertices this buffer was created to hold.
         * @return The vertex capacity of the buffer.
         */
        [[nodiscard]] int getVertexCountProperty() const { return vertexCount_; }

        /**
         * @brief Uploads VertexPositionColor vertex data to the GPU buffer.
         * @param data  Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        void SetData(const VertexPositionColor* data, int count);

        /**
         * @brief Uploads a slice of VertexPositionColor vertex data to the GPU buffer.
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         */
        void SetData(const VertexPositionColor* data, int startIndex, int elementCount);

        /**
         * @brief Reads back VertexPositionColor vertex data previously uploaded via `SetData`.
         * @param data  Destination array to receive the vertex data.
         * @param count Number of vertices to read.
         */
        void GetData(VertexPositionColor* data, int count);

        /**
         * @brief Reads back a slice of VertexPositionColor vertex data previously uploaded via `SetData`.
         * @param data         Destination array to receive the vertex data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of vertices to read.
         */
        void GetData(VertexPositionColor* data, int startIndex, int elementCount);

        /**
         * @brief Uploads VertexPositionColorTexture vertex data to the GPU buffer.
         * @param data  Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        void SetData(const VertexPositionColorTexture* data, int count);

        /**
         * @brief Uploads a slice of VertexPositionColorTexture vertex data to the GPU buffer.
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         */
        void SetData(const VertexPositionColorTexture* data, int startIndex, int elementCount);

        /**
         * @brief Reads back VertexPositionColorTexture vertex data previously uploaded via `SetData`.
         * @param data  Destination array to receive the vertex data.
         * @param count Number of vertices to read.
         */
        void GetData(VertexPositionColorTexture* data, int count);

        /**
         * @brief Reads back a slice of VertexPositionColorTexture vertex data previously uploaded via `SetData`.
         * @param data         Destination array to receive the vertex data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of vertices to read.
         */
        void GetData(VertexPositionColorTexture* data, int startIndex, int elementCount);

        /**
         * @brief Uploads VertexPositionNormalTexture vertex data to the GPU buffer.
         * @param data  Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        void SetData(const VertexPositionNormalTexture* data, int count);

        /**
         * @brief Uploads a slice of VertexPositionNormalTexture vertex data to the GPU buffer.
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         */
        void SetData(const VertexPositionNormalTexture* data, int startIndex, int elementCount);

        /**
         * @brief Reads back VertexPositionNormalTexture vertex data previously uploaded via `SetData`.
         * @param data  Destination array to receive the vertex data.
         * @param count Number of vertices to read.
         */
        void GetData(VertexPositionNormalTexture* data, int count);

        /**
         * @brief Reads back a slice of VertexPositionNormalTexture vertex data previously uploaded via `SetData`.
         * @param data         Destination array to receive the vertex data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of vertices to read.
         */
        void GetData(VertexPositionNormalTexture* data, int startIndex, int elementCount);

        /**
         * @brief Uploads VertexPositionTexture vertex data to the GPU buffer.
         * @param data  Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        void SetData(const VertexPositionTexture* data, int count);

        /**
         * @brief Uploads a slice of VertexPositionTexture vertex data to the GPU buffer.
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         */
        void SetData(const VertexPositionTexture* data, int startIndex, int elementCount);

        /**
         * @brief Reads back VertexPositionTexture vertex data previously uploaded via `SetData`.
         * @param data  Destination array to receive the vertex data.
         * @param count Number of vertices to read.
         */
        void GetData(VertexPositionTexture* data, int count);

        /**
         * @brief Reads back a slice of VertexPositionTexture vertex data previously uploaded via `SetData`.
         * @param data         Destination array to receive the vertex data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of vertices to read.
         */
        void GetData(VertexPositionTexture* data, int startIndex, int elementCount);

        /**
         * @brief Uploads VertexPositionNormalTextureSkinned vertex data to the GPU buffer.
         *
         * CNAEXT overload for the GPU-skinned vertex type.
         *
         * @param data  Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        CNAEXT void SetData(const VertexPositionNormalTextureSkinned* data, int count);

        /**
         * @brief Uploads a slice of VertexPositionNormalTextureSkinned vertex data to the GPU buffer.
         *
         * CNAEXT overload for the GPU-skinned vertex type.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         */
        CNAEXT void SetData(const VertexPositionNormalTextureSkinned* data, int startIndex, int elementCount);

        /**
         * @brief Reads back VertexPositionNormalTextureSkinned vertex data previously uploaded via `SetData`.
         *
         * CNAEXT overload for the GPU-skinned vertex type.
         *
         * @param data  Destination array to receive the vertex data.
         * @param count Number of vertices to read.
         */
        CNAEXT void GetData(VertexPositionNormalTextureSkinned* data, int count);

        /**
         * @brief Reads back a slice of VertexPositionNormalTextureSkinned vertex data previously uploaded via `SetData`.
         *
         * CNAEXT overload for the GPU-skinned vertex type.
         *
         * @param data         Destination array to receive the vertex data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of vertices to read.
         */
        CNAEXT void GetData(VertexPositionNormalTextureSkinned* data, int startIndex, int elementCount);

        /**
         * @brief Uploads VertexPositionNormalTangentTexture vertex data to the GPU buffer.
         *
         * CNAEXT overload for PbrEffect's tangent-space vertex type.
         *
         * @param data  Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        CNAEXT void SetData(const VertexPositionNormalTangentTexture* data, int count);

        /**
         * @brief Uploads a slice of VertexPositionNormalTangentTexture vertex data to the GPU buffer.
         *
         * CNAEXT overload for PbrEffect's tangent-space vertex type.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         */
        CNAEXT void SetData(const VertexPositionNormalTangentTexture* data, int startIndex, int elementCount);

        /**
         * @brief Reads back VertexPositionNormalTangentTexture vertex data previously uploaded via `SetData`.
         *
         * CNAEXT overload for PbrEffect's tangent-space vertex type.
         *
         * @param data  Destination array to receive the vertex data.
         * @param count Number of vertices to read.
         */
        CNAEXT void GetData(VertexPositionNormalTangentTexture* data, int count);

        /**
         * @brief Reads back a slice of VertexPositionNormalTangentTexture vertex data previously uploaded via `SetData`.
         *
         * CNAEXT overload for PbrEffect's tangent-space vertex type.
         *
         * @param data         Destination array to receive the vertex data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of vertices to read.
         */
        CNAEXT void GetData(VertexPositionNormalTangentTexture* data, int startIndex, int elementCount);

        /**
         * @brief Uploads VertexPositionNormalTangentTextureSkinned vertex data to the GPU buffer.
         *
         * CNAEXT overload for SkinnedPbrEffect's tangent-space, GPU-skinned vertex type.
         *
         * @param data  Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        CNAEXT void SetData(const VertexPositionNormalTangentTextureSkinned* data, int count);

        /**
         * @brief Uploads a slice of VertexPositionNormalTangentTextureSkinned vertex data to the GPU buffer.
         *
         * CNAEXT overload for SkinnedPbrEffect's tangent-space, GPU-skinned vertex type.
         *
         * @param data         Pointer to the source vertex array.
         * @param startIndex   Index of the first element to read from @p data.
         * @param elementCount Number of vertices to upload.
         */
        CNAEXT void SetData(const VertexPositionNormalTangentTextureSkinned* data, int startIndex, int elementCount);

        /**
         * @brief Reads back VertexPositionNormalTangentTextureSkinned vertex data previously uploaded via `SetData`.
         *
         * CNAEXT overload for SkinnedPbrEffect's tangent-space, GPU-skinned vertex type.
         *
         * @param data  Destination array to receive the vertex data.
         * @param count Number of vertices to read.
         */
        CNAEXT void GetData(VertexPositionNormalTangentTextureSkinned* data, int count);

        /**
         * @brief Reads back a slice of VertexPositionNormalTangentTextureSkinned vertex data previously uploaded via `SetData`.
         *
         * CNAEXT overload for SkinnedPbrEffect's tangent-space, GPU-skinned vertex type.
         *
         * @param data         Destination array to receive the vertex data.
         * @param startIndex   Index of the first element to write in @p data.
         * @param elementCount Number of vertices to read.
         */
        CNAEXT void GetData(VertexPositionNormalTangentTextureSkinned* data, int startIndex, int elementCount);

        /**
         * @brief Uploads vertices of an application-defined XNA vertex type.
         *
         * This is the C++ equivalent of XNA's generic `SetData<T>(T[])` overload. The buffer's
         * `VertexDeclaration` defines how the bytes are interpreted by the graphics device, and
         * the C++ vertex type must therefore have the same stride and a directly copyable layout.
         * Built-in XNA vertex types continue to use their dedicated packing overloads.
         *
         * @tparam TVertex Application-defined, trivially-copyable vertex type.
         * @param data Pointer to the source vertex array.
         * @param count Number of vertices to upload.
         */
        template<typename TVertex>
        void SetData(const TVertex* data, int count)
        {
            static_assert(std::is_trivially_copyable_v<TVertex>,
                          "VertexBuffer::SetData<T> requires a trivially-copyable vertex type");
            SetDataRaw(data, count, static_cast<int>(sizeof(TVertex)));
        }

        /**
         * @brief Uploads raw vertex data with an explicit per-vertex byte stride.
         *
         * Use this overload when uploading GPU-compact vertex layouts that have no
         * corresponding typed XNA vertex struct (e.g. the 52-byte skinned layout).
         *
         * The contract is exactly `count * stride` bytes read from @p data, and **the source
         * extent is the caller's promise**: this buffer's own capacity is in vertices, so a
         * stride wider than the data behind @p data cannot be detected here. Compute the two from
         * one place — the morph re-upload path takes both from the same `MeshOut` for that reason.
         * A `count` above the buffer's capacity, a null @p data, a negative `count` and a zero
         * `stride` are all rejected; a `count` of zero uploads nothing.
         *
         * When the buffer carries a `VertexDeclaration`, @p stride must equal its own stride and
         * every declared element must fit inside it.
         *
         * @param data   Pointer to the raw vertex data; at least `count * stride` readable bytes.
         * @param count  Number of vertices.
         * @param stride Size of one vertex in bytes.
         */
        CNAEXT void SetDataRaw(const void* data, int count, int stride);

        /**
         * @brief Uploads raw vertex bytes into a window of this buffer, leaving the rest alone.
         *
         * XNA's `SetData(int offsetInBytes, T[] data, int startIndex, int elementCount, int
         * vertexStride)` overload, in the raw form SetDataRaw() already speaks. It is what a
         * particle system or a streaming terrain needs: rewrite one slice of a large dynamic
         * buffer per frame instead of the whole thing.
         *
         * **Documented deviation, and it is about cost rather than about result.** The renderer
         * contract this buffer sits on replaces a buffer's whole contents -- `SetData(data, count,
         * stride)` has no destination offset, in any of the renderer families -- so the window is
         * applied to the CPU shadow and the whole shadow is then uploaded. The bytes end up
         * exactly where XNA puts them; what a caller does not get is a smaller transfer. Anything
         * outside the window keeps whatever it held, which is the property the overload exists for
         * and the one a full re-upload would otherwise destroy.
         *
         * Bytes never written by any upload read as zero, exactly as a freshly created buffer's do.
         *
         * @param offsetInBytes Byte offset into **this buffer**, a multiple of @p stride.
         * @param data   Pointer to the raw vertex data; at least `count * stride` readable bytes.
         *               May be null only when @p count is zero, which uploads nothing.
         * @param count  Number of vertices to write.
         * @param stride Size of one vertex in bytes; must match the declaration's own when this
         *               buffer carries one.
         * @throws System::ArgumentOutOfRangeException if the window leaves this buffer's capacity.
         * @throws System::ArgumentException if @p stride or @p offsetInBytes is unusable.
         */
        CNAEXT void SetDataRawAtEXT(int offsetInBytes, const void* data, int count, int stride);

        /**
         * @brief Reads raw vertex bytes back from a window of this buffer.
         *
         * The counterpart of SetDataRaw()/SetDataRawAtEXT(), and the reason it exists: the typed
         * GetData() overloads each name one of the built-in vertex layouts, so a buffer written
         * with a custom layout through SetDataRaw() could be filled and never read -- an asymmetry
         * with no reason behind it, since the CPU shadow holds the bytes either way.
         *
         * @param offsetInBytes Byte offset into **this buffer**.
         * @param destination   Buffer receiving `count * stride` bytes. May be null only when
         *                      @p count is zero.
         * @param count         Number of vertices to read.
         * @param stride        Size of one vertex in bytes.
         * @throws System::NotSupportedException if this buffer was created write-only, matching
         *         the typed overloads.
         * @throws System::ArgumentOutOfRangeException if the window leaves what has been written.
         * @throws System::ArgumentException if @p stride is unusable.
         */
        CNAEXT void GetDataRawEXT(int offsetInBytes, void* destination, int count, int stride) const;

        /**
         * @brief Internal accessor used by the renderer draw paths.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Renderers::IVertexBufferRenderer& GetRenderer() const { return *renderer_; }

        /**
         * @brief Returns true while the GPU buffer handle is allocated.
         *
         * Becomes false immediately after `Dispose()` is called.
         */
        CNAEXT [[nodiscard]] bool HasRenderer() const { return renderer_ != nullptr; }

    protected:
        /**
         * @brief Uploads typed vertex data with a streaming hint.
         *
         * Called by DynamicVertexBuffer to forward `SetDataOptions` to the renderer.
         * Packs the typed struct into the compact GPU layout before uploading.
         *
         * @param data         Source vertex array.
         * @param startIndex   First element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param options      Streaming hint passed to the renderer.
         */
        void SetDataWithOptions(const VertexPositionColor* data, int startIndex,
                                int elementCount, SetDataOptions options);
        /** @brief Uploads VertexPositionColorTexture data with a streaming hint. */
        void SetDataWithOptions(const VertexPositionColorTexture* data, int startIndex,
                                int elementCount, SetDataOptions options);
        /** @brief Uploads VertexPositionNormalTexture data with a streaming hint. */
        void SetDataWithOptions(const VertexPositionNormalTexture* data, int startIndex,
                                int elementCount, SetDataOptions options);
        /** @brief Uploads VertexPositionTexture data with a streaming hint. */
        void SetDataWithOptions(const VertexPositionTexture* data, int startIndex,
                                int elementCount, SetDataOptions options);

        /**
         * @brief Uploads raw vertex data with an explicit stride and a streaming hint.
         *
         * Called by DynamicVertexBuffer's generic `SetData<T>` overload, for the vertex types
         * that have no dedicated packing path: an application-defined type is uploaded exactly
         * as it sits in memory, so there is nothing to pack and the stride is the type's own.
         *
         * @param data         Source vertex array.
         * @param startIndex   First element to read from @p data.
         * @param elementCount Number of vertices to upload.
         * @param stride       Size of one vertex in bytes.
         * @param options      Streaming hint passed to the renderer.
         */
        void SetDataRawWithOptions(const void* data, int startIndex, int elementCount,
                                   int stride, SetDataOptions options);

        /**
         * @brief Writes raw vertex data into a window of this buffer, with a streaming hint.
         *
         * Called by DynamicVertexBuffer's generic windowed `SetData<T>` overload -- the shape a
         * particle system uses to push only the newly created particles at the queue's write
         * position instead of the whole buffer.
         *
         * **The hint is accepted for XNA conformance and deliberately not forwarded.** A windowed
         * write is composed in this buffer's CPU shadow and then uploaded whole, because the
         * renderer contract replaces whole-buffer contents; passing `NoOverwrite` on to the driver
         * would promise that nothing the GPU may still be reading is touched, and a whole-buffer
         * upload cannot keep that promise. The result is correct and merely slower than XNA's.
         *
         * @param offsetInBytes Byte offset into **this buffer**, a multiple of @p stride.
         * @param data          Source vertex array.
         * @param startIndex    First element to read from @p data.
         * @param elementCount  Number of vertices to write.
         * @param stride        Size of one vertex in bytes.
         * @param options       Streaming hint; see above.
         */
        void SetDataRawAtWithOptions(int offsetInBytes, const void* data, int startIndex,
                                     int elementCount, int stride, SetDataOptions options);

        /**
         * @brief Protected constructor used by DynamicVertexBuffer to pass the dynamic flag.
         *
         * The @p dynamic hint is accepted for XNA API conformance but is currently
         * ignored by all CNA renderers — static and dynamic VBOs use the same GPU path.
         *
         * @param device            Owning graphics device.
         * @param vertexDeclaration Vertex layout description.
         * @param vertexCount       Number of vertices the buffer can hold.
         * @param bufferUsage       Usage hint.
         * @param dynamic           True when the buffer content will be updated frequently.
         */
        VertexBuffer(GraphicsDevice& device,
                     const VertexDeclaration& vertexDeclaration,
                     int vertexCount,
                     BufferUsage bufferUsage,
                     bool dynamic);

        /** @brief Releases the GPU buffer handle when the resource is disposed. */
        void Dispose(bool disposing) override;

    private:
        [[nodiscard]] bool ValidateSetDataRange(const void* data,
                                                int startIndex,
                                                int elementCount,
                                                std::size_t sourceElementSize,
                                                std::size_t uploadStride,
                                                bool rawUpload) const;
        void UploadValidatedData(const void* data,
                                 int elementCount,
                                 std::size_t uploadStride,
                                 SetDataOptions options,
                                 bool useOptions);

        void SetDataInternal(const VertexPositionColor* data,
                             int startIndex,
                             int elementCount,
                             SetDataOptions options,
                             bool useOptions);
        void SetDataInternal(const VertexPositionColorTexture* data,
                             int startIndex,
                             int elementCount,
                             SetDataOptions options,
                             bool useOptions);
        void SetDataInternal(const VertexPositionNormalTexture* data,
                             int startIndex,
                             int elementCount,
                             SetDataOptions options,
                             bool useOptions);
        void SetDataInternal(const VertexPositionTexture* data,
                             int startIndex,
                             int elementCount,
                             SetDataOptions options,
                             bool useOptions);
        void SetDataInternal(const VertexPositionNormalTextureSkinned* data,
                             int startIndex,
                             int elementCount,
                             SetDataOptions options,
                             bool useOptions);

        std::unique_ptr<CNA::Internal::Renderers::IVertexBufferRenderer> renderer_;
        VertexDeclaration vertexDeclaration_;
        BufferUsage bufferUsage_{BufferUsage::None};
        int vertexCount_{0};
        // Task 930: CPU-side shadow of the most recent SetData call's compact GPU-layout bytes,
        // enabling GetData() without a real per-renderer GPU readback path (mirrors Texture2D's
        // own SetData/GetData shadow-buffer precedent) -- nothing in the XNA 4.0 pipeline writes
        // back into a VertexBuffer from the GPU side, so a CPU shadow is a fully faithful
        // implementation, not an approximation.
        std::vector<std::uint8_t> cpuShadow_;
    };
}
