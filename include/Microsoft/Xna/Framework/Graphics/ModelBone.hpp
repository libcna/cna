#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelBoneCollection;

    /// Represents a bone in a model skeleton.
    class ModelBone
    {
    public:
        [[nodiscard]] const std::string& getNameProperty() const;
        [[nodiscard]] int getIndexProperty() const;
        [[nodiscard]] const Matrix& getTransformProperty() const;
        void setTransformProperty(const Matrix& value);
        [[nodiscard]] ModelBone* getParentProperty() const;
        [[nodiscard]] const std::vector<ModelBone*>& getChildrenProperty() const;

        NOXNA void AddChild(ModelBone* child);

    private:
        std::string name_;
        int index_ = 0;
        Matrix transform_ = Matrix::getIdentityProperty();
        ModelBone* parent_ = nullptr;
        std::vector<ModelBone*> children_;

        friend class Model;
    };
}
