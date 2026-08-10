// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class ModelBone;

    /**
     * @brief Represents a set of bones associated with a model.
     */
    class ModelBoneCollection
    {
    public:
        /** @brief Constructs an empty bone collection. */
        ModelBoneCollection() = default;

        /**
         * @brief Retrieves a ModelBone by index.
         * @param index The zero-based index of the bone to retrieve.
         * @return Pointer to the ModelBone at the specified index.
         * @throws System::ArgumentOutOfRangeException if index is outside the collection.
         */
        [[nodiscard]] ModelBone* operator[](int index) const;

        /**
         * @brief Retrieves a ModelBone by name. Throws if not found.
         * @param name The name of the bone to retrieve.
         * @return Pointer to the ModelBone with the given name.
         */
        [[nodiscard]] ModelBone* operator[](const std::string& name) const;

        /**
         * @brief Gets the number of bones in this collection.
         * @return The bone count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Finds a bone with the given name if it exists in the collection.
         * @param boneName The name of the bone to find.
         * @param value Receives the bone named @p boneName, if found.
         * @return true if the bone was found; otherwise false.
         */
        bool TryGetValue(const std::string& boneName, ModelBone*& value) const;

        /**
         * @brief Determines whether the collection contains the specified bone.
         * @param item The bone to locate.
         * @return true if the bone is present in the collection; otherwise false.
         */
        [[nodiscard]] bool Contains(ModelBone* item) const;

        using iterator = std::vector<ModelBone*>::iterator;
        using const_iterator = std::vector<ModelBone*>::const_iterator;

        /** @brief Returns an iterator to the beginning of the collection. */
        CNAEXT iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<ModelBone*> bones_;
        friend class Model;
        friend class ModelBone;
    };
}
