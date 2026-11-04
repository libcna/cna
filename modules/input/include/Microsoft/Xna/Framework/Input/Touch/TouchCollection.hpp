// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "System/Collections/Generic/IEnumerator.hpp"
#include "System/IDisposable.hpp"

#include <cstddef>
#include <vector>

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Provides an immutable snapshot of current touch locations.
     */
    struct TouchCollection
    {
        /** @brief Iterates over a value snapshot of a TouchCollection. */
        struct Enumerator final : public System::Collections::Generic::IEnumerator<TouchLocation>,
                                  public System::IDisposable
        {
            /** @brief Constructs the default value of the enumerator struct. */
            Enumerator() = default;

            /**
             * @brief Gets the current touch location.
             * @return The current location value.
             */
            [[nodiscard]] const TouchLocation& Current() const override;

            /**
             * @brief Advances to the next touch location.
             * @return True if an element is available.
             */
            bool MoveNext() override;

            /** @brief Returns to the position before the first element through the IEnumerator contract. */
            CNAEXT void Reset() override;

            /** @brief Releases enumerator resources. */
            void Dispose() override;

        private:
            friend struct TouchCollection;
            explicit Enumerator(const TouchCollection& collection);
            std::vector<TouchLocation> touches_;
            int position_ = 0;
        };

        /**
         * @brief Creates an enumerator over this collection's touch snapshot.
         * @return An independent value-type enumerator.
         */
        [[nodiscard]] Enumerator GetEnumerator() const;

        /**
         * @brief Gets the number of touch locations in this collection.
         * @return The touch location count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets the connection flag stored in this snapshot.
         * @return True if connected; false otherwise.
         */
        [[nodiscard]] bool getIsConnectedProperty() const;

        /**
         * @brief Gets whether this snapshot is read-only.
         * @return Always true; mutation methods throw System::NotSupportedException.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /**
         * @brief Constructs an empty, disconnected touch collection.
         * @note CNAEXT — FNA's `TouchCollection` has no explicit parameterless constructor
         *       (only the one taking a touch array).
         */
        CNAEXT TouchCollection();

        /**
         * @brief Constructs a connected snapshot from a vector, including an empty vector.
         * @param touches The touch locations to include.
         */
        explicit TouchCollection(const std::vector<TouchLocation>& touches);

        /**
         * @brief Constructs a connected snapshot by moving a vector, including an empty vector.
         * @param touches The touch locations to move in.
         */
        explicit TouchCollection(std::vector<TouchLocation>&& touches);

        /**
         * @brief Implements the indexer setter, which always throws System::NotSupportedException.
         * @param index The requested index; no range validation precedes the exception.
         * @param value The requested value.
         */
        void setItemProperty(std::size_t index, const TouchLocation& value);

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
         * @note XNA FindById(int,out), IL_0032-003a, writes a default TouchLocation on a miss:
         *       ID zero, Invalid state, and zero position. The output is assigned on both paths.
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
         * @brief Always throws System::NotSupportedException; snapshots are read-only.
         * @param item The touch location to add.
         */
        void Add(const TouchLocation& item);

        /** @brief Always throws System::NotSupportedException; snapshots are read-only. */
        void Clear();

        /**
         * @brief Always throws System::NotSupportedException; snapshots are read-only.
         * @param item The touch location to remove.
         * @return Never returns; throws System::NotSupportedException.
         */
        bool Remove(const TouchLocation& item);

        /**
         * @brief Always throws System::NotSupportedException, even for an invalid index.
         * @param index The zero-based index to remove.
         */
        void RemoveAt(int index);

        /**
         * @brief Always throws System::NotSupportedException, even for an invalid index.
         * @param index The zero-based index to insert at.
         * @param item The touch location to insert.
         */
        void Insert(int index, const TouchLocation& item);

        /**
         * @brief Returns a const iterator to the beginning of the collection.
         * @note CNAEXT — replaces FNA's `IEnumerable<TouchLocation>::GetEnumerator()`.
         */
        CNAEXT std::vector<TouchLocation>::const_iterator begin();
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT std::vector<TouchLocation>::const_iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        CNAEXT [[nodiscard]] std::vector<TouchLocation>::const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT [[nodiscard]] std::vector<TouchLocation>::const_iterator end() const;

    private:
        friend class TouchPanel;
        bool isConnected_ = false;
        std::vector<TouchLocation> touches_;
    };
}
