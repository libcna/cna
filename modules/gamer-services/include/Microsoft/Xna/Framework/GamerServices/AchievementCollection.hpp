// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"
#include "System/IDisposable.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A disposable, indexed collection of Achievement objects.
     */
    class AchievementCollection : public System::IDisposable
    {
    public:
        /**
         * @brief Returns the number of achievements in the collection.
         *
         * @return The element count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the achievement at the specified index.
         *
         * @param index Zero-based index.
         * @return Const reference to the Achievement.
         * @throws System::ArgumentOutOfRangeException if index is out of range.
         */
        [[nodiscard]] const Achievement& operator[](int index) const;

        /**
         * @brief Gets the achievement with the specified key.
         *
         * @param achievementKey The key to search for.
         * @return Const reference to the matching Achievement.
         * @throws System::IndexOutOfRangeException if no achievement with that key exists.
         */
        [[nodiscard]] const Achievement& operator[](const std::string& achievementKey) const;

        /**
         * @brief Returns a C++ iterator to the beginning (for range-for).
         *
         * @return Begin iterator.
         */
        CNAEXT [[nodiscard]] auto begin() const { return collection_.begin(); }

        /**
         * @brief Returns a C++ iterator past the end (for range-for).
         *
         * @return End iterator.
         */
        CNAEXT [[nodiscard]] auto end() const { return collection_.end(); }

        /**
         * @brief Releases the achievements held by this collection.
         */
        void Dispose() override;

        /**
         * @brief Returns the index of the first achievement equal to item, or -1 if not found.
         *
         * Task 8.2: matches FNA's explicit `IList<Achievement>.IndexOf`, reachable in C# only
         * through an `IList<Achievement>` cast — exposed as an ordinary public method here
         * instead, since C++ has no equivalent of explicit interface implementation (a
         * documented, pragmatic mapping deviation, same as Task 8.1's `PropertyDictionary`
         * additions). Uses `Achievement::operator==` (structural equality), since FNA's own
         * reference-identity equality has no direct equivalent for by-value storage.
         *
         * @param item The achievement to locate.
         * @return The zero-based index, or -1 if not found.
         */
        [[nodiscard]] int IndexOf(const Achievement& item) const;

        /**
         * @brief Inserts item at the specified index.
         *
         * Task 8.2: matches FNA's explicit `IList<Achievement>.Insert`.
         *
         * @param index The zero-based index to insert at.
         * @param item The achievement to insert.
         */
        void Insert(int index, const Achievement& item);

        /**
         * @brief Removes the achievement at the specified index.
         *
         * Task 8.2: matches FNA's explicit `IList<Achievement>.RemoveAt`.
         *
         * @param index The zero-based index to remove.
         */
        void RemoveAt(int index);

        /**
         * @brief Appends item to the collection.
         *
         * Task 8.2: matches FNA's explicit `ICollection<Achievement>.Add`.
         *
         * @param item The achievement to append.
         */
        void Add(const Achievement& item);

        /**
         * @brief Removes the first achievement equal to item.
         *
         * Task 8.2: matches FNA's explicit `ICollection<Achievement>.Remove`.
         *
         * @param item The achievement to remove.
         * @return true if a matching achievement was found and removed; otherwise false.
         */
        bool Remove(const Achievement& item);

        /**
         * @brief Removes every achievement from the collection.
         *
         * Task 8.2: matches FNA's explicit `ICollection<Achievement>.Clear`.
         */
        void Clear();

        /**
         * @brief Determines whether the collection contains an achievement equal to item.
         *
         * Task 8.2: matches FNA's explicit `ICollection<Achievement>.Contains`.
         *
         * @param item The achievement to search for.
         * @return true if found; otherwise false.
         */
        [[nodiscard]] bool Contains(const Achievement& item) const;

        /**
         * @brief Copies every achievement into array, starting at arrayIndex.
         *
         * Task 8.2: matches FNA's explicit `ICollection<Achievement>.CopyTo`, which — unlike
         * `PropertyDictionary`'s own `CopyTo` stub (Task 8.1) — is a real, working
         * implementation in FNA (`collection.CopyTo(array, arrayIndex);`), not an
         * unimplemented one.
         *
         * @param array The destination vector; must already be large enough to hold
         * `arrayIndex + getCountProperty()` elements.
         * @param arrayIndex The zero-based index in array to begin writing at.
         * @throws System::ArgumentOutOfRangeException if arrayIndex is negative or array is too
         * small.
         */
        void CopyTo(std::vector<Achievement>& array, int arrayIndex) const;

        /**
         * @brief Gets whether this collection is read-only.
         *
         * Always true, matching FNA's own hardcoded `ICollection<Achievement>.IsReadOnly`
         * getter (the conventional .NET pattern for a collection whose explicit-interface
         * mutators exist only to satisfy the interface contract, not for ordinary callers to
         * rely on).
         *
         * @return Always true.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /** @brief Creates an AchievementCollection for CNA internal use. */
        CNAEXT static AchievementCollection CreateInternal(std::vector<Achievement> achievements);

    private:
        explicit AchievementCollection(std::vector<Achievement> achievements);

        std::vector<Achievement> collection_;
        bool isDisposed_{false};
    };
}
