// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// An indexed collection of EffectPass objects within a technique.
    class EffectPassCollection
    {
    public:
        /// Constructs an empty EffectPassCollection.
        EffectPassCollection() = default;

        /// Gets the number of passes in this collection.
        [[nodiscard]] int getCountProperty() const;
        /// Gets the pass at the specified index.
        [[nodiscard]] EffectPass& operator[](int index);
        /// Gets the pass at the specified index (const overload).
        [[nodiscard]] const EffectPass& operator[](int index) const;
        /// Gets the pass with the specified name, or nullptr if not found.
        [[nodiscard]] EffectPass* operator[](const std::string& name);
        /// Gets the pass with the specified name, or nullptr if not found (const overload).
        [[nodiscard]] const EffectPass* operator[](const std::string& name) const;

        /// Adds a pass to this collection.
        void Add(EffectPass pass);

        /// Iterator type for range-for support.
        using iterator = std::vector<EffectPass>::iterator;
        /// Const iterator type for range-for support.
        using const_iterator = std::vector<EffectPass>::const_iterator;
        /// Returns an iterator to the beginning of the collection.
        iterator begin();
        /// Returns an iterator to the end of the collection.
        iterator end();
        /// Returns a const iterator to the beginning of the collection.
        const_iterator begin() const;
        /// Returns a const iterator to the end of the collection.
        const_iterator end() const;

    private:
        std::vector<EffectPass> elements_;
    };
}
