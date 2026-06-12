// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>
#include <memory>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// An indexed collection of EffectAnnotation objects.
    class EffectAnnotationCollection
    {
    public:
        /// Constructs an empty EffectAnnotationCollection.
        EffectAnnotationCollection() = default;

        /// Gets the number of annotations in this collection.
        [[nodiscard]] int getCountProperty() const;
        /// Gets the annotation at the specified index.
        [[nodiscard]] EffectAnnotation& operator[](int index);
        /// Gets the annotation at the specified index (const overload).
        [[nodiscard]] const EffectAnnotation& operator[](int index) const;
        /// Gets the annotation with the specified name, or nullptr if not found.
        [[nodiscard]] EffectAnnotation* operator[](const std::string& name);
        /// Gets the annotation with the specified name, or nullptr if not found (const overload).
        [[nodiscard]] const EffectAnnotation* operator[](const std::string& name) const;

        /// Adds an annotation to this collection.
        void Add(EffectAnnotation annotation);

        /// Iterator type for range-for support.
        using iterator = std::vector<EffectAnnotation>::iterator;
        /// Const iterator type for range-for support.
        using const_iterator = std::vector<EffectAnnotation>::const_iterator;
        /// Returns an iterator to the beginning of the collection.
        iterator begin();
        /// Returns an iterator to the end of the collection.
        iterator end();
        /// Returns a const iterator to the beginning of the collection.
        const_iterator begin() const;
        /// Returns a const iterator to the end of the collection.
        const_iterator end() const;

    private:
        std::vector<EffectAnnotation> elements_;
    };
}
