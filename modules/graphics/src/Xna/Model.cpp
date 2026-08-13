// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"

#include <algorithm>
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
    namespace
    {
        BoundingSphere TransformModelBoundsSphereEXT(const BoundingSphere& sphere,
                                                      const Matrix& matrix)
        {
            BoundingSphere result;
            result.Center = Vector3::Transform(sphere.Center, matrix);

            // BoundingSphere::Transform follows XNA and uses the longest transformed basis
            // vector. That is exact for one TRS, whose basis is orthogonal, but a hierarchy can
            // compose a rotated child below a non-uniformly scaled parent and thereby create
            // shear. Under shear the longest basis vector can be shorter than the matrix's true
            // maximum stretch, so a nominal "bound" can exclude vertices.
            //
            // The squared maximum stretch is the largest eigenvalue of A*A^T. The maximum
            // absolute row sum of that symmetric matrix is a conservative upper bound on the
            // eigenvalue (and remains exact for an orthogonal scaled basis). This keeps the
            // ordinary XNA result for rotation/scale while making composed glTF placements safe.
            const double b11 = static_cast<double>(matrix.M11) * matrix.M11 +
                               static_cast<double>(matrix.M12) * matrix.M12 +
                               static_cast<double>(matrix.M13) * matrix.M13;
            const double b22 = static_cast<double>(matrix.M21) * matrix.M21 +
                               static_cast<double>(matrix.M22) * matrix.M22 +
                               static_cast<double>(matrix.M23) * matrix.M23;
            const double b33 = static_cast<double>(matrix.M31) * matrix.M31 +
                               static_cast<double>(matrix.M32) * matrix.M32 +
                               static_cast<double>(matrix.M33) * matrix.M33;
            const double b12 = static_cast<double>(matrix.M11) * matrix.M21 +
                               static_cast<double>(matrix.M12) * matrix.M22 +
                               static_cast<double>(matrix.M13) * matrix.M23;
            const double b13 = static_cast<double>(matrix.M11) * matrix.M31 +
                               static_cast<double>(matrix.M12) * matrix.M32 +
                               static_cast<double>(matrix.M13) * matrix.M33;
            const double b23 = static_cast<double>(matrix.M21) * matrix.M31 +
                               static_cast<double>(matrix.M22) * matrix.M32 +
                               static_cast<double>(matrix.M23) * matrix.M33;
            const double stretchSquared = std::max(
                b11 + std::abs(b12) + std::abs(b13),
                std::max(b22 + std::abs(b12) + std::abs(b23),
                         b33 + std::abs(b13) + std::abs(b23)));
            result.Radius = sphere.Radius *
                static_cast<float>(std::sqrt(std::max(0.0, stretchSquared)));
            return result;
        }
    }

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

    std::optional<BoundingSphere> Model::getBoundingSphereEXTProperty() const
    {
        const int meshCount = meshes_.getCountProperty();
        if (meshCount == 0) { return std::nullopt; }

        const int boneCount = bones_.getCountProperty();
        std::vector<Matrix> absoluteBoneTransforms(static_cast<std::size_t>(boneCount));
        if (boneCount > 0) { CopyAbsoluteBoneTransformsTo(absoluteBoneTransforms); }

        std::optional<BoundingSphere> result;
        for (int i = 0; i < meshCount; ++i)
        {
            const ModelMesh* mesh = meshes_[i];
            Matrix placement = Matrix::getIdentityProperty();
            if (boneCount > 0)
            {
                // Match Model::Draw exactly: a mesh with no explicit ParentBone uses bone zero.
                const ModelBone* parent = mesh->getParentBoneProperty();
                const int boneIndex = parent != nullptr ? parent->getIndexProperty() : 0;
                placement = absoluteBoneTransforms.at(static_cast<std::size_t>(boneIndex));
            }

            const BoundingSphere placed =
                TransformModelBoundsSphereEXT(mesh->getBoundingSphereProperty(), placement);
            result = result.has_value()
                ? BoundingSphere::CreateMerged(*result, placed)
                : placed;
        }
        return result;
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
