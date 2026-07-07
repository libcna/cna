// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include <algorithm>
#include <vector>
#include <stdexcept>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A read-only collection of Gamer-derived objects.
     *
     * Task 10.2: ownership contract, stated once here and applied consistently by every
     * `GamerCollection<T>`-derived and `GamerCollection<T>`-adjacent type in both `GamerServices`
     * and `Net` (`SignedInGamerCollection`, `FriendCollection`, `AchievementCollection`-sibling
     * collections, `NetworkSession`'s `GamerCollection<NetworkGamer>` views, etc.):
     *
     * `GamerCollection<T>` and every type built on it is always a **non-owning view**. It stores
     * `T*` pointers, but never allocates one, and neither `Dispose()`/`Clear()` nor destruction
     * ever deletes one — matching FNA's own real `ReadOnlyCollection<T>`-based collections, which
     * rely entirely on .NET's GC for the pointed-to objects once nothing else references them.
     * The *creator* of a `Gamer`-derived object is exclusively responsible for freeing it, tracked
     * in its own separate registry, never through this view. Established precedent:
     * `NetworkSession::ownedGamers_` (owns locally-created `NetworkGamer`s),
     * `ENetBackend::SessionState::OwnedRemoteGamers` (owns remotely-created `NetworkGamer`s),
     * and `GamerServicesDispatcher::Initialize()`'s explicit free-before-replace loop (owns the 4
     * stub `SignedInGamer`s it creates). Any future real (non-stub) population of a
     * `GamerCollection<T>`-derived view (e.g. a real `FriendCollection` population from a live
     * friends-list service) must establish its own analogous ownership registry at the point of
     * creation — never make this view itself free anything.
     *
     * Task 10.5: this type has no virtual members and is **not** designed to be used
     * polymorphically through a base `GamerCollection<T>*`/`GamerCollection<T>&`. Every current
     * heap-allocated instance (`Gamer::signedInGamers_`, a `SignedInGamerCollection*`) is deleted
     * through its own concrete derived type, never through this base; every other instance
     * (`NetworkSession`'s `allGamers_`/`localGamers_`/`remoteGamers_`/`previousGamers_`,
     * `NetworkMachine::gamers_`) is a plain by-value member, destroyed via its own static type by
     * the owning object's destructor. If any future code ever needs to `delete` a derived
     * collection (e.g. `FriendCollection`, `SignedInGamerCollection`) through a
     * `GamerCollection<T>*` base pointer, this class needs a virtual destructor first — deleting
     * a derived object through a non-virtual base destructor is undefined behavior. Until that
     * need actually exists, deliberately not adding one, since it would add a vtable pointer to
     * every instance of this otherwise-trivial value type for no current benefit.
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
         * @brief Returns the index of the first occurrence of item, or -1 if not found.
         *
         * Task 8.3: FNA's real `GamerCollection<T>` derives from `ReadOnlyCollection<T>` and
         * inherits its `IndexOf`/`Contains`/`CopyTo` — this port's own `GamerCollection<T>`
         * lacked equivalents. Compares by pointer identity, which — unlike `Achievement`/
         * `LeaderboardEntry`'s own value-storage workaround — already matches FNA's real
         * reference-type equality semantics exactly for `Gamer`-derived types, since this
         * collection stores `T*` and native C++ pointer comparison already is
         * reference-identity comparison; no `operator==` needed.
         *
         * @param item The element to locate.
         * @return The zero-based index, or -1 if not found.
         */
        [[nodiscard]] int IndexOf(T* item) const
        {
            for (std::size_t i = 0; i < collection_.size(); ++i)
            {
                if (collection_[i] == item)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        /**
         * @brief Determines whether the collection contains item.
         *
         * @param item The element to search for.
         * @return true if found; otherwise false.
         */
        [[nodiscard]] bool Contains(T* item) const
        {
            return IndexOf(item) >= 0;
        }

        /**
         * @brief Copies every element into destination, starting at index.
         *
         * @param destination The destination vector; must already be large enough to hold
         * `index + getCountProperty()` elements.
         * @param index The zero-based index in destination to begin writing at.
         * @throws System::ArgumentOutOfRangeException if index is negative.
         * @throws System::ArgumentException if destination is too small.
         */
        void CopyTo(std::vector<T*>& destination, int index) const
        {
            System::ArgumentOutOfRangeException::ThrowIfNegative(index, "index");
            if (static_cast<int>(destination.size()) - index < static_cast<int>(collection_.size()))
            {
                throw System::ArgumentException(
                    "The number of elements in the source collection is greater than the "
                    "available space from index to the end of the destination array."
                );
            }
            std::copy(collection_.begin(), collection_.end(), destination.begin() + index);
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
