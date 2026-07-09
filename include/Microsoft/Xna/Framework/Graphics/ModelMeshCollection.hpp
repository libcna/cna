// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMesh;

    /**
     * @brief Represents a collection of ModelMesh objects.
     */
    class ModelMeshCollection
    {
    public:
        /**
         * @brief Retrieves a ModelMesh by index.
         * @param index The zero-based index of the mesh to retrieve.
         * @return Pointer to the ModelMesh at the specified index.
         */
        [[nodiscard]] ModelMesh* operator[](int index) const;

        /**
         * @brief Retrieves a ModelMesh by name. Throws if not found.
         * @param name The name of the mesh to retrieve.
         * @return Pointer to the ModelMesh with the given name.
         */
        [[nodiscard]] ModelMesh* operator[](const std::string& name) const;

        /**
         * @brief Gets the number of meshes in this collection.
         * @return The mesh count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Finds a mesh with the given name if it exists in the collection.
         * @param meshName The name of the mesh to find.
         * @param value Receives the mesh named @p meshName, if found.
         * @return true if the mesh was found; otherwise false.
         */
        bool TryGetValue(const std::string& meshName, ModelMesh*& value) const;

        /**
         * @brief Determines whether the collection contains the specified mesh.
         * @param item The mesh to locate.
         * @return true if the mesh is present in the collection; otherwise false.
         */
        [[nodiscard]] bool Contains(ModelMesh* item) const;

        using iterator = std::vector<ModelMesh*>::iterator;
        using const_iterator = std::vector<ModelMesh*>::const_iterator;

        /** @brief Returns an iterator to the beginning of the collection. */
        NOXNA iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        NOXNA iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        NOXNA const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        NOXNA const_iterator end() const;

    private:
        std::vector<ModelMesh*> meshes_;
        friend class Model;
    };
}
