// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMeshPart;

    /**
     * @brief Represents a collection of ModelMeshPart objects for a mesh.
     */
    class ModelMeshPartCollection
    {
    public:
        /**
         * @brief Retrieves a ModelMeshPart by index.
         * @param index The zero-based index of the part to retrieve.
         * @return Pointer to the ModelMeshPart at the specified index.
         */
        [[nodiscard]] ModelMeshPart* operator[](int index) const;

        /**
         * @brief Gets the number of parts in this collection.
         * @return The part count.
         */
        [[nodiscard]] int getCountProperty() const;

        using iterator = std::vector<ModelMeshPart*>::iterator;
        using const_iterator = std::vector<ModelMeshPart*>::const_iterator;

        /** @brief Returns an iterator to the beginning of the collection. */
        iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        const_iterator end() const;

    private:
        std::vector<ModelMeshPart*> parts_;
        friend class ModelMesh;
    };
}
