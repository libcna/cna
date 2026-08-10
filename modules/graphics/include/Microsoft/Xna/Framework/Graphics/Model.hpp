// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "System/Object.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class ModelBone;
    class ModelMesh;

    /**
     * @brief A basic 3D model with per-mesh parent bones.
     */
    class Model
    {
    public:
        /** @brief Constructs an empty model. */
        Model() = default;

        /**
         * @brief Constructs a model from a graphics device, bones, and meshes.
         * @param graphicsDevice A valid reference to the GraphicsDevice.
         * @param bones The collection of bones.
         * @param meshes The collection of meshes.
         */
        CNAEXT Model(GraphicsDevice* graphicsDevice,
                    std::vector<ModelBone*> bones,
                    std::vector<ModelMesh*> meshes);

        /**
         * @brief Constructs a model from a graphics device, bones, meshes, and each mesh's parent bone.
         *
         * @param graphicsDevice A valid reference to the GraphicsDevice.
         * @param bones The collection of bones.
         * @param meshes The collection of meshes.
         * @param meshParentBones Per-mesh parent bone, in the same order as @p meshes. Must either
         *        be empty (every mesh's ParentBone stays nullptr, matching the 3-argument
         *        constructor) or have exactly one entry per mesh.
         * @param rootBoneIndex Index into @p bones naming the model's root bone. Defaults to `0`,
         *        matching the 3-argument constructor's own default. FNA's real content-pipeline
         *        loader (`ModelReader`) can assign an arbitrary root bone this way; this parameter
         *        is the public equivalent for hand-built models.
         */
        CNAEXT Model(GraphicsDevice* graphicsDevice,
                    std::vector<ModelBone*> bones,
                    std::vector<ModelMesh*> meshes,
                    std::vector<ModelBone*> meshParentBones,
                    std::size_t rootBoneIndex = 0);

        /**
         * @brief Gets the collection of bones that describe how each mesh relates to its parent.
         * @return The model's bone collection.
         */
        [[nodiscard]] const ModelBoneCollection& getBonesProperty() const;

        /**
         * @brief Gets the collection of meshes that compose this model.
         * @return The model's mesh collection.
         */
        [[nodiscard]] const ModelMeshCollection& getMeshesProperty() const;

        /**
         * @brief Gets the root bone for this model.
         * @return Pointer to the root ModelBone.
         */
        [[nodiscard]] ModelBone* getRootProperty() const;

        /**
         * @brief Gets the custom object attached to this model.
         * @return Pointer to the tag object, or nullptr.
         */
        [[nodiscard]] System::Object* getTagProperty() const;

        /**
         * @brief Sets the custom object attached to this model.
         * @param value Pointer to the tag object.
         */
        void setTagProperty(System::Object* value);

        /**
         * @brief Transfers ownership of GPU resources allocated by a content reader.
         * @param resources Shared ownership handle to the resources.
         */
        CNAEXT void setOwnedResources(std::shared_ptr<void> resources);

        /**
         * @brief Copies bone transforms relative to all parent bones to a given vector.
         * @param destinationBoneTransforms The vector receiving the absolute bone transforms.
         */
        void CopyAbsoluteBoneTransformsTo(std::vector<Matrix>& destinationBoneTransforms) const;

        /**
         * @brief Copies bone transforms from a given vector into this model's bones.
         * @param sourceBoneTransforms The vector of prepared bone transform data.
         */
        void CopyBoneTransformsFrom(const std::vector<Matrix>& sourceBoneTransforms);

        /**
         * @brief Copies bone transforms relative to the root bone to a given vector.
         * @param destinationBoneTransforms The vector receiving the bone transforms.
         */
        void CopyBoneTransformsTo(std::vector<Matrix>& destinationBoneTransforms) const;

        /**
         * @brief Draws all model meshes using their current effect settings.
         * @param world The world transform matrix.
         * @param view The view transform matrix.
         * @param projection The projection transform matrix.
         */
        void Draw(const Matrix& world, const Matrix& view, const Matrix& projection);

    private:
        ModelBoneCollection bones_;
        ModelMeshCollection meshes_;
        ModelBone* root_ = nullptr;
        System::Object* tag_ = nullptr;
        std::shared_ptr<void> ownedResources_;

        static std::vector<Matrix> sharedDrawBoneMatrices_;
    };
}
