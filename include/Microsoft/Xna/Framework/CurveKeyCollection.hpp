#pragma once

#include <vector>

#include "Microsoft/Xna/Framework/CurveKey.hpp"

namespace Microsoft::Xna::Framework
{
    /// Ordered collection of CurveKey values used by Curve.
    class CurveKeyCollection
    {
    public:
        using container_type = std::vector<CurveKey>;
        using iterator = container_type::iterator;
        using const_iterator = container_type::const_iterator;

        /// Creates an empty key collection.
        CurveKeyCollection();

        /// Returns the number of keys in the collection.
        [[nodiscard]] int Count() const;
        [[nodiscard]] int getCountProperty() const;

        /// Returns whether the collection is read-only.
        [[nodiscard]] bool IsReadOnly() const;
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /// Accesses a key by index.
        [[nodiscard]] CurveKey& operator[](int index);
        [[nodiscard]] const CurveKey& operator[](int index) const;

        /// Replaces a key and keeps the collection ordered by position.
        void Set(int index, const CurveKey& value);

        /// Adds a key while preserving ascending Position order.
        void Add(const CurveKey& item);

        /// Removes all keys.
        void Clear();

        /// Creates a copy of this collection.
        [[nodiscard]] CurveKeyCollection Clone() const;

        /// Returns true if the collection contains the given key.
        [[nodiscard]] bool Contains(const CurveKey& item) const;

        /// Copies keys into an existing vector starting at arrayIndex.
        void CopyTo(std::vector<CurveKey>& array, int arrayIndex) const;

        /// Returns the index of the key, or -1 if it is not present.
        [[nodiscard]] int IndexOf(const CurveKey& item) const;

        /// Removes the first matching key.
        bool Remove(const CurveKey& item);

        /// Removes the key at the specified index.
        void RemoveAt(int index);

        [[nodiscard]] iterator begin();
        [[nodiscard]] iterator end();
        [[nodiscard]] const_iterator begin() const;
        [[nodiscard]] const_iterator end() const;
        [[nodiscard]] const_iterator cbegin() const;
        [[nodiscard]] const_iterator cend() const;

    private:
        bool isReadOnly;
        std::vector<CurveKey> innerlist;
    };
}
