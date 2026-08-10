// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief Represents a collection of effects associated with a model mesh.
     */
    class ModelEffectCollection
    {
    public:
        /**
         * @brief Retrieves an Effect by index.
         * @param index The zero-based index of the effect to retrieve.
         * @return Pointer to the Effect at the specified index.
         * @throws System::ArgumentOutOfRangeException if index is outside the collection.
         */
        [[nodiscard]] Effect* operator[](int index) const;

        /**
         * @brief Gets the number of effects in this collection.
         * @return The effect count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Returns true if the collection contains the given effect.
         * @param effect Pointer to the Effect to search for.
         * @return True if found; false otherwise.
         */
        [[nodiscard]] bool Contains(const Effect* effect) const;

        /**
         * @brief Adds an effect to this collection.
         * @param effect Pointer to the Effect to add.
         */
        CNAEXT void Add(Effect* effect);

        /**
         * @brief Removes an effect from this collection.
         * @param effect Pointer to the Effect to remove.
         */
        CNAEXT void Remove(const Effect* effect);

        using iterator = std::vector<Effect*>::iterator;
        using const_iterator = std::vector<Effect*>::const_iterator;

        /** @brief Returns an iterator to the beginning of the collection. */
        CNAEXT iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<Effect*> effects_;
        friend class ModelMesh;
    };
}
