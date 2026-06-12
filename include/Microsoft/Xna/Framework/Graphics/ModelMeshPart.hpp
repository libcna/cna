// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;
    class IndexBuffer;
    class ModelMesh;
    class VertexBuffer; // NOXNA: used by NOXNA constructor
    class VertexDeclaration;

    /// Represents a batch of geometry using the same effect within a ModelMesh.
    class ModelMeshPart
    {
    public:
        ModelMeshPart() = default;
        NOXNA ModelMeshPart(VertexBuffer* vb, IndexBuffer* ib,
                            int numVertices, int primitiveCount,
                            int startIndex, int vertexOffset);

        [[nodiscard]] int getNumVerticesProperty() const;
        [[nodiscard]] int getPrimitiveCountProperty() const;
        [[nodiscard]] int getStartIndexProperty() const;
        [[nodiscard]] int getVertexOffsetProperty() const;
        [[nodiscard]] Effect* getEffectProperty() const;
        void setEffectProperty(Effect* value);
        [[nodiscard]] IndexBuffer* getIndexBufferProperty() const;
        [[nodiscard]] VertexBuffer* getVertexBufferProperty() const;
        [[nodiscard]] System::Object* getTagProperty() const;
        void setTagProperty(System::Object* value);

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
