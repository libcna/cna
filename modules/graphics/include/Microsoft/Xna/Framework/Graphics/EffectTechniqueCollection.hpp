// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An indexed, name-keyed collection of EffectTechnique objects.
     *
     * Stores its elements behind `std::unique_ptr` (not by value) so that a previously
     * obtained `EffectTechnique*`/`&` — including Effect::CurrentTechnique, which is captured
     * at construction time — stays valid after a later Add(), even if the backing vector of
     * pointers itself reallocates. A by-value `std::vector<EffectTechnique>` would silently
     * dangle such pointers on reallocation.
     */
    class EffectTechniqueCollection
    {
    public:
        /**
         * @brief Forward, non-owning iterator over EffectTechnique& (mutable overload).
         */
        class iterator
        {
        public:
            /** @brief Wraps the underlying storage iterator. */
            explicit iterator(std::vector<std::unique_ptr<EffectTechnique>>::iterator it) : it_(it) {}
            /** @brief Dereferences to the referenced EffectTechnique. */
            EffectTechnique& operator*() const { return **it_; }
            /** @brief Member access on the referenced EffectTechnique. */
            EffectTechnique* operator->() const { return it_->get(); }
            /** @brief Advances to the next element. */
            iterator& operator++() { ++it_; return *this; }
            /** @brief Compares for inequality. */
            bool operator!=(const iterator& other) const { return it_ != other.it_; }
            /** @brief Compares for equality. */
            bool operator==(const iterator& other) const { return it_ == other.it_; }

        private:
            std::vector<std::unique_ptr<EffectTechnique>>::iterator it_;
        };

        /**
         * @brief Forward, non-owning iterator over const EffectTechnique& (const overload).
         */
        class const_iterator
        {
        public:
            /** @brief Wraps the underlying storage iterator. */
            explicit const_iterator(std::vector<std::unique_ptr<EffectTechnique>>::const_iterator it) : it_(it) {}
            /** @brief Dereferences to the referenced EffectTechnique. */
            const EffectTechnique& operator*() const { return **it_; }
            /** @brief Member access on the referenced EffectTechnique. */
            const EffectTechnique* operator->() const { return it_->get(); }
            /** @brief Advances to the next element. */
            const_iterator& operator++() { ++it_; return *this; }
            /** @brief Compares for inequality. */
            bool operator!=(const const_iterator& other) const { return it_ != other.it_; }
            /** @brief Compares for equality. */
            bool operator==(const const_iterator& other) const { return it_ == other.it_; }

        private:
            std::vector<std::unique_ptr<EffectTechnique>>::const_iterator it_;
        };

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
        CNAEXT void Add(EffectTechnique technique);

        /** @brief Returns a mutable iterator to the first technique. */
        CNAEXT iterator begin();
        /** @brief Returns a mutable iterator past the last technique. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the first technique. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the last technique. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<std::unique_ptr<EffectTechnique>> elements_;
    };
}
