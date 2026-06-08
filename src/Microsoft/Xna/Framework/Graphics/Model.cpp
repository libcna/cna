#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    std::vector<Matrix> Model::sharedDrawBoneMatrices_;

    Model::Model(GraphicsDevice* /*graphicsDevice*/,
                 std::vector<ModelBone*> bones,
                 std::vector<ModelMesh*> meshes)
    {
        bones_.bones_   = std::move(bones);
        meshes_.meshes_ = std::move(meshes);
        if (!bones_.bones_.empty())
            root_ = bones_.bones_[0];
    }

    void Model::setOwnedResources(std::shared_ptr<void> resources) {
        ownedResources_ = std::move(resources);
    }

    const ModelBoneCollection& Model::getBonesProperty()  const { return bones_; }
    const ModelMeshCollection& Model::getMeshesProperty() const { return meshes_; }
    ModelBone*                 Model::getRootProperty()   const { return root_; }
    System::Object*            Model::getTagProperty()    const { return tag_; }
    void                       Model::setTagProperty(System::Object* value) { tag_ = value; }

    void Model::CopyAbsoluteBoneTransformsTo(std::vector<Matrix>& dest) const
    {
        if (dest.size() < static_cast<std::size_t>(bones_.getCountProperty()))
            throw std::out_of_range("destinationBoneTransforms");

        int count = bones_.getCountProperty();
        for (int i = 0; i < count; ++i)
        {
            ModelBone* bone = bones_[i];
            if (bone->getParentProperty() == nullptr)
            {
                dest[static_cast<std::size_t>(i)] = bone->getTransformProperty();
            }
            else
            {
                int parentIdx = bone->getParentProperty()->getIndexProperty();
                dest[static_cast<std::size_t>(i)] =
                    bone->getTransformProperty() *
                    dest[static_cast<std::size_t>(parentIdx)];
            }
        }
    }

    void Model::CopyBoneTransformsFrom(const std::vector<Matrix>& src)
    {
        if (src.size() < static_cast<std::size_t>(bones_.getCountProperty()))
            throw std::out_of_range("sourceBoneTransforms");

        int count = bones_.getCountProperty();
        for (int i = 0; i < count; ++i)
            bones_[i]->setTransformProperty(src[static_cast<std::size_t>(i)]);
    }

    void Model::CopyBoneTransformsTo(std::vector<Matrix>& dest) const
    {
        if (dest.size() < static_cast<std::size_t>(bones_.getCountProperty()))
            throw std::out_of_range("destinationBoneTransforms");

        int count = bones_.getCountProperty();
        for (int i = 0; i < count; ++i)
            dest[static_cast<std::size_t>(i)] = bones_[i]->getTransformProperty();
    }

    void Model::Draw(const Matrix& world, const Matrix& view, const Matrix& projection)
    {
        int boneCount = bones_.getCountProperty();

        if (static_cast<int>(sharedDrawBoneMatrices_.size()) < boneCount)
            sharedDrawBoneMatrices_.resize(static_cast<std::size_t>(boneCount));

        CopyAbsoluteBoneTransformsTo(sharedDrawBoneMatrices_);

        int meshCount = meshes_.getCountProperty();
        for (int mi = 0; mi < meshCount; ++mi)
        {
            ModelMesh* mesh = meshes_[mi];
            const ModelEffectCollection& effects = mesh->getEffectsProperty();
            int effectCount = effects.getCountProperty();
            for (int ei = 0; ei < effectCount; ++ei)
            {
                Effect* effect = effects[ei];
                IEffectMatrices* em = dynamic_cast<IEffectMatrices*>(effect);
                if (em == nullptr)
                    throw std::runtime_error("Effect does not implement IEffectMatrices");

                int boneIdx = mesh->getParentBoneProperty()
                              ? mesh->getParentBoneProperty()->getIndexProperty() : 0;
                em->setWorldProperty(
                    sharedDrawBoneMatrices_[static_cast<std::size_t>(boneIdx)] * world);
                em->setViewProperty(view);
                em->setProjectionProperty(projection);
            }
            mesh->Draw();
        }
    }
}
