// SPDX-License-Identifier: MS-PL

#pragma once

#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief The collection of CurveKey elements and a part of the Curve class. */
    class CurveKeyCollection
    {
    public:
        /** @brief Underlying container type. */
        CNAEXT using container_type = std::vector<CurveKey>;
        /** @brief Mutable iterator over the key sequence. */
        CNAEXT using iterator = container_type::iterator;
        /** @brief Immutable iterator over the key sequence. */
        CNAEXT using const_iterator = container_type::const_iterator;

        /**
         * @brief Returns the count of keys in this collection.
         * @return The number of keys stored.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Returns false because this is not a read-only collection.
         * @return false unless the collection has been locked.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /**
         * @brief Gets a key by index.
         * @param index The zero-based index of the key.
         * @return A mutable reference to the key at the given index.
         */
        [[nodiscard]] CurveKey& getItemProperty(int index);

        /**
         * @brief Gets a key by index.
         * @param index The zero-based index of the key.
         * @return A const reference to the key at the given index.
         */
        [[nodiscard]] const CurveKey& getItemProperty(int index) const;

        /**
         * @brief Sets a key by index while preserving position ordering.
         * @param index The zero-based index of the key to replace.
         * @param value The new key value.
         */
        void setItemProperty(int index, const CurveKey& value);

        /**
         * @brief C++ indexer equivalent for the C# collection indexer.
         * @param index The zero-based index of the key.
         * @return A mutable reference to the key at the given index.
         */
        [[nodiscard]] CurveKey& operator[](int index);

        /**
         * @brief C++ indexer equivalent for the C# collection indexer.
         * @param index The zero-based index of the key.
         * @return A const reference to the key at the given index.
         */
        [[nodiscard]] const CurveKey& operator[](int index) const;

        /** @brief Creates an empty key collection. */
        CurveKeyCollection();

        /**
         * @brief Adds a key while preserving ascending position order.
         * @param item The key to add to the collection.
         */
        void Add(const CurveKey& item);

        /** @brief Removes all keys from the collection. */
        void Clear();

        /**
         * @brief Creates a copy of this collection.
         * @return A deep copy of this collection.
         */
        [[nodiscard]] CurveKeyCollection Clone() const;

        /**
         * @brief Returns true if the collection contains the given key.
         * @param item The key to locate.
         * @return true if the key is found; false otherwise.
         */
        [[nodiscard]] bool Contains(const CurveKey& item) const;

        /**
         * @brief Copies keys into an existing vector starting at the specified array index.
         * @param array The destination vector to copy into.
         * @param arrayIndex The zero-based index in the array to start copying from.
         */
        void CopyTo(std::vector<CurveKey>& array, int arrayIndex) const;

        /**
         * @brief Finds a key and returns its index, or -1 when it is not present.
         * @param item The key to locate.
         * @return The zero-based index of the key, or -1 if not found.
         */
        [[nodiscard]] int IndexOf(const CurveKey& item) const;

        /**
         * @brief Removes the first matching key.
         * @param item The key to remove.
         * @return true if the key was found and removed; false otherwise.
         */
        bool Remove(const CurveKey& item);

        /**
         * @brief Removes the key at the specified index.
         * @param index The zero-based index of the key to remove.
         */
        void RemoveAt(int index);

        /**
         * @brief Returns an iterator to the first key.
         * @return A mutable iterator pointing to the first key.
         */
        CNAEXT [[nodiscard]] iterator begin();

        /**
         * @brief Returns an iterator past the last key.
         * @return A mutable iterator one past the last key.
         */
        CNAEXT [[nodiscard]] iterator end();

        /**
         * @brief Returns a const iterator to the first key.
         * @return An immutable iterator pointing to the first key.
         */
        CNAEXT [[nodiscard]] const_iterator begin() const;

        /**
         * @brief Returns a const iterator past the last key.
         * @return An immutable iterator one past the last key.
         */
        CNAEXT [[nodiscard]] const_iterator end() const;

        /**
         * @brief Returns a const iterator to the first key.
         * @return An immutable iterator pointing to the first key.
         */
        CNAEXT [[nodiscard]] const_iterator cbegin() const;

        /**
         * @brief Returns a const iterator past the last key.
         * @return An immutable iterator one past the last key.
         */
        CNAEXT [[nodiscard]] const_iterator cend() const;

    private:
        bool isReadOnly;
        std::vector<CurveKey> innerlist;
    };
}
