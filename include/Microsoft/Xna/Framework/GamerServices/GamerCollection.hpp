// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include <algorithm>
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
             * @throws System::ArgumentOutOfRangeException if called before the first MoveNext(),
             * after enumeration has run past the end, or after Dispose().
             */
            [[nodiscard]] T* getCurrent() const
            {
                // Task 7.8: raw std::vector::operator[] on an unvalidated position_ was real
                // undefined behavior for position_ == -1 (the pre-MoveNext() starting value,
                // casting to a huge std::size_t) or past the end - FNA's own equivalent
                // (`collection[position]`, via ReadOnlyCollection<T>'s indexer -> List<T>'s own
                // indexer) throws a catchable ArgumentOutOfRangeException in both cases instead.
                // Also guards the post-Dispose() case (collection_ set to nullptr), which would
                // otherwise be a null-pointer dereference.
                if (collection_ == nullptr
                    || position_ < 0
                    || position_ >= static_cast<int>(collection_->size()))
                {
                    throw System::ArgumentOutOfRangeException("position");
                }
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

            /**
             * @brief Constructs an enumerator over coll, positioned at pos.
             *
             * @param coll The collection to enumerate.
             * @param pos The zero-based starting position (typically -1, before the first element).
             */
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
         * @throws System::ArgumentOutOfRangeException if index is out of range.
         */
        [[nodiscard]] T* operator[](int index) const
        {
            // Task 7.9: FNA's own int indexer (ReadOnlyCollection<T> -> List<T>) throws
            // ArgumentOutOfRangeException, not std::out_of_range - use ThrowIfNegative/
            // ThrowIfGreaterThanOrEqual for the matching sharp-runtime exception type instead of
            // relying on std::vector::at()'s own (differently-typed) exception.
            System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
            System::ArgumentOutOfRangeException::ThrowIfGreaterThanOrEqual(
                index, static_cast<int>(collection_.size()), "index"
            );
            return collection_[static_cast<std::size_t>(index)];
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

        /**
         * @brief Creates a plain GamerCollection<T> for CNA internal use.
         *
         * FNA's GamerCollection<T> constructor is `internal` (same-assembly), so callers
         * outside the GamerServices/Net namespaces that aren't a named subclass (e.g.
         * NetworkMachine.Gamers, typed as a bare GamerCollection<NetworkGamer>) can still
         * construct one directly in FNA. The C++ port's constructor is `protected` instead,
         * so this factory restores that same-library-wide construction ability.
         *
         * @param items The elements to store.
         * @return A new GamerCollection<T> wrapping items.
         */
        NOXNA static GamerCollection<T> CreateInternal(std::vector<T*> items)
        {
            return GamerCollection<T>(std::move(items));
        }

        /**
         * @brief Appends an item to the collection.
         *
         * FNA's GamerCollection<T>.collection field is `internal` (same-assembly-mutable) —
         * NetworkSession.AddLocalGamer mutates a sibling class's collection directly through
         * it. The C++ port's collection_ is `protected` (subclass-only); this restores that
         * same-library mutation access.
         *
         * @param item The element to append.
         */
        NOXNA void Add(T* item)
        {
            collection_.push_back(item);
        }

        /**
         * @brief Removes the first occurrence of item from the collection.
         *
         * Restores the same same-library mutation access as Add(), for the same reason
         * (NetworkSession removing a gamer from a sibling class's collection).
         *
         * @param item The element to remove.
         */
        NOXNA void Remove(T* item)
        {
            collection_.erase(std::remove(collection_.begin(), collection_.end(), item), collection_.end());
        }

    protected:
        explicit GamerCollection(std::vector<T*> items)
            : collection_(std::move(items))
        {
        }

        std::vector<T*> collection_;
    };
}
