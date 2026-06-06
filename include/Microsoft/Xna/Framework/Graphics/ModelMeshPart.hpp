#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;
    class IndexBuffer;
    class VertexBuffer;
    class VertexDeclaration;

    /// Represents a batch of geometry using the same effect within a ModelMesh.
    class ModelMeshPart
    {
    public:
        [[nodiscard]] int getNumVerticesProperty() const;
        [[nodiscard]] int getPrimitiveCountProperty() const;
        [[nodiscard]] int getStartIndexProperty() const;
        [[nodiscard]] int getVertexOffsetProperty() const;
        [[nodiscard]] Effect* getEffectProperty() const;
        void setEffectProperty(Effect* value);
        [[nodiscard]] IndexBuffer* getIndexBufferProperty() const;
        [[nodiscard]] VertexBuffer* getVertexBufferProperty() const;

    private:
        int numVertices_    = 0;
        int primitiveCount_ = 0;
        int startIndex_     = 0;
        int vertexOffset_   = 0;
        Effect* effect_     = nullptr;
        IndexBuffer* indexBuffer_   = nullptr;
        VertexBuffer* vertexBuffer_ = nullptr;

        friend class ModelMesh;
    };
}
