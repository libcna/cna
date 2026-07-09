// SPDX-License-Identifier: MS-PL
#include <stdexcept>
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelBone* ModelBoneCollection::operator[](int index) const
    {
        return bones_.at(static_cast<std::size_t>(index));
    }

    ModelBone* ModelBoneCollection::operator[](const std::string& name) const
    {
        ModelBone* value = nullptr;
        if (TryGetValue(name, value))
            return value;
        throw std::out_of_range("ModelBoneCollection: bone not found: " + name);
    }

    int ModelBoneCollection::getCountProperty() const
    {
        return static_cast<int>(bones_.size());
    }

    bool ModelBoneCollection::TryGetValue(const std::string& boneName, ModelBone*& value) const
    {
        for (ModelBone* bone : bones_)
        {
            if (bone && bone->getNameProperty() == boneName)
            {
                value = bone;
                return true;
            }
        }
        value = nullptr;
        return false;
    }

    bool ModelBoneCollection::Contains(ModelBone* item) const
    {
        for (ModelBone* bone : bones_)
        {
            if (bone == item)
                return true;
        }
        return false;
    }

    ModelBoneCollection::iterator ModelBoneCollection::begin() { return bones_.begin(); }
    ModelBoneCollection::iterator ModelBoneCollection::end() { return bones_.end(); }
    ModelBoneCollection::const_iterator ModelBoneCollection::begin() const { return bones_.begin(); }
    ModelBoneCollection::const_iterator ModelBoneCollection::end() const { return bones_.end(); }
}
