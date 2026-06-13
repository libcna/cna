// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An indexed, name-keyed collection of EffectPass objects within a technique.
     */
    class EffectPassCollection
    {
    public:
        /** @brief Constructs an empty EffectPassCollection. */
        EffectPassCollection() = default;

        /**
         * @brief Gets the number of passes in this collection.
         *
         * @return The pass count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets the pass at the specified index (mutable overload).
         *
         * @param index Zero-based index of the pass.
         * @return Reference to the pass.
         */
        [[nodiscard]] EffectPass& operator[](int index);

        /**
         * @brief Gets the pass at the specified index (const overload).
         *
         * @param index Zero-based index of the pass.
         * @return Const reference to the pass.
         */
        [[nodiscard]] const EffectPass& operator[](int index) const;

        /**
         * @brief Gets the pass with the specified name.
         *
         * @param name The pass name to search for.
         * @return Pointer to the matching pass, or nullptr if not found.
         */
        [[nodiscard]] EffectPass* operator[](const std::string& name);

        /**
         * @brief Gets the pass with the specified name (const overload).
         *
         * @param name The pass name to search for.
         * @return Const pointer to the matching pass, or nullptr if not found.
         */
        [[nodiscard]] const EffectPass* operator[](const std::string& name) const;

        /**
         * @brief Adds a pass to this collection.
         *
         * @param pass The EffectPass to add.
         */
        void Add(EffectPass pass);

        /** @brief Mutable iterator type for range-for support. */
        using iterator = std::vector<EffectPass>::iterator;
        /** @brief Const iterator type for range-for support. */
        using const_iterator = std::vector<EffectPass>::const_iterator;

        /**
         * @brief Returns a mutable iterator to the first pass.
         *
         * @return Begin iterator.
         */
        iterator begin();

        /**
         * @brief Returns a mutable iterator past the last pass.
         *
         * @return End iterator.
         */
        iterator end();

        /**
         * @brief Returns a const iterator to the first pass.
         *
         * @return Const begin iterator.
         */
        const_iterator begin() const;

        /**
         * @brief Returns a const iterator past the last pass.
         *
         * @return Const end iterator.
         */
        const_iterator end() const;

    private:
        std::vector<EffectPass> elements_;
    };
}
