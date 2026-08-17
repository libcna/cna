// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMeshPart::ModelMeshPart(VertexBuffer* vb, IndexBuffer* ib,
                                 int numVertices, int primitiveCount,
                                 int startIndex, int vertexOffset)
        : numVertices_(numVertices)
        , primitiveCount_(primitiveCount)
        , startIndex_(startIndex)
        , vertexOffset_(vertexOffset)
        , indexBuffer_(ib)
        , vertexBuffer_(vb)
    {}

    int ModelMeshPart::getNumVerticesProperty()  const { return numVertices_; }
    int ModelMeshPart::getPrimitiveCountProperty() const { return primitiveCount_; }
    PrimitiveType ModelMeshPart::getPrimitiveTypeEXTProperty() const { return primitiveType_; }
    void ModelMeshPart::setPrimitiveTypeEXTProperty(PrimitiveType value) { primitiveType_ = value; }
    const std::array<SamplerState, 5>& ModelMeshPart::getSamplerStatesEXTProperty() const
    {
        return samplerStates_;
    }

    void ModelMeshPart::setSamplerStateEXTProperty(int slot, const SamplerState& value)
    {
        // Silently ignoring an out-of-range slot rather than throwing: this is a per-slot setter
        // the importer drives in a loop, and a slot count that grew would otherwise turn a
        // forward-compatible no-op into a crash on old content.
        if (slot < 0 || slot >= static_cast<int>(samplerStates_.size())) { return; }
        samplerStates_[static_cast<std::size_t>(slot)] = value;
    }

    const std::array<SamplerState, 2>& ModelMeshPart::getSpecularSamplerStatesEXTProperty() const
    {
        return specularSamplerStatesEXT_;
    }

    void ModelMeshPart::setSpecularSamplerStateEXTProperty(int slot, const SamplerState& value)
    {
        if (slot < 0 || slot >= static_cast<int>(specularSamplerStatesEXT_.size())) { return; }
        specularSamplerStatesEXT_[static_cast<std::size_t>(slot)] = value;
    }

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

    void ModelMeshPart::SetVertexOffset(int value) { vertexOffset_ = value; }
    void ModelMeshPart::SetNumVertices(int value) { numVertices_ = value; }
    void ModelMeshPart::SetStartIndex(int value) { startIndex_ = value; }
    void ModelMeshPart::SetPrimitiveCount(int value) { primitiveCount_ = value; }
    void ModelMeshPart::SetVertexBuffer(VertexBuffer* value) { vertexBuffer_ = value; }
    void ModelMeshPart::SetIndexBuffer(IndexBuffer* value) { indexBuffer_ = value; }
}
