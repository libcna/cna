// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"

#include <cstddef>
#include <vector>

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Provides an immutable snapshot of current touch locations.
     */
    struct TouchCollection
    {
        /**
         * @brief Gets the number of touch locations in this collection.
         * @return The touch location count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets whether a touch device is connected.
         * @return True if connected; false otherwise.
         */
        [[nodiscard]] bool getIsConnectedProperty() const;

        /**
         * @brief Returns whether the collection reports itself read-only. Always true, matching
         *        XNA/FNA's `TouchCollection.IsReadOnly` (hard-coded to `true`).
         *
         * @note This flag is advisory, exactly as in FNA: FNA's `IsReadOnly` getter returns `true`,
         *       yet its `Add`/`Clear`/`Insert`/`Remove`/`RemoveAt` methods still mutate the backing
         *       `List<TouchLocation>` whenever it is non-null (i.e. the state returned by
         *       `TouchPanel::GetState`). CNA is faithful to that — this getter returns `true` while
         *       the mutation methods below actually succeed. The one unavoidable C++ deviation is the
         *       default-constructed collection: FNA leaves its backing list `null` so mutating it
         *       throws `NullReferenceException`; CNA's backing is a value `std::vector`, so a default
         *       collection is simply empty and mutable instead of null-and-throwing.
         * @return Always true.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /**
         * @brief Constructs an empty touch collection.
         * @note CNAEXT — FNA's `TouchCollection` has no explicit parameterless constructor
         *       (only the one taking a touch array).
         */
        CNAEXT TouchCollection();

        /**
         * @brief Constructs from a vector of touch locations.
         * @param touches The touch locations to include.
         */
        explicit TouchCollection(const std::vector<TouchLocation>& touches);

        /**
         * @brief Constructs by moving a vector of touch locations.
         * @param touches The touch locations to move in.
         */
        explicit TouchCollection(std::vector<TouchLocation>&& touches);

        /**
         * @brief Returns the touch location at the given index (mutable overload).
         *
         * Mirrors FNA's settable `this[int]` indexer; since the collection is never
         * actually read-only in this implementation (unlike FNA's default-constructed,
         * null-backed struct), assignment is never blocked purely because IsReadOnly is
         * true. Assignment still requires an in-range index — out-of-range access throws
         * std::out_of_range, matching FNA's indexer throwing for a bad/null-backed index.
         *
         * @param index The zero-based index to retrieve.
         * @return A reference to the touch location.
         */
        [[nodiscard]] TouchLocation& operator[](std::size_t index);

        /**
         * @brief Returns the touch location at the given index.
         * @param index The zero-based index to retrieve.
         * @return A const reference to the touch location.
         */
        [[nodiscard]] const TouchLocation& operator[](std::size_t index) const;

        /**
         * @brief Returns true if the collection has no touch locations.
         * @note CNAEXT STL-ergonomics helper; not part of the XNA `TouchCollection` API.
         * @return True if empty; false otherwise.
         */
        CNAEXT [[nodiscard]] bool empty() const;

        /**
         * @brief Returns true if the collection contains the given touch location.
         * @param item The touch location to search for.
         * @return True if found; false otherwise.
         */
        [[nodiscard]] bool Contains(const TouchLocation& item) const;

        /**
         * @brief Tries to find a touch by finger id. Returns false if not found.
         *
         * @note Matches FNA: `touchLocation` is written unconditionally, on every path — not just
         *       when a match is found. When no match exists (including when the collection is
         *       empty), `touchLocation` is set to `TouchLocation(-1, TouchLocationState::Invalid,
         *       Vector2::Zero)` before returning false.
         * @param id The finger id to search for.
         * @param touchLocation Output parameter receiving the found location, or the Invalid
         *        sentinel location if no match was found.
         * @return True if found; false otherwise.
         */
        bool FindById(int id, TouchLocation& touchLocation) const;

        /**
         * @brief Copies touch locations into a vector starting at the given index.
         * @param array The destination vector.
         * @param arrayIndex The starting index in the destination vector.
         */
        void CopyTo(std::vector<TouchLocation>& array, int arrayIndex) const;

        /**
         * @brief Returns the index of the given touch location in this collection.
         * @param item The touch location to search for.
         * @return The index, or -1 if not found.
         */
        [[nodiscard]] int IndexOf(const TouchLocation& item) const;

        /**
         * @brief Adds a touch location to this collection.
         * @param item The touch location to add.
         */
        void Add(const TouchLocation& item);

        /** @brief Removes all touch locations from this collection. */
        void Clear();

        /**
         * @brief Removes the given touch location from this collection.
         * @param item The touch location to remove.
         * @return True if removed; false if not found.
         */
        bool Remove(const TouchLocation& item);

        /**
         * @brief Removes the touch location at the given index.
         * @param index The zero-based index to remove.
         */
        void RemoveAt(int index);

        /**
         * @brief Inserts a touch location at the given index.
         * @param index The zero-based index to insert at.
         * @param item The touch location to insert.
         */
        void Insert(int index, const TouchLocation& item);

        /**
         * @brief Returns a mutable iterator to the beginning of the collection.
         * @note CNAEXT — replaces FNA's `IEnumerable<TouchLocation>::GetEnumerator()`.
         */
        CNAEXT std::vector<TouchLocation>::iterator begin();
        /** @brief Returns a mutable iterator past the end of the collection. */
        CNAEXT std::vector<TouchLocation>::iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        CNAEXT [[nodiscard]] std::vector<TouchLocation>::const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT [[nodiscard]] std::vector<TouchLocation>::const_iterator end() const;

    private:
        std::vector<TouchLocation> touches_;
    };
}
