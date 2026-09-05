// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/ArgumentException.hpp"
#include "System/Collections/Generic/IDictionary.hpp"
#include "System/Collections/Generic/IEnumerator.hpp"
#include "System/Collections/Generic/KeyNotFoundException.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides a base class for dictionaries that map string identifiers to data values.
     *
     * Entries keep their insertion order, which is the order a .NET `Dictionary<string,T>` with
     * no removals enumerates in and therefore the order XNA's intermediate XML lists them.
     * Derived classes customize storage through the protected `AddItem`/`ClearItems`/
     * `RemoveItem`/`SetItem` hooks, exactly as in XNA.
     *
     * @tparam T The value type stored by the dictionary.
     */
    namespace Serialization::Intermediate
    {
        template<typename TDictionary, typename TValue>
        class NamedValueDictionarySerializer;
    }

    template<typename T>
    class NamedValueDictionary : public System::Collections::Generic::IDictionary<std::string, T>
    {
    public:
        /** @brief The pair type the dictionary enumerates. */
        using value_type = std::pair<std::string, T>;

        /** @brief Initializes an empty dictionary. */
        NamedValueDictionary() = default;

        /** @brief Destroys the dictionary. */
        ~NamedValueDictionary() override = default;

        /**
         * @brief Gets the number of items in the dictionary.
         *
         * @return The entry count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const override
        {
            return static_cast<SharpRuntime::intcs>(entries_.size());
        }

        /**
         * @brief Gets all keys contained in the dictionary, in enumeration order.
         *
         * @return A copy of the keys.
         */
        [[nodiscard]] std::vector<std::string> getKeysProperty() const
        {
            std::vector<std::string> keys;
            keys.reserve(entries_.size());
            for (const value_type& entry : entries_) { keys.push_back(entry.first); }
            return keys;
        }

        /**
         * @brief Gets all values contained in the dictionary, in enumeration order.
         *
         * @return A copy of the values.
         */
        [[nodiscard]] std::vector<T> getValuesProperty() const
        {
            std::vector<T> values;
            values.reserve(entries_.size());
            for (const value_type& entry : entries_) { values.push_back(entry.second); }
            return values;
        }

        /**
         * @brief Gets the value associated with the specified key.
         *
         * @param key The key to look up.
         * @return A reference to the stored value.
         * @throws System::Collections::Generic::KeyNotFoundException when the key is absent.
         */
        [[nodiscard]] const T& operator[](const std::string& key) const override
        {
            const auto it = Find(key);
            if (it == entries_.end())
            {
                throw System::Collections::Generic::KeyNotFoundException(
                    "The key '" + key + "' was not present in the dictionary.");
            }
            return it->second;
        }

        /**
         * @brief Gets, for assignment, the value slot associated with the specified key.
         *
         * Reading through this reference requires the key to exist; assigning adds or replaces
         * the entry through `SetItem`/`AddItem`, matching the C# indexer setter.
         *
         * @param key The key to look up.
         * @return A reference to the stored value.
         * @throws System::Collections::Generic::KeyNotFoundException when the key is absent.
         */
        T& operator[](const std::string& key) override
        {
            auto it = Find(key);
            if (it == entries_.end())
            {
                throw System::Collections::Generic::KeyNotFoundException(
                    "The key '" + key + "' was not present in the dictionary.");
            }
            return it->second;
        }

        /**
         * @brief Adds a new key/value pair, or replaces the value under an existing key --
         *        the C# indexer setter.
         *
         * @param key The key.
         * @param value The value.
         */
        void Set(const std::string& key, const T& value)
        {
            if (Find(key) == entries_.end()) { AddItem(key, value); }
            else { SetItem(key, value); }
        }

        /**
         * @brief Adds the specified key and value to the dictionary.
         *
         * @param key The key; must not be empty or already present.
         * @param value The value.
         * @throws System::ArgumentException when the key is empty or already present.
         */
        void Add(const std::string& key, const T& value) override
        {
            AddItem(key, value);
        }

        /** @brief Removes all keys and values from the dictionary. */
        void Clear() override
        {
            ClearItems();
        }

        /**
         * @brief Determines whether the dictionary contains the specified key.
         *
         * @param key The key to look for.
         * @return True when present.
         */
        [[nodiscard]] bool ContainsKey(const std::string& key) const override
        {
            return Find(key) != entries_.end();
        }

        /**
         * @brief Removes the value with the specified key.
         *
         * @param key The key to remove.
         * @return True when an entry was removed.
         */
        bool Remove(const std::string& key) override
        {
            return RemoveItem(key);
        }

        /**
         * @brief Gets the value associated with the specified key without throwing.
         *
         * @param key The key to look up.
         * @param value Receives the value when present.
         * @return True when the key was present.
         */
        bool TryGetValue(const std::string& key, T& value) const override
        {
            const auto it = Find(key);
            if (it == entries_.end()) { return false; }
            value = it->second;
            return true;
        }

        /**
         * @brief Returns an enumerator over the key/value pairs in insertion order.
         *
         * @return A heap-allocated enumerator the caller owns, as sharp-runtime's collections do.
         */
        [[nodiscard]] System::Collections::Generic::IEnumerator<value_type>* GetEnumerator() override
        {
            return new Enumerator(this);
        }

        /** @brief Range-for support: first entry. */
        [[nodiscard]] auto begin() const noexcept { return entries_.cbegin(); }

        /** @brief Range-for support: past-the-end. */
        [[nodiscard]] auto end() const noexcept { return entries_.cend(); }

        /** @brief The intermediate serializer reads the protected default serializer type. */
        template<typename, typename>
        friend class Serialization::Intermediate::NamedValueDictionarySerializer;

    protected:
        /**
         * @brief Gets the type of serializer used when the value type cannot be determined from
         *        a value alone -- the `object` element type of an opaque dictionary.
         *
         * @return `System::Type::From<T>()` here; `OpaqueDataDictionary` answers `Object`.
         */
        [[nodiscard]] virtual System::Type getDefaultSerializerTypeProperty() const
        {
            return System::Type::From<T>();
        }

        /**
         * @brief Adds an element to the dictionary.
         *
         * @param key The key; must be non-empty and absent.
         * @param value The value.
         * @throws System::ArgumentException when the key is empty or already present.
         */
        virtual void AddItem(const std::string& key, const T& value)
        {
            if (key.empty()) { throw System::ArgumentException("The dictionary key must not be empty.", "key"); }
            if (Find(key) != entries_.end())
            {
                throw System::ArgumentException("An item with the same key has already been added: '" + key + "'.", "key");
            }
            entries_.emplace_back(key, value);
        }

        /** @brief Removes all elements. */
        virtual void ClearItems()
        {
            entries_.clear();
        }

        /**
         * @brief Removes the element with the specified key.
         *
         * @param key The key to remove.
         * @return True when an entry was removed.
         */
        virtual bool RemoveItem(const std::string& key)
        {
            auto it = Find(key);
            if (it == entries_.end()) { return false; }
            entries_.erase(it);
            return true;
        }

        /**
         * @brief Replaces the value of an existing element.
         *
         * @param key The key; must be present.
         * @param value The new value.
         * @throws System::Collections::Generic::KeyNotFoundException when the key is absent.
         */
        virtual void SetItem(const std::string& key, const T& value)
        {
            auto it = Find(key);
            if (it == entries_.end())
            {
                throw System::Collections::Generic::KeyNotFoundException(
                    "The key '" + key + "' was not present in the dictionary.");
            }
            it->second = value;
        }

    private:
        class Enumerator final : public System::Collections::Generic::IEnumerator<value_type>
        {
        public:
            explicit Enumerator(const NamedValueDictionary* owner) : owner_(owner) {}
            bool MoveNext() override
            {
                if (index_ + 1 >= static_cast<long>(owner_->entries_.size())) { index_ = static_cast<long>(owner_->entries_.size()); return false; }
                ++index_;
                return true;
            }
            void Reset() override { index_ = -1; }
            [[nodiscard]] const value_type& Current() const override
            {
                return owner_->entries_[static_cast<std::size_t>(index_)];
            }

        private:
            const NamedValueDictionary* owner_;
            long index_ = -1;
        };

        [[nodiscard]] typename std::vector<value_type>::iterator Find(const std::string& key)
        {
            return std::find_if(entries_.begin(), entries_.end(),
                                [&key](const value_type& entry) { return entry.first == key; });
        }

        [[nodiscard]] typename std::vector<value_type>::const_iterator Find(const std::string& key) const
        {
            return std::find_if(entries_.begin(), entries_.end(),
                                [&key](const value_type& entry) { return entry.first == key; });
        }

        std::vector<value_type> entries_;
    };
}
