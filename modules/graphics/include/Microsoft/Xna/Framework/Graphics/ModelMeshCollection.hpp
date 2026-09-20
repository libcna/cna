// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "System/Collections/Generic/IEnumerator.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMesh;

    /**
     * @brief Represents a collection of ModelMesh objects.
     */
    class ModelMeshCollection
    {
    public:
        /** @brief Iterates over the meshes in a ModelMeshCollection. */
        struct Enumerator final : public System::Collections::Generic::IEnumerator<ModelMesh*>,
                                  public System::IDisposable
        {
            /** @brief Constructs the default value of the enumerator struct. */
            Enumerator() = default;

            /**
             * @brief Gets the current mesh.
             * @return The current ModelMesh pointer.
             */
            [[nodiscard]] ModelMesh* const& Current() const override;

            /**
             * @brief Advances to the next mesh.
             * @return True if an element is available.
             */
            bool MoveNext() override;

            /** @brief Returns to the position before the first element. */
            void Reset() override;

            /** @brief Releases enumerator resources. */
            void Dispose() override;

        private:
            friend class ModelMeshCollection;
            explicit Enumerator(const ModelMeshCollection& collection);
            const ModelMeshCollection* collection_ = nullptr;
            int position_ = 0;
            int count_ = 0;
        };

        /**
         * @brief Creates an enumerator positioned before the first mesh.
         * @return An independent value-type enumerator.
         */
        [[nodiscard]] Enumerator GetEnumerator() const;
        /**
         * @brief Retrieves a ModelMesh by index.
         * @param index The zero-based index of the mesh to retrieve.
         * @return Pointer to the ModelMesh at the specified index.
         * @throws System::ArgumentOutOfRangeException if index is outside the collection.
         */
        [[nodiscard]] ModelMesh* operator[](int index) const;

        /**
         * @brief Retrieves a ModelMesh by name.
         * @param name The name of the mesh to retrieve.
         * @return Pointer to the ModelMesh with the given name.
         * @throws System::ArgumentNullException if @p name is empty.
         * @throws System::Collections::Generic::KeyNotFoundException if no mesh has that name.
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
         * @throws System::ArgumentNullException if @p meshName is empty.
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
        CNAEXT iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<ModelMesh*> meshes_;
        friend class Model;
    };
}
