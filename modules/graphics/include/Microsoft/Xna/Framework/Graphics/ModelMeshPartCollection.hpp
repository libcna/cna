// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "CNA/CNAHelper.hpp"
#include "System/Collections/Generic/IEnumerator.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelMeshPart;

    /**
     * @brief Represents a collection of ModelMeshPart objects for a mesh.
     */
    class ModelMeshPartCollection
    {
    public:
        /** @brief Iterates over the mesh parts in a ModelMeshPartCollection. */
        struct Enumerator final : public System::Collections::Generic::IEnumerator<ModelMeshPart*>,
                                  public System::IDisposable
        {
            /** @brief Constructs the default value of the enumerator struct. */
            Enumerator() = default;

            /**
             * @brief Gets the current mesh part.
             * @return The current ModelMeshPart pointer.
             */
            [[nodiscard]] ModelMeshPart* const& Current() const override;

            /**
             * @brief Advances to the next mesh part.
             * @return True if an element is available.
             */
            bool MoveNext() override;

            /** @brief Returns to the position before the first element. */
            void Reset() override;

            /** @brief Releases enumerator resources. */
            void Dispose() override;

        private:
            friend class ModelMeshPartCollection;
            explicit Enumerator(const ModelMeshPartCollection& collection);
            const ModelMeshPartCollection* collection_ = nullptr;
            int position_ = 0;
            int count_ = 0;
        };

        /**
         * @brief Creates an enumerator positioned before the first mesh part.
         * @return An independent value-type enumerator.
         */
        [[nodiscard]] Enumerator GetEnumerator() const;
        /**
         * @brief Retrieves a ModelMeshPart by index.
         * @param index The zero-based index of the part to retrieve.
         * @return Pointer to the ModelMeshPart at the specified index.
         * @throws System::ArgumentOutOfRangeException if index is outside the collection.
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
        CNAEXT iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<ModelMeshPart*> parts_;
        friend class ModelMesh;
    };
}
