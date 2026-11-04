// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMesh::ModelMesh(GraphicsDevice* graphicsDevice, std::vector<ModelMeshPart*> parts)
        : ModelMesh(graphicsDevice, std::string(), std::move(parts))
    {
    }

    ModelMesh::ModelMesh(GraphicsDevice* graphicsDevice, std::string name,
                         std::vector<ModelMeshPart*> parts)
        : graphicsDevice_(graphicsDevice)
        , name_(std::move(name))
    {
        meshParts_.parts_ = std::move(parts);
        for (ModelMeshPart* part : meshParts_.parts_)
        {
            part->parent_ = this;
            // A part that already carries an effect contributes it now, exactly as
            // ModelMeshPart::setEffectProperty would once the part belongs to this mesh
            // (CNA-DEMOS-ISSUE-081).
            if (part->effect_ != nullptr && !effects_.Contains(part->effect_))
                effects_.Add(part->effect_);
        }
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
            // XNA Refresh ModelMesh.Draw IL_0025..0032: refuse null effects before any draw gate.
            if (effect == nullptr)
                throw System::InvalidOperationException("ModelMeshPart has a null Effect.");

            // XNA Refresh ModelMesh.Draw IL_0033..0069: capture the count, then re-read the
            // current technique for each pass (OnApply can change it).
            const int passCount = effect->getCurrentTechniqueProperty()->getPassesProperty().getCountProperty();
            for (int passIndex = 0; passIndex < passCount; ++passIndex)
            {
                EffectPass* pass = effect->getCurrentTechniqueProperty()->getPassesProperty()[passIndex];
                // Refresh EffectPassCollection.get_Item(int) IL_001f returns null out of range;
                // ModelMesh.Draw IL_005a then throws NullReferenceException via callvirt.
                if (pass == nullptr)
                    throw System::NullReferenceException();
                pass->Apply();
                // XNA Refresh ModelMeshPart.Draw IL_0000..0007: this gate is after Apply,
                // and tests NumVertices rather than PrimitiveCount (FNA gates primitives).
                if (part->getNumVerticesProperty() <= 0)
                    continue;

                VertexBuffer* vertices = part->getVertexBufferProperty();
                if (vertices == nullptr)
                    throw System::NullReferenceException();
                // Refresh Part.Draw IL_0009..002e uses the vertex buffer's device and binds
                // VertexOffset on the stream; DrawIndexedPrimitives receives baseVertex zero.
                GraphicsDevice* device = vertices->getGraphicsDeviceProperty();
                device->SetVertexBuffer(vertices, part->getVertexOffsetProperty());
                device->setIndicesProperty(part->getIndexBufferProperty());
                device->DrawIndexedPrimitives(
                    part->getPrimitiveTypeEXTProperty(),
                    0,
                    0,
                    part->getNumVerticesProperty(),
                    part->getStartIndexProperty(),
                    part->getPrimitiveCountProperty()
                );
            }
        }
    }
}
