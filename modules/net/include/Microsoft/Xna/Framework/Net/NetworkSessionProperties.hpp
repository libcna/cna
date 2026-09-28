// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/Collections/Generic/IEnumerator.hpp"
#include "System/Collections/Generic/IList.hpp"
#include "System/Collections/detail/ElementReference.hpp"
#include "System/Collections/detail/MutationCounter.hpp"
#include "System/InvalidOperationException.hpp"
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace Microsoft::Xna::Framework::Net
{
    /** @brief Eight nullable integer values advertised and filtered during matchmaking. */
    class NetworkSessionProperties : public System::Collections::Generic::IList<std::optional<int>>
    {
    public:
        /** @brief Initializes eight unspecified session properties. */
        NetworkSessionProperties();
        /**
         * @brief Copies values into an independent collection.
         *
         * @param other Source values.
         */
        CNAEXT NetworkSessionProperties(const NetworkSessionProperties& other);
        /**
         * @brief Copies values while preserving the source's valid fixed shape.
         *
         * @param other Source.
         */
        CNAEXT NetworkSessionProperties(NetworkSessionProperties&& other);
        /**
         * @brief Replaces values subject to this collection's write permission.
         *
         * @param other Source values.
         * @return This collection.
         */
        CNAEXT NetworkSessionProperties& operator=(const NetworkSessionProperties& other);
        /**
         * @brief Replaces values subject to this collection's write permission.
         *
         * @param other Source values.
         * @return This collection.
         */
        CNAEXT NetworkSessionProperties& operator=(NetworkSessionProperties&& other);
        /**
         * @brief Gets the fixed property count.
         *
         * @return Eight.
         */
        [[nodiscard]] int getCountProperty() const override;
        /**
         * @brief Reads an indexed value.
         *
         * @param index Slot 0..7.
         * @return Property value.
         * @throws System::ArgumentOutOfRangeException Invalid slot.
         */
        [[nodiscard]] const std::optional<int>& operator[](int index) const override;
        /**
         * @brief Reads or writes a slot through a permission-checked proxy.
         *
         * @param index Slot 0..7.
         * @return Tracked property reference.
         * @throws System::ArgumentOutOfRangeException Invalid slot.
         */
        [[nodiscard]] System::Collections::detail::ElementReference<std::optional<int>> operator[](
            int index) override;
        /**
         * @brief Reads a slot.
         *
         * @param index Slot 0..7.
         * @return Property value.
         */
        [[nodiscard]] const std::optional<int>& getItem(int index) const override;
        /**
         * @brief Sets a slot when writable.
         *
         * @param index Slot 0..7.
         * @param value Nullable value.
         */
        void setItem(int index, const std::optional<int>& value) override;
        /**
         * @brief Finds the first equal value.
         *
         * @param item Value.
         * @return Slot, or -1.
         */
        [[nodiscard]] int IndexOf(const std::optional<int>& item) const override;
        /**
         * @brief Rejects structural insertion into this fixed collection.
         *
         * @param index Requested slot.
         * @param item Requested value.
         * @throws System::NotSupportedException Always.
         */
        void Insert(int index, const std::optional<int>& item) override;
        /**
         * @brief Rejects structural removal.
         *
         * @param index Requested slot.
         * @throws System::NotSupportedException Always.
         */
        void RemoveAt(int index) override;
        /**
         * @brief Reports the collection interface's read-only flag.
         *
         * @return False.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const override;
        /**
         * @brief Rejects appending to this fixed collection.
         *
         * @param item Requested value.
         * @throws System::NotSupportedException Always.
         */
        void Add(const std::optional<int>& item) override;
        /**
         * @brief Rejects structural removal.
         *
         * @param item Requested value.
         * @return Never returns.
         * @throws System::NotSupportedException Always.
         */
        bool Remove(const std::optional<int>& item) override;
        /**
         * @brief Tests for an equal value.
         *
         * @param item Value.
         * @return Whether present.
         */
        [[nodiscard]] bool Contains(const std::optional<int>& item) const override;
        /**
         * @brief Rejects clearing this fixed collection.
         *
         * @throws System::NotSupportedException Always.
         */
        void Clear() override;
        /**
         * @brief Copies all eight values into an existing vector.
         *
         * @param destination Output storage.
         * @param index First output slot.
         */
        void CopyTo(std::vector<std::optional<int>>& destination, int index) const;
        /**
         * @brief Enumerates the eight current property values.
         *
         * @return Caller-owned enumerator.
         */
        [[nodiscard]] System::Collections::Generic::IEnumerator<std::optional<int>>* GetEnumerator() override;
        /**
         * @brief Gets a read-only range iterator.
         *
         * @return First slot.
         */
        CNAEXT [[nodiscard]] auto begin()
        {
            return properties_.cbegin();
        }
        /**
         * @brief Gets a read-only range end.
         *
         * @return Past last slot.
         */
        CNAEXT [[nodiscard]] auto end()
        {
            return properties_.cend();
        }
        /**
         * @brief Gets a read-only range iterator.
         *
         * @return First slot.
         */
        CNAEXT [[nodiscard]] auto begin() const
        {
            return properties_.cbegin();
        }
        /**
         * @brief Gets a read-only range end.
         *
         * @return Past last slot.
         */
        CNAEXT [[nodiscard]] auto end() const
        {
            return properties_.cend();
        }

    private:
        friend class NetworkSession;
        friend class AvailableNetworkSession;
        void setWriteGuard(std::function<void()> guard);
        void makeReadOnly();
        void replaceFromTransport(const NetworkSessionProperties& properties);
        class Enumerator : public System::Collections::Generic::IEnumerator<std::optional<int>>
        {
        public:
            /**
             * @brief Starts before the first slot.
             *
             * @param items Fixed property storage.
             */
            explicit Enumerator(const std::array<std::optional<int>, 8>& items) : items_(items)
            {
            }
            /**
             * @brief Advances one slot.
             *
             * @return Whether positioned on a value.
             */
            bool MoveNext() override
            {
                if (index_ < 8)
                    ++index_;
                return index_ < 8;
            }
            /** @brief Resets before the first slot. */
            void Reset() override
            {
                index_ = -1;
            }
            /**
             * @brief Reads the current slot.
             *
             * @return Value at the current position.
             */
            [[nodiscard]] const std::optional<int>& Current() const override
            {
                if (index_ < 0 || index_ >= 8)
                    throw System::InvalidOperationException("Enumerator is not positioned on a property.");
                return items_[static_cast<std::size_t>(index_)];
            }

        private:
            const std::array<std::optional<int>, 8>& items_;
            int index_ = -1;
        };
        std::array<std::optional<int>, 8> properties_{};
        System::Collections::detail::MutationCounter version_;
        std::function<void()> beforeWrite_;
        bool readOnly_ = false;
    };
} // namespace Microsoft::Xna::Framework::Net
