#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    int ModelMeshPart::getNumVerticesProperty()  const { return numVertices_; }
    int ModelMeshPart::getPrimitiveCountProperty() const { return primitiveCount_; }
    int ModelMeshPart::getStartIndexProperty()   const { return startIndex_; }
    int ModelMeshPart::getVertexOffsetProperty() const { return vertexOffset_; }

    Effect* ModelMeshPart::getEffectProperty() const { return effect_; }

    void ModelMeshPart::setEffectProperty(Effect* value)
    {
        if (value == effect_)
            return;

        if (effect_ != nullptr && parent_ != nullptr)
        {
            // Only remove the old effect if no other part in this mesh still uses it.
            bool removeEffect = true;
            int count = parent_->getMeshPartsProperty().getCountProperty();
            for (int i = 0; i < count; ++i)
            {
                ModelMeshPart* part = parent_->getMeshPartsProperty()[i];
                if (part != this && part->effect_ == effect_)
                {
                    removeEffect = false;
                    break;
                }
            }
            if (removeEffect)
                parent_->getEffectsPropertyMutable().Remove(effect_);
        }

        effect_ = value;

        if (effect_ != nullptr && parent_ != nullptr &&
            !parent_->getEffectsProperty().Contains(effect_))
        {
            parent_->getEffectsPropertyMutable().Add(effect_);
        }
    }

    IndexBuffer*  ModelMeshPart::getIndexBufferProperty()  const { return indexBuffer_; }
    VertexBuffer* ModelMeshPart::getVertexBufferProperty() const { return vertexBuffer_; }

    System::Object* ModelMeshPart::getTagProperty() const { return tag_; }
    void ModelMeshPart::setTagProperty(System::Object* value) { tag_ = value; }
}
