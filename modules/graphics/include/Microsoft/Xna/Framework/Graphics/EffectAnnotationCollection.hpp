// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>
#include <memory>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An indexed, name-keyed collection of EffectAnnotation objects.
     */
    class EffectAnnotationCollection
    {
    public:
        /** @brief Constructs an empty EffectAnnotationCollection. */
        EffectAnnotationCollection() = default;

        /**
         * @brief Gets the number of annotations in this collection.
         *
         * @return The annotation count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets the annotation at the specified index (mutable overload).
         *
         * @param index Zero-based index of the annotation.
         * @return Reference to the annotation.
         */
        [[nodiscard]] EffectAnnotation& operator[](int index);

        /**
         * @brief Gets the annotation at the specified index (const overload).
         *
         * @param index Zero-based index of the annotation.
         * @return Const reference to the annotation.
         */
        [[nodiscard]] const EffectAnnotation& operator[](int index) const;

        /**
         * @brief Gets the annotation with the specified name.
         *
         * @param name The annotation name to search for.
         * @return Pointer to the matching annotation, or nullptr if not found.
         */
        [[nodiscard]] EffectAnnotation* operator[](const std::string& name);

        /**
         * @brief Gets the annotation with the specified name (const overload).
         *
         * @param name The annotation name to search for.
         * @return Const pointer to the matching annotation, or nullptr if not found.
         */
        [[nodiscard]] const EffectAnnotation* operator[](const std::string& name) const;

        /**
         * @brief Adds an annotation to this collection.
         *
         * @param annotation The EffectAnnotation to add.
         */
        CNAEXT void Add(EffectAnnotation annotation);

        /** @brief Mutable iterator type for range-for support. */
        using iterator = std::vector<EffectAnnotation>::iterator;
        /** @brief Const iterator type for range-for support. */
        using const_iterator = std::vector<EffectAnnotation>::const_iterator;

        /**
         * @brief Returns a mutable iterator to the first annotation.
         *
         * @return Begin iterator.
         */
        /** @brief Returns a mutable iterator to the first annotation. */
        CNAEXT iterator begin();
        /** @brief Returns a mutable iterator past the last annotation. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the first annotation. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the last annotation. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<EffectAnnotation> elements_;
    };
}
