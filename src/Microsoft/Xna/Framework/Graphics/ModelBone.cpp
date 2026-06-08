#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

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
    const std::vector<ModelBone*>& ModelBone::getChildrenProperty() const { return children_; }

    void ModelBone::AddChild(ModelBone* child)
    {
        child->parent_ = this;
        children_.push_back(child);
    }
}
