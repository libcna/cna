// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include <vector>
#include <stdexcept>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A read-only collection of Gamer-derived objects.
     *
     * @tparam T A type derived from Gamer.
     */
    template<typename T>
    class GamerCollection
    {
    public:
        /**
         * @brief A forward iterator over the collection.
         */
        struct GamerCollectionEnumerator
        {
            /**
             * @brief Gets the element at the current position.
             *
             * @return Pointer to the current element.
             */
            [[nodiscard]] T* getCurrent() const
            {
                return (*collection_)[static_cast<std::size_t>(position_)];
            }

            /**
             * @brief Advances the enumerator to the next element.
             *
             * @return true if there is a next element; otherwise false.
             */
            bool MoveNext()
            {
                ++position_;
                return position_ < static_cast<int>(collection_->size());
            }

            /** @brief Resets the enumerator to before the first element. */
            void Reset() { position_ = -1; }

            /** @brief Releases enumerator resources. */
            void Dispose() { collection_ = nullptr; }

            NOXNA GamerCollectionEnumerator(const std::vector<T*>* coll, int pos)
                : collection_(coll), position_(pos) {}

        private:
            const std::vector<T*>* collection_;
            int position_;
        };

        /**
         * @brief Returns the number of elements in the collection.
         *
         * @return The element count.
         */
        [[nodiscard]] int getCountProperty() const
        {
            return static_cast<int>(collection_.size());
        }

        /**
         * @brief Gets the element at the specified index.
         *
         * @param index Zero-based index.
         * @return Pointer to the element.
         */
        [[nodiscard]] T* operator[](int index) const
        {
            return collection_.at(static_cast<std::size_t>(index));
        }

        /**
         * @brief Returns a GamerCollectionEnumerator positioned before the first element.
         *
         * @return A new enumerator.
         */
        [[nodiscard]] GamerCollectionEnumerator GetEnumerator() const
        {
            return GamerCollectionEnumerator(&collection_, -1);
        }

        /** @brief Returns a C++ iterator to the beginning (for range-for). */
        NOXNA [[nodiscard]] auto begin() const { return collection_.begin(); }
        /** @brief Returns a C++ iterator past the end (for range-for). */
        NOXNA [[nodiscard]] auto end()   const { return collection_.end(); }

    protected:
        explicit GamerCollection(std::vector<T*> items)
            : collection_(std::move(items))
        {
        }

        std::vector<T*> collection_;
    };
}
