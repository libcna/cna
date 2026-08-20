// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "System/Object.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class ModelBone;
    class ModelMeshPart;

    /**
     * @brief Represents a mesh that is part of a Model.
     */
    class ModelMesh
    {
    public:
        /**
         * @brief Constructs a mesh from a graphics device and parts.
         * @param graphicsDevice A valid reference to the GraphicsDevice.
         * @param parts The mesh parts that make up this mesh.
         */
        CNAEXT ModelMesh(GraphicsDevice* graphicsDevice, std::vector<ModelMeshPart*> parts);

        /**
         * @brief Constructs a named mesh from a graphics device and parts.
         * @param graphicsDevice A valid reference to the GraphicsDevice.
         * @param name The name of this mesh.
         * @param parts The mesh parts that make up this mesh.
         */
        CNAEXT ModelMesh(GraphicsDevice* graphicsDevice, std::string name,
                        std::vector<ModelMeshPart*> parts);

        /**
         * @brief Gets the bounding sphere that contains this mesh.
         * @return The bounding sphere.
         */
        [[nodiscard]] BoundingSphere getBoundingSphereProperty() const;

        /**
         * @brief Sets the bounding sphere that contains this mesh.
         *
         * Real XNA's `ModelMesh.BoundingSphere` is a public read-write property (the content
         * pipeline's `ModelReader` is the only realistic caller, but the property itself has no
         * `internal`/access restriction to preserve) -- this was a pre-existing gap (getter only)
         * until plans/plan_xnb.md XNB-39 needed a real caller.
         * @param value The new bounding sphere.
         */
        void setBoundingSphereProperty(const BoundingSphere& value);

        /**
         * @brief Gets the collection of effects associated with this mesh.
         * @return A const reference to the effect collection.
         */
        [[nodiscard]] const ModelEffectCollection& getEffectsProperty() const;

        /**
         * @brief Gets a mutable reference to the effect collection.
         * @return A mutable reference to the effect collection.
         */
        CNAEXT [[nodiscard]] ModelEffectCollection& getEffectsPropertyMutable();

        /**
         * @brief Gets the ModelMeshPart objects that make up this mesh.
         * @return A const reference to the mesh parts collection.
         */
        [[nodiscard]] const ModelMeshPartCollection& getMeshPartsProperty() const;

        /**
         * @brief Gets the name of this mesh.
         * @return The mesh name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the parent bone for this mesh.
         * @return Pointer to the parent ModelBone.
         */
        [[nodiscard]] ModelBone* getParentBoneProperty() const;

        /**
         * @brief Sets the parent bone for this mesh.
         * @param value Pointer to the new parent ModelBone.
         */
        void setParentBoneProperty(ModelBone* value);

        /**
         * @brief Gets the custom object attached to this mesh.
         * @return Pointer to the tag object, or nullptr.
         */
        [[nodiscard]] System::Object* getTagProperty() const;

        /**
         * @brief Sets the custom object attached to this mesh.
         * @param value Pointer to the tag object.
         */
        void setTagProperty(System::Object* value);

        /**
         * @brief Draws all ModelMeshPart objects in this mesh using their current effect settings.
         */
        void Draw();

    private:
        GraphicsDevice* graphicsDevice_ = nullptr;
        BoundingSphere boundingSphere_;
        std::string name_;
        ModelBone* parentBone_ = nullptr;
        ModelMeshPartCollection meshParts_;
        ModelEffectCollection effects_;
        System::Object* tag_ = nullptr;

        friend class Model;
    };
}
