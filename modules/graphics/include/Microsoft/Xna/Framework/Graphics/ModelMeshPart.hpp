// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;
    class IndexBuffer;
    class ModelMesh;
    class VertexBuffer;
    class VertexDeclaration;

    /**
     * @brief Represents a batch of geometry using the same effect within a ModelMesh.
     */
    class ModelMeshPart
    {
    public:
        /** @brief Constructs an empty mesh part. */
        ModelMeshPart() = default;

        /**
         * @brief Constructs a mesh part with explicit buffer and count parameters.
         * @param vb The vertex buffer for this mesh part.
         * @param ib The index buffer for this mesh part.
         * @param numVertices The number of vertices used during a draw call.
         * @param primitiveCount The number of primitives to render.
         * @param startIndex The location in the index buffer at which to start reading.
         * @param vertexOffset The offset in vertices from the top of the vertex buffer.
         */
        NOXNA ModelMeshPart(VertexBuffer* vb, IndexBuffer* ib,
                            int numVertices, int primitiveCount,
                            int startIndex, int vertexOffset);

        /**
         * @brief Gets the number of vertices used during a draw call.
         * @return The vertex count.
         */
        [[nodiscard]] int getNumVerticesProperty() const;

        /**
         * @brief Gets the number of primitives to render.
         * @return The primitive count.
         */
        [[nodiscard]] int getPrimitiveCountProperty() const;

        /**
         * @brief Gets the location in the index array at which to start reading vertices.
         * @return The start index.
         */
        [[nodiscard]] int getStartIndexProperty() const;

        /**
         * @brief Gets the offset in vertices from the top of the vertex buffer.
         * @return The vertex offset.
         */
        [[nodiscard]] int getVertexOffsetProperty() const;

        /**
         * @brief Gets the material Effect for this mesh part.
         * @return Pointer to the Effect, or nullptr if none is set.
         */
        [[nodiscard]] Effect* getEffectProperty() const;

        /**
         * @brief Sets the material Effect for this mesh part.
         * @param value Pointer to the new Effect.
         */
        void setEffectProperty(Effect* value);

        /**
         * @brief Gets the index buffer for this mesh part.
         * @return Pointer to the IndexBuffer.
         */
        [[nodiscard]] IndexBuffer* getIndexBufferProperty() const;

        /**
         * @brief Gets the vertex buffer for this mesh part.
         * @return Pointer to the VertexBuffer.
         */
        [[nodiscard]] VertexBuffer* getVertexBufferProperty() const;

        /**
         * @brief Gets the custom object attached to this mesh part.
         * @return Pointer to the tag object, or nullptr.
         */
        [[nodiscard]] System::Object* getTagProperty() const;

        /**
         * @brief Sets the custom object attached to this mesh part.
         * @param value Pointer to the tag object.
         */
        void setTagProperty(System::Object* value);

        /**
         * @brief Sets the offset in vertices from the top of the vertex buffer.
         *
         * NOXNA: real XNA's `ModelMeshPart.VertexOffset` setter is content-pipeline-only
         * (`internal set`) rather than a public game-facing API; the content pipeline is the only
         * real caller (plan_xnb.md XNB-38's `ModelReader`), so this stays a NOXNA-marked method
         * rather than a bare public setter.
         * @param value The new vertex offset.
         */
        NOXNA void SetVertexOffset(int value);

        /**
         * @brief Sets the number of vertices used during a draw call. See `SetVertexOffset()`'s
         *        own note on why this is NOXNA rather than a bare public setter.
         * @param value The new vertex count.
         */
        NOXNA void SetNumVertices(int value);

        /**
         * @brief Sets the location in the index buffer at which to start reading. See
         *        `SetVertexOffset()`'s own note on why this is NOXNA rather than a bare public setter.
         * @param value The new start index.
         */
        NOXNA void SetStartIndex(int value);

        /**
         * @brief Sets the number of primitives to render. See `SetVertexOffset()`'s own note on
         *        why this is NOXNA rather than a bare public setter.
         * @param value The new primitive count.
         */
        NOXNA void SetPrimitiveCount(int value);

        /**
         * @brief Sets the vertex buffer for this mesh part. See `SetVertexOffset()`'s own note on
         *        why this is NOXNA rather than a bare public setter.
         * @param value Pointer to the new VertexBuffer.
         */
        NOXNA void SetVertexBuffer(VertexBuffer* value);

        /**
         * @brief Sets the index buffer for this mesh part. See `SetVertexOffset()`'s own note on
         *        why this is NOXNA rather than a bare public setter.
         * @param value Pointer to the new IndexBuffer.
         */
        NOXNA void SetIndexBuffer(IndexBuffer* value);

    private:
        int numVertices_    = 0;
        int primitiveCount_ = 0;
        int startIndex_     = 0;
        int vertexOffset_   = 0;
        Effect* effect_     = nullptr;
        IndexBuffer* indexBuffer_   = nullptr;
        VertexBuffer* vertexBuffer_ = nullptr;
        System::Object* tag_        = nullptr;
        ModelMesh* parent_          = nullptr;

        friend class ModelMesh;
    };
}
