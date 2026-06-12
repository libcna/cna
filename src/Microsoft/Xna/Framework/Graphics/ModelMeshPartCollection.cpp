// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMeshPart* ModelMeshPartCollection::operator[](int index) const
    {
        return parts_[static_cast<std::size_t>(index)];
    }

    int ModelMeshPartCollection::getCountProperty() const
    {
        return static_cast<int>(parts_.size());
    }

    ModelMeshPartCollection::iterator ModelMeshPartCollection::begin() { return parts_.begin(); }
    ModelMeshPartCollection::iterator ModelMeshPartCollection::end()   { return parts_.end(); }
    ModelMeshPartCollection::const_iterator ModelMeshPartCollection::begin() const { return parts_.begin(); }
    ModelMeshPartCollection::const_iterator ModelMeshPartCollection::end()   const { return parts_.end(); }
}
