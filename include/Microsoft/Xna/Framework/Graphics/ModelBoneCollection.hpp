// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

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

    private:
        std::vector<ModelBone*> bones_;
        friend class Model;
    };
}
