// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"

#include <cmath>

#include <stdexcept>
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{

    Matrix CreateInfinitePerspectiveFieldOfViewEXT(float fieldOfView, float aspectRatio,
                                                    float nearPlaneDistance)
    {
        // XNA's own argument contract, repeated rather than delegated: this builder does not call
        // CreatePerspectiveFieldOfView (it cannot -- there is no zfar to pass), so it has to
        // enforce the same preconditions itself or it would accept arguments the finite overload
        // rejects.
        // std::invalid_argument rather than an XNA exception type, to match Matrix's own finite
        // CreatePerspectiveFieldOfView exactly: this builder is that one's limit case and a caller
        // switching between them must not have to catch two different things.
        if (fieldOfView <= 0.0f || fieldOfView >= 3.141593f)
        {
            throw std::invalid_argument("fieldOfView <= 0 or >= PI");
        }
        if (nearPlaneDistance <= 0.0f)
        {
            throw std::invalid_argument("nearPlaneDistance <= 0");
        }

        // CreatePerspectiveFieldOfView with the zfar terms at their limits: -zfar/(zfar-znear) -> -1
        // and (znear*zfar)/(znear-zfar) -> -znear. Written as the limits rather than as a large
        // finite zfar, because a large zfar perturbs EVERY depth value rather than only the far
        // ones -- an exact depth comparison would silently become an approximate one.
        const float yScale = 1.0f / std::tan(fieldOfView * 0.5f);
        const float xScale = yScale / aspectRatio;

        Matrix result = Matrix::getIdentityProperty();
        result.M11 = xScale;
        result.M12 = result.M13 = result.M14 = 0.0f;
        result.M22 = yScale;
        result.M21 = result.M23 = result.M24 = 0.0f;
        result.M31 = result.M32 = 0.0f;
        result.M33 = -1.0f;
        result.M34 = -1.0f;
        result.M41 = result.M42 = result.M44 = 0.0f;
        result.M43 = -nearPlaneDistance;
        return result;
    }

    const std::vector<ModelCameraEXT>& Model::getCamerasEXTProperty() const { return cameras_; }

    void Model::setCamerasEXTProperty(std::vector<ModelCameraEXT> value)
    {
        cameras_ = std::move(value);
    }
    thread_local std::vector<Matrix> Model::sharedDrawBoneMatrices_;

    Model::Model(GraphicsDevice* /*graphicsDevice*/,
                 std::vector<ModelBone*> bones,
                 std::vector<ModelMesh*> meshes)
    {
        bones_.bones_   = std::move(bones);
        meshes_.meshes_ = std::move(meshes);
        if (!bones_.bones_.empty())
            root_ = bones_.bones_[0];
    }

    Model::Model(GraphicsDevice* graphicsDevice,
                 std::vector<ModelBone*> bones,
                 std::vector<ModelMesh*> meshes,
                 std::vector<ModelBone*> meshParentBones,
                 std::size_t rootBoneIndex)
        : Model(graphicsDevice, std::move(bones), std::move(meshes))
    {
        // Matches the 3-argument constructor's own leniency: an empty bones vector leaves root_
        // as nullptr regardless of rootBoneIndex, rather than throwing on the default value 0.
        if (!bones_.bones_.empty())
        {
            if (rootBoneIndex >= bones_.bones_.size())
                throw std::out_of_range("rootBoneIndex");
            root_ = bones_.bones_[rootBoneIndex];
        }

        if (meshParentBones.empty())
            return;
        if (meshParentBones.size() != meshes_.meshes_.size())
            throw std::out_of_range("meshParentBones");

        for (std::size_t i = 0; i < meshParentBones.size(); ++i)
            meshes_.meshes_[i]->parentBone_ = meshParentBones[i];
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
