#pragma once

#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMeshPart;

    /// A collection of ModelMeshPart objects.
    class ModelMeshPartCollection
    {
    public:
        [[nodiscard]] ModelMeshPart* operator[](int index) const;
        [[nodiscard]] int getCountProperty() const;

    private:
        std::vector<ModelMeshPart*> parts_;
        friend class ModelMesh;
    };
}
