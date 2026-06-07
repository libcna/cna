#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMesh* ModelMeshCollection::operator[](int index) const
    {
        return meshes_[static_cast<std::size_t>(index)];
    }

    ModelMesh* ModelMeshCollection::operator[](const std::string& name) const
    {
        for (ModelMesh* mesh : meshes_)
        {
            if (mesh && mesh->getNameProperty() == name)
                return mesh;
        }
        return nullptr;
    }

    int ModelMeshCollection::getCountProperty() const
    {
        return static_cast<int>(meshes_.size());
    }

    ModelMeshCollection::iterator ModelMeshCollection::begin() { return meshes_.begin(); }
    ModelMeshCollection::iterator ModelMeshCollection::end()   { return meshes_.end(); }
    ModelMeshCollection::const_iterator ModelMeshCollection::begin() const { return meshes_.begin(); }
    ModelMeshCollection::const_iterator ModelMeshCollection::end()   const { return meshes_.end(); }
}
