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
     * @brief A disposable, read-only indexed collection of Achievement objects.
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
         * @throws System::Collections::Generic::KeyNotFoundException if no achievement has that key.
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
         * @brief Refuses insertion into this read-only collection.
         *
         * @param index The zero-based index to insert at.
         * @param item The achievement to insert.
         * @throws System::NotSupportedException always, before validating the index.
         */
        void Insert(int index, const Achievement& item);

        /**
         * @brief Refuses indexed removal from this read-only collection.
         *
         * @param index The zero-based index to remove.
         * @throws System::NotSupportedException always, before validating the index.
         */
        void RemoveAt(int index);

        /**
         * @brief Refuses addition to this read-only collection.
         *
         * @param item The achievement to append.
         * @throws System::NotSupportedException always.
         */
        void Add(const Achievement& item);

        /**
         * @brief Refuses removal from this read-only collection.
         *
         * @param item The achievement to remove.
         * @return Never returns normally.
         * @throws System::NotSupportedException always, even when item is absent.
         */
        bool Remove(const Achievement& item);

        /**
         * @brief Refuses clearing this read-only collection.
         * @throws System::NotSupportedException always, even when empty.
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
         * The ICollection/IList mutation methods always throw NotSupportedException.
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
