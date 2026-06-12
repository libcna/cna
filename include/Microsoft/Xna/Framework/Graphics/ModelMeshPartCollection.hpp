// SPDX-License-Identifier: MS-PL
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

        using iterator = std::vector<ModelMeshPart*>::iterator;
        using const_iterator = std::vector<ModelMeshPart*>::const_iterator;
        iterator begin();
        iterator end();
        const_iterator begin() const;
        const_iterator end() const;

    private:
        std::vector<ModelMeshPart*> parts_;
        friend class ModelMesh;
    };
}
