// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Graphics
{
    ModelBone::ModelBone(int index, std::string name)
        : name_(std::move(name)), index_(index)
    {}

    const std::string& ModelBone::getNameProperty()            const { return name_; }
    int                ModelBone::getIndexProperty()           const { return index_; }
    const Matrix&      ModelBone::getTransformProperty()       const { return transform_; }
    void               ModelBone::setTransformProperty(const Matrix& value) { transform_ = value; }
    ModelBone*         ModelBone::getParentProperty()          const { return parent_; }
    const ModelBoneCollection& ModelBone::getChildrenProperty() const { return children_; }

    void ModelBone::AddChild(ModelBone* child)
    {
        child->parent_ = this;
        children_.bones_.push_back(child);
    }

    // XNA ModelBone.SetParentAndChildren IL_0000-0013: stores the parent and wraps the children
    // array as is.
    void ModelBone::SetParentAndChildren(ModelBone* newParent, std::vector<ModelBone*> newChildren)
    {
        parent_ = newParent;
        children_.bones_ = std::move(newChildren);
    }
}
