#pragma once

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

    private:
        std::vector<ModelMesh*> meshes_;
        friend class Model;
    };
}
