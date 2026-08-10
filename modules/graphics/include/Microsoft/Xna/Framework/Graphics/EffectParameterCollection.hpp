// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An indexed, name-keyed collection of EffectParameter objects.
     *
     * Stores its elements behind `std::unique_ptr` (not by value) so that a previously
     * obtained `EffectParameter*`/`&` stays valid after a later Add(), even if the backing
     * vector of pointers itself reallocates. A by-value `std::vector<EffectParameter>` would
     * silently dangle such pointers on reallocation (the same hazard Task 355 fixed for
     * EffectTechniqueCollection).
     */
    class EffectParameterCollection
    {
    public:
        /**
         * @brief Forward, non-owning iterator over EffectParameter& (mutable overload).
         */
        class iterator
        {
        public:
            /** @brief Wraps the underlying storage iterator. */
            explicit iterator(std::vector<std::unique_ptr<EffectParameter>>::iterator it) : it_(it) {}
            /** @brief Dereferences to the referenced EffectParameter. */
            EffectParameter& operator*() const { return **it_; }
            /** @brief Member access on the referenced EffectParameter. */
            EffectParameter* operator->() const { return it_->get(); }
            /** @brief Advances to the next element. */
            iterator& operator++() { ++it_; return *this; }
            /** @brief Compares for inequality. */
            bool operator!=(const iterator& other) const { return it_ != other.it_; }
            /** @brief Compares for equality. */
            bool operator==(const iterator& other) const { return it_ == other.it_; }

        private:
            std::vector<std::unique_ptr<EffectParameter>>::iterator it_;
        };

        /**
         * @brief Forward, non-owning iterator over const EffectParameter& (const overload).
         */
        class const_iterator
        {
        public:
            /** @brief Wraps the underlying storage iterator. */
            explicit const_iterator(std::vector<std::unique_ptr<EffectParameter>>::const_iterator it) : it_(it) {}
            /** @brief Dereferences to the referenced EffectParameter. */
            const EffectParameter& operator*() const { return **it_; }
            /** @brief Member access on the referenced EffectParameter. */
            const EffectParameter* operator->() const { return it_->get(); }
            /** @brief Advances to the next element. */
            const_iterator& operator++() { ++it_; return *this; }
            /** @brief Compares for inequality. */
            bool operator!=(const const_iterator& other) const { return it_ != other.it_; }
            /** @brief Compares for equality. */
            bool operator==(const const_iterator& other) const { return it_ == other.it_; }

        private:
            std::vector<std::unique_ptr<EffectParameter>>::const_iterator it_;
        };

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
        CNAEXT void Add(EffectParameter param);

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

        /** @brief Returns a mutable iterator to the first parameter. */
        CNAEXT iterator begin();
        /** @brief Returns a mutable iterator past the last parameter. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the first parameter. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the last parameter. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<std::unique_ptr<EffectParameter>> elements_;
    };
}
