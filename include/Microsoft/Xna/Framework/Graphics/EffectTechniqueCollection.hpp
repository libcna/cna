// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An indexed, name-keyed collection of EffectTechnique objects.
     */
    class EffectTechniqueCollection
    {
    public:
        /** @brief Constructs an empty EffectTechniqueCollection. */
        EffectTechniqueCollection() = default;

        /**
         * @brief Gets the number of techniques in this collection.
         *
         * @return The technique count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets the technique at the specified index (mutable overload).
         *
         * @param index Zero-based index of the technique.
         * @return Reference to the technique.
         */
        [[nodiscard]] EffectTechnique& operator[](int index);

        /**
         * @brief Gets the technique at the specified index (const overload).
         *
         * @param index Zero-based index of the technique.
         * @return Const reference to the technique.
         */
        [[nodiscard]] const EffectTechnique& operator[](int index) const;

        /**
         * @brief Gets the technique with the specified name.
         *
         * @param name The technique name to search for.
         * @return Pointer to the matching technique, or nullptr if not found.
         */
        [[nodiscard]] EffectTechnique* operator[](const std::string& name);

        /**
         * @brief Gets the technique with the specified name (const overload).
         *
         * @param name The technique name to search for.
         * @return Const pointer to the matching technique, or nullptr if not found.
         */
        [[nodiscard]] const EffectTechnique* operator[](const std::string& name) const;

        /**
         * @brief Adds a technique to this collection.
         *
         * @param technique The EffectTechnique to add.
         */
        void Add(EffectTechnique technique);

        /** @brief Mutable iterator type for range-for support. */
        using iterator = std::vector<EffectTechnique>::iterator;
        /** @brief Const iterator type for range-for support. */
        using const_iterator = std::vector<EffectTechnique>::const_iterator;

        /** @brief Returns a mutable iterator to the first technique. */
        NOXNA iterator begin();
        /** @brief Returns a mutable iterator past the last technique. */
        NOXNA iterator end();
        /** @brief Returns a const iterator to the first technique. */
        NOXNA const_iterator begin() const;
        /** @brief Returns a const iterator past the last technique. */
        NOXNA const_iterator end() const;

    private:
        std::vector<EffectTechnique> elements_;
    };
}
