// SPDX-License-Identifier: MS-PL
#pragma once

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
         * @brief Always returns true — this collection does not support mutation.
         * @return Always true.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /** @brief Constructs an empty touch collection. */
        TouchCollection();

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
         * @brief Returns the touch location at the given index.
         * @param index The zero-based index to retrieve.
         * @return A const reference to the touch location.
         */
        [[nodiscard]] const TouchLocation& operator[](std::size_t index) const;

        /**
         * @brief Returns true if the collection has no touch locations.
         * @return True if empty; false otherwise.
         */
        [[nodiscard]] bool empty() const;

        /**
         * @brief Returns true if the collection contains the given touch location.
         * @param item The touch location to search for.
         * @return True if found; false otherwise.
         */
        [[nodiscard]] bool Contains(const TouchLocation& item) const;

        /**
         * @brief Tries to find a touch by finger id. Returns false if not found.
         * @param id The finger id to search for.
         * @param touchLocation Output parameter receiving the found location.
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

        /** @brief Returns an iterator to the beginning of the collection. */
        std::vector<TouchLocation>::iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        std::vector<TouchLocation>::iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        [[nodiscard]] std::vector<TouchLocation>::const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        [[nodiscard]] std::vector<TouchLocation>::const_iterator end() const;

    private:
        std::vector<TouchLocation> touches_;
    };
}
