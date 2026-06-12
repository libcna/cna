// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// An indexed collection of EffectTechnique objects.
    class EffectTechniqueCollection
    {
    public:
        /// Constructs an empty EffectTechniqueCollection.
        EffectTechniqueCollection() = default;

        /// Gets the number of techniques in this collection.
        [[nodiscard]] int getCountProperty() const;
        /// Gets the technique at the specified index.
        [[nodiscard]] EffectTechnique& operator[](int index);
        /// Gets the technique at the specified index (const overload).
        [[nodiscard]] const EffectTechnique& operator[](int index) const;
        /// Gets the technique with the specified name, or nullptr if not found.
        [[nodiscard]] EffectTechnique* operator[](const std::string& name);
        /// Gets the technique with the specified name, or nullptr if not found (const overload).
        [[nodiscard]] const EffectTechnique* operator[](const std::string& name) const;

        /// Adds a technique to this collection.
        void Add(EffectTechnique technique);

        /// Iterator type for range-for support.
        using iterator = std::vector<EffectTechnique>::iterator;
        /// Const iterator type for range-for support.
        using const_iterator = std::vector<EffectTechnique>::const_iterator;
        /// Returns an iterator to the beginning of the collection.
        iterator begin();
        /// Returns an iterator to the end of the collection.
        iterator end();
        /// Returns a const iterator to the beginning of the collection.
        const_iterator begin() const;
        /// Returns a const iterator to the end of the collection.
        const_iterator end() const;

    private:
        std::vector<EffectTechnique> elements_;
    };
}
