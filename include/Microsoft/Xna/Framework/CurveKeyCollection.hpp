// SPDX-License-Identifier: MS-PL

#pragma once

#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

namespace Microsoft::Xna::Framework
{
    /// Collection of CurveKey elements used by Curve.
    class CurveKeyCollection
    {
    public:
        /// Underlying container type.
        NOXNA using container_type = std::vector<CurveKey>;
        /// Mutable iterator over the key sequence.
        NOXNA using iterator = container_type::iterator;
        /// Immutable iterator over the key sequence.
        NOXNA using const_iterator = container_type::const_iterator;

        /// Gets the number of keys in the collection.
        [[nodiscard]] int getCountProperty() const;

        /// Gets whether the collection is read-only.
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /// Gets a key by index.
        [[nodiscard]] CurveKey& getItemProperty(int index);
        [[nodiscard]] const CurveKey& getItemProperty(int index) const;

        /// Sets a key by index while preserving position ordering.
        void setItemProperty(int index, const CurveKey& value);

        /// C++ indexer equivalent for the C# collection indexer.
        [[nodiscard]] CurveKey& operator[](int index);
        [[nodiscard]] const CurveKey& operator[](int index) const;

        /// Creates an empty key collection.
        CurveKeyCollection();

        /// Adds a key while preserving ascending position order.
        void Add(const CurveKey& item);

        /// Removes all keys from the collection.
        void Clear();

        /// Creates a copy of this collection.
        [[nodiscard]] CurveKeyCollection Clone() const;

        /// Returns true if the collection contains the given key.
        [[nodiscard]] bool Contains(const CurveKey& item) const;

        /// Copies keys into an existing vector starting at the specified array index.
        void CopyTo(std::vector<CurveKey>& array, int arrayIndex) const;

        /// Finds a key and returns its index, or -1 when it is not present.
        [[nodiscard]] int IndexOf(const CurveKey& item) const;

        /// Removes the first matching key.
        bool Remove(const CurveKey& item);

        /// Removes the key at the specified index.
        void RemoveAt(int index);

        /// Returns an iterator to the first key.
        NOXNA [[nodiscard]] iterator begin();
        /// Returns an iterator past the last key.
        NOXNA [[nodiscard]] iterator end();
        /// Returns a const iterator to the first key.
        NOXNA [[nodiscard]] const_iterator begin() const;
        /// Returns a const iterator past the last key.
        NOXNA [[nodiscard]] const_iterator end() const;
        /// Returns a const iterator to the first key.
        NOXNA [[nodiscard]] const_iterator cbegin() const;
        /// Returns a const iterator past the last key.
        NOXNA [[nodiscard]] const_iterator cend() const;

    private:
        bool isReadOnly;
        std::vector<CurveKey> innerlist;
    };
}
