// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMesh::ModelMesh(GraphicsDevice* graphicsDevice, std::vector<ModelMeshPart*> parts)
        : graphicsDevice_(graphicsDevice)
    {
        meshParts_.parts_ = std::move(parts);
        for (ModelMeshPart* part : meshParts_.parts_)
            part->parent_ = this;
    }

    ModelMesh::ModelMesh(GraphicsDevice* graphicsDevice, std::string name,
                         std::vector<ModelMeshPart*> parts)
        : graphicsDevice_(graphicsDevice)
        , name_(std::move(name))
    {
        meshParts_.parts_ = std::move(parts);
        for (ModelMeshPart* part : meshParts_.parts_)
            part->parent_ = this;
    }

    BoundingSphere ModelMesh::getBoundingSphereProperty() const { return boundingSphere_; }
    void ModelMesh::setBoundingSphereProperty(const BoundingSphere& value) { boundingSphere_ = value; }
    const ModelEffectCollection& ModelMesh::getEffectsProperty()        const { return effects_; }
    ModelEffectCollection&       ModelMesh::getEffectsPropertyMutable()       { return effects_; }
    const ModelMeshPartCollection& ModelMesh::getMeshPartsProperty()    const { return meshParts_; }
    const std::string&             ModelMesh::getNameProperty()         const { return name_; }
    ModelBone*                     ModelMesh::getParentBoneProperty()   const { return parentBone_; }
    void                           ModelMesh::setParentBoneProperty(ModelBone* value) { parentBone_ = value; }
    System::Object*                ModelMesh::getTagProperty()          const { return tag_; }
    void                           ModelMesh::setTagProperty(System::Object* value) { tag_ = value; }

    void ModelMesh::Draw()
    {
        if (graphicsDevice_ != nullptr)
            graphicsDevice_->GetRenderer().Ensure3DSupported("ModelMesh::Draw");

        for (ModelMeshPart* part : meshParts_)
        {
            Effect* effect = part->getEffectProperty();
            if (effect == nullptr || part->getPrimitiveCountProperty() <= 0)
                continue;

            graphicsDevice_->SetVertexBuffer(part->getVertexBufferProperty());
            graphicsDevice_->setIndicesProperty(part->getIndexBufferProperty());

            EffectTechnique* technique = effect->getCurrentTechniqueProperty();
            for (EffectPass& pass : technique->getPassesProperty())
            {
                pass.Apply();
                // plans/plan_gltf.md GLTF-073: the part's own topology, not an assumed triangle list.
                // Every part built by any other path defaults to TriangleList, so this is a
                // no-op for existing content and the one place a line or point part can be drawn
                // as what it actually is.
                graphicsDevice_->DrawIndexedPrimitives(
                    part->getPrimitiveTypeEXTProperty(),
                    part->getVertexOffsetProperty(),
                    0,
                    part->getNumVerticesProperty(),
                    part->getStartIndexProperty(),
                    part->getPrimitiveCountProperty()
                );
            }
        }
    }
}
