#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelBone* ModelBoneCollection::operator[](int index) const
    {
        return bones_[static_cast<std::size_t>(index)];
    }

    ModelBone* ModelBoneCollection::operator[](const std::string& name) const
    {
        for (ModelBone* bone : bones_)
        {
            if (bone && bone->getNameProperty() == name)
                return bone;
        }
        return nullptr;
    }

    int ModelBoneCollection::getCountProperty() const
    {
        return static_cast<int>(bones_.size());
    }
}
