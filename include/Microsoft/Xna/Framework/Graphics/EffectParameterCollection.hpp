// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An indexed, name-keyed collection of EffectParameter objects.
     */
    class EffectParameterCollection
    {
    public:
        /** @brief Constructs an empty EffectParameterCollection. */
        EffectParameterCollection() = default;

        /**
         * @brief Gets the number of parameters in this collection.
         *
         * @return The parameter count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets the parameter at the specified index (mutable overload).
         *
         * @param index Zero-based index of the parameter.
         * @return Reference to the parameter.
         */
        [[nodiscard]] EffectParameter& operator[](int index);

        /**
         * @brief Gets the parameter at the specified index (const overload).
         *
         * @param index Zero-based index of the parameter.
         * @return Const reference to the parameter.
         */
        [[nodiscard]] const EffectParameter& operator[](int index) const;

        /**
         * @brief Gets the parameter with the specified name.
         *
         * @param name The parameter name to search for.
         * @return Pointer to the matching parameter, or nullptr if not found.
         */
        [[nodiscard]] EffectParameter* operator[](const std::string& name);

        /**
         * @brief Gets the parameter with the specified name (const overload).
         *
         * @param name The parameter name to search for.
         * @return Const pointer to the matching parameter, or nullptr if not found.
         */
        [[nodiscard]] const EffectParameter* operator[](const std::string& name) const;

        /**
         * @brief Adds a parameter to this collection.
         *
         * @param param The EffectParameter to add.
         */
        NOXNA void Add(EffectParameter param);

        /**
         * @brief Gets the first parameter whose semantic matches the given string.
         *
         * @param semantic The HLSL semantic string to search for.
         * @return Pointer to the matching parameter, or nullptr if not found.
         */
        [[nodiscard]] EffectParameter* GetParameterBySemantic(const std::string& semantic);

        /**
         * @brief Gets the first parameter whose semantic matches the given string (const overload).
         *
         * @param semantic The HLSL semantic string to search for.
         * @return Const pointer to the matching parameter, or nullptr if not found.
         */
        [[nodiscard]] const EffectParameter* GetParameterBySemantic(const std::string& semantic) const;

        /** @brief Mutable iterator type for range-for support. */
        using iterator = std::vector<EffectParameter>::iterator;
        /** @brief Const iterator type for range-for support. */
        using const_iterator = std::vector<EffectParameter>::const_iterator;

        /** @brief Returns a mutable iterator to the first parameter. */
        NOXNA iterator begin();
        /** @brief Returns a mutable iterator past the last parameter. */
        NOXNA iterator end();
        /** @brief Returns a const iterator to the first parameter. */
        NOXNA const_iterator begin() const;
        /** @brief Returns a const iterator past the last parameter. */
        NOXNA const_iterator end() const;

    private:
        std::vector<EffectParameter> elements_;
    };
}
