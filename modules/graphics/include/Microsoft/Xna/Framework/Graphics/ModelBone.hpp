// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Represents bone data for a model.
     */
    class ModelBone
    {
    public:
        /** @brief Constructs a default bone. */
        CNAEXT ModelBone() = default;

        /**
         * @brief Constructs a bone with the given index and name.
         * @param index The index of this bone in the model's Bones collection.
         * @param name The name of this bone.
         */
        CNAEXT ModelBone(int index, std::string name);

        /**
         * @brief Gets the name of this bone.
         * @return The bone's name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the index of this bone in the Bones collection.
         * @return The zero-based bone index.
         */
        [[nodiscard]] int getIndexProperty() const;

        /**
         * @brief Gets the transform matrix of this bone relative to its parent bone.
         * @return The bone transform matrix.
         */
        [[nodiscard]] const Matrix& getTransformProperty() const;

        /**
         * @brief Sets the transform matrix of this bone relative to its parent bone.
         * @param value The new transform matrix.
         */
        void setTransformProperty(const Matrix& value);

        /**
         * @brief Gets the parent bone of this bone.
         * @return Pointer to the parent ModelBone, or nullptr if this is the root.
         */
        [[nodiscard]] ModelBone* getParentProperty() const;

        /**
         * @brief Gets a collection of bones that are children of this bone.
         * @return The collection of child bones.
         */
        [[nodiscard]] const ModelBoneCollection& getChildrenProperty() const;

        /**
         * @brief Adds a child bone to this bone.
         * @param child Pointer to the child ModelBone to add.
         */
        CNAEXT void AddChild(ModelBone* child);

    private:
        std::string name_;
        int index_ = 0;
        Matrix transform_ = Matrix::getIdentityProperty();
        ModelBone* parent_ = nullptr;
        ModelBoneCollection children_;

        friend class Model;
    };
}
