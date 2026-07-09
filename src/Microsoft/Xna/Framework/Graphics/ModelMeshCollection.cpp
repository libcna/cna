// SPDX-License-Identifier: MS-PL
#include <stdexcept>
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    ModelMesh* ModelMeshCollection::operator[](int index) const
    {
        return meshes_.at(static_cast<std::size_t>(index));
    }

    ModelMesh* ModelMeshCollection::operator[](const std::string& name) const
    {
        ModelMesh* value = nullptr;
        if (TryGetValue(name, value))
            return value;
        throw std::out_of_range("ModelMeshCollection: mesh not found: " + name);
    }

    int ModelMeshCollection::getCountProperty() const
    {
        return static_cast<int>(meshes_.size());
    }

    bool ModelMeshCollection::TryGetValue(const std::string& meshName, ModelMesh*& value) const
    {
        for (ModelMesh* mesh : meshes_)
        {
            if (mesh && mesh->getNameProperty() == meshName)
            {
                value = mesh;
                return true;
            }
        }
        value = nullptr;
        return false;
    }

    bool ModelMeshCollection::Contains(ModelMesh* item) const
    {
        for (ModelMesh* mesh : meshes_)
        {
            if (mesh == item)
                return true;
        }
        return false;
    }

    ModelMeshCollection::iterator ModelMeshCollection::begin() { return meshes_.begin(); }
    ModelMeshCollection::iterator ModelMeshCollection::end()   { return meshes_.end(); }
    ModelMeshCollection::const_iterator ModelMeshCollection::begin() const { return meshes_.begin(); }
    ModelMeshCollection::const_iterator ModelMeshCollection::end()   const { return meshes_.end(); }
}
