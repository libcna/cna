#pragma once

#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMesh;

    /// A collection of ModelMesh objects.
    class ModelMeshCollection
    {
    public:
        [[nodiscard]] ModelMesh* operator[](int index) const;
        [[nodiscard]] ModelMesh* operator[](const std::string& name) const;
        [[nodiscard]] int getCountProperty() const;

        using iterator = std::vector<ModelMesh*>::iterator;
        using const_iterator = std::vector<ModelMesh*>::const_iterator;
        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

    private:
        std::vector<ModelMesh*> meshes_;
        friend class Model;
    };
}
