// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// An indexed collection of EffectParameter objects.
    class EffectParameterCollection
    {
    public:
        /// Constructs an empty EffectParameterCollection.
        EffectParameterCollection() = default;

        /// Gets the number of parameters in this collection.
        [[nodiscard]] int getCountProperty() const;
        /// Gets the parameter at the specified index.
        [[nodiscard]] EffectParameter& operator[](int index);
        /// Gets the parameter at the specified index (const overload).
        [[nodiscard]] const EffectParameter& operator[](int index) const;
        /// Gets the parameter with the specified name, or nullptr if not found.
        [[nodiscard]] EffectParameter* operator[](const std::string& name);
        /// Gets the parameter with the specified name, or nullptr if not found (const overload).
        [[nodiscard]] const EffectParameter* operator[](const std::string& name) const;

        /// Adds a parameter to this collection.
        void Add(EffectParameter param);

        /// Iterator type for range-for support.
        using iterator = std::vector<EffectParameter>::iterator;
        /// Const iterator type for range-for support.
        using const_iterator = std::vector<EffectParameter>::const_iterator;
        /// Returns an iterator to the beginning of the collection.
        iterator begin();
        /// Returns an iterator to the end of the collection.
        iterator end();
        /// Returns a const iterator to the beginning of the collection.
        const_iterator begin() const;
        /// Returns a const iterator to the end of the collection.
        const_iterator end() const;

    private:
        std::vector<EffectParameter> elements_;
    };
}
