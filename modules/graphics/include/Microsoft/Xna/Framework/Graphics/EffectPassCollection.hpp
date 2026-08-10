// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief An indexed, name-keyed collection of EffectPass objects within a technique.
     *
     * Stores its elements behind `std::unique_ptr` (not by value) so that a previously
     * obtained `EffectPass*`/`&` stays valid after a later Add(), even if the backing
     * vector of pointers itself reallocates. A by-value `std::vector<EffectPass>` would
     * silently dangle such pointers on reallocation (the same hazard Task 355 fixed for
     * EffectTechniqueCollection).
     */
    class EffectPassCollection
    {
    public:
        /**
         * @brief Forward, non-owning iterator over EffectPass& (mutable overload).
         */
        class iterator
        {
        public:
            /** @brief Wraps the underlying storage iterator. */
            explicit iterator(std::vector<std::unique_ptr<EffectPass>>::iterator it) : it_(it) {}
            /** @brief Dereferences to the referenced EffectPass. */
            EffectPass& operator*() const { return **it_; }
            /** @brief Member access on the referenced EffectPass. */
            EffectPass* operator->() const { return it_->get(); }
            /** @brief Advances to the next element. */
            iterator& operator++() { ++it_; return *this; }
            /** @brief Compares for inequality. */
            bool operator!=(const iterator& other) const { return it_ != other.it_; }
            /** @brief Compares for equality. */
            bool operator==(const iterator& other) const { return it_ == other.it_; }

        private:
            std::vector<std::unique_ptr<EffectPass>>::iterator it_;
        };

        /**
         * @brief Forward, non-owning iterator over const EffectPass& (const overload).
         */
        class const_iterator
        {
        public:
            /** @brief Wraps the underlying storage iterator. */
            explicit const_iterator(std::vector<std::unique_ptr<EffectPass>>::const_iterator it) : it_(it) {}
            /** @brief Dereferences to the referenced EffectPass. */
            const EffectPass& operator*() const { return **it_; }
            /** @brief Member access on the referenced EffectPass. */
            const EffectPass* operator->() const { return it_->get(); }
            /** @brief Advances to the next element. */
            const_iterator& operator++() { ++it_; return *this; }
            /** @brief Compares for inequality. */
            bool operator!=(const const_iterator& other) const { return it_ != other.it_; }
            /** @brief Compares for equality. */
            bool operator==(const const_iterator& other) const { return it_ == other.it_; }

        private:
            std::vector<std::unique_ptr<EffectPass>>::const_iterator it_;
        };

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
        NOXNA void Add(EffectPass pass);

        /**
         * @brief Returns a mutable iterator to the first pass.
         *
         * @return Begin iterator.
         */
        NOXNA iterator begin();

        /**
         * @brief Returns a mutable iterator past the last pass.
         *
         * @return End iterator.
         */
        NOXNA iterator end();

        /**
         * @brief Returns a const iterator to the first pass.
         *
         * @return Const begin iterator.
         */
        NOXNA const_iterator begin() const;

        /**
         * @brief Returns a const iterator past the last pass.
         *
         * @return Const end iterator.
         */
        NOXNA const_iterator end() const;

    private:
        std::vector<std::unique_ptr<EffectPass>> elements_;
    };
}
