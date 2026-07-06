// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/Collections/Generic/IEnumerator.hpp"
#include "System/Collections/Generic/IList.hpp"
#include <optional>
#include <vector>

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief A growable list of optional integer session properties, indexable by position.
     *
     * C#'s explicit interface implementation of IList<int?>/ICollection<int?>/IEnumerable<int?>
     * hides most of these members from the concrete type unless accessed through an interface
     * reference; C++ has no equivalent mechanism, so all interface members below are directly
     * public on this type.
     */
    class NetworkSessionProperties : public System::Collections::Generic::IList<std::optional<int>>
    {
    public:
        /** @brief Initializes a new, empty instance of NetworkSessionProperties. */
        NetworkSessionProperties() = default;

        /**
         * @brief Gets the number of properties in the list.
         *
         * @return The element count.
         */
        [[nodiscard]] int getCountProperty() const override;

        /**
         * @brief Gets the property at the specified index.
         *
         * @param index Zero-based index.
         * @return Const reference to the value at that index.
         * @throws std::out_of_range if index is out of range.
         */
        [[nodiscard]] const std::optional<int>& operator[](int index) const override;

        /**
         * @brief Gets or sets the property at the specified index.
         *
         * If index is beyond the current end of the list, the returned slot is a
         * newly appended element at the end instead of one extending the list out
         * to @p index — this matches FNA's own incomplete behavior (its reference
         * source carries a "TODO: Expand list to index size?" comment).
         *
         * Known, structural divergence from FNA: real XNA's `int?[index]` indexer has separate
         * `get`/`set` accessors — only `set` auto-appends on an out-of-range index; `get` throws
         * (via `List<T>`'s own indexer). `IList<T>::operator[]` (the interface this overrides) has
         * a single non-const signature returning `T&` for both reading and writing, so C++ cannot
         * distinguish "this call is a read" from "this call is a write" the way C#'s separate
         * accessors can — auto-append fires on *any* out-of-range access through this non-const
         * overload, including a bare read with no assignment. A proxy-object return type could in
         * principle disambiguate this, but `operator[]`'s return type is fixed by the pure virtual
         * `IList<T>::operator[]` it overrides (changing that would ripple through every `IList<T>`
         * implementer in this codebase) — this divergence is accepted as-is, not silently. Code
         * that needs strict, non-appending bounds-checked reads should go through a
         * `const NetworkSessionProperties&` reference instead, which always resolves to the
         * correctly-throwing const overload above.
         *
         * @param index Zero-based index.
         * @return Reference to the slot that will hold the assigned value.
         */
        [[nodiscard]] std::optional<int>& operator[](int index) override;

        /**
         * @brief Determines the index of a specific value in the list.
         *
         * @param item The value to locate.
         * @return The index of the value if found; otherwise -1.
         */
        [[nodiscard]] int IndexOf(const std::optional<int>& item) const override;

        /**
         * @brief Inserts a value at the specified index.
         *
         * @param index The zero-based index at which to insert.
         * @param item  The value to insert.
         */
        void Insert(int index, const std::optional<int>& item) override;

        /**
         * @brief Removes the value at the specified index.
         *
         * @param index The zero-based index to remove.
         */
        void RemoveAt(int index) override;

        /**
         * @brief Gets whether this list is read-only.
         *
         * Always true, matching FNA's explicit `ICollection<int?>.IsReadOnly` — despite
         * Add/Remove/Clear below still being fully functional in the reference source.
         *
         * @return Always true.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const override;

        /**
         * @brief Appends a value to the end of the list.
         *
         * @param item The value to add.
         */
        void Add(const std::optional<int>& item) override;

        /**
         * @brief Removes the first occurrence of a specific value.
         *
         * @param item The value to remove.
         * @return true if a value was removed; otherwise false.
         */
        bool Remove(const std::optional<int>& item) override;

        /**
         * @brief Determines whether the list contains a specific value.
         *
         * @param item The value to locate.
         * @return true if found; otherwise false.
         */
        [[nodiscard]] bool Contains(const std::optional<int>& item) const override;

        /**
         * @brief Removes all values from the list.
         */
        void Clear() override;

        /**
         * @brief Returns a new enumerator that iterates through the list.
         *
         * @return A heap-allocated enumerator; caller takes ownership.
         */
        [[nodiscard]] System::Collections::Generic::IEnumerator<std::optional<int>>* GetEnumerator() override;

        /** @brief Returns an iterator to the beginning of the list (STL/range-for interop). */
        NOXNA [[nodiscard]] auto begin()       { return properties_.begin(); }
        /** @brief Returns an iterator past the end of the list (STL/range-for interop). */
        NOXNA [[nodiscard]] auto end()         { return properties_.end(); }
        /** @brief Returns a const iterator to the beginning of the list (STL/range-for interop). */
        NOXNA [[nodiscard]] auto begin() const { return properties_.cbegin(); }
        /** @brief Returns a const iterator past the end of the list (STL/range-for interop). */
        NOXNA [[nodiscard]] auto end()   const { return properties_.cend(); }

    private:
        class Enumerator : public System::Collections::Generic::IEnumerator<std::optional<int>>
        {
        public:
            /** @brief Constructs an enumerator over items, positioned before the first element. */
            explicit Enumerator(const std::vector<std::optional<int>>& items) : items_(items) {}
            /**
             * @brief Advances the enumerator to the next element.
             *
             * @return true if there is a next element; otherwise false.
             */
            bool MoveNext() override { return ++index_ < static_cast<int>(items_.size()); }
            /** @brief Resets the enumerator to before the first element. */
            void Reset() override { index_ = -1; }
            /** @brief Gets the element at the current position of the enumerator. */
            [[nodiscard]] const std::optional<int>& Current() const override
            {
                return items_[static_cast<std::size_t>(index_)];
            }

        private:
            const std::vector<std::optional<int>>& items_;
            int index_ = -1;
        };

        std::vector<std::optional<int>> properties_;
    };
}
