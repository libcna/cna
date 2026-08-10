// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardOutcome.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"
#include "System/IO/Stream.hpp"
#include <any>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A dictionary that maps string keys to property values of varying types.
     */
    class PropertyDictionary
    {
    public:
        /**
         * @brief Returns the number of key/value pairs in the dictionary.
         *
         * @return The number of entries.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets the value associated with the specified key, or overwrites it through the
         * returned reference. Throws if key is not already present - use SetValue to insert a
         * new key.
         *
         * @param key The key to look up.
         * @return Reference to the stored std::any value.
         */
        [[nodiscard]] std::any& operator[](const std::string& key);

        /**
         * @brief Gets the value associated with the specified key (const overload).
         *
         * @param key The key to look up.
         * @return Const reference to the stored std::any value.
         */
        [[nodiscard]] const std::any& operator[](const std::string& key) const;

        /**
         * @brief Returns whether the dictionary contains the specified key.
         *
         * @param key The key to locate.
         * @return true if the key was found; otherwise false.
         */
        [[nodiscard]] bool ContainsKey(const std::string& key) const;

        /**
         * @brief Gets the value associated with the specified key.
         *
         * @param key The key to locate.
         * @param value Receives the value if found.
         * @return true if the key was found; otherwise false.
         */
        bool TryGetValue(const std::string& key, std::any& value) const;

        /**
         * @brief Gets the DateTime value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored DateTime.
         */
        [[nodiscard]] System::DateTime GetValueDateTime(const std::string& key) const;

        /**
         * @brief Gets the double value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored double.
         */
        [[nodiscard]] double GetValueDouble(const std::string& key) const;

        /**
         * @brief Gets the int32 value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored int.
         */
        [[nodiscard]] int GetValueInt32(const std::string& key) const;

        /**
         * @brief Gets the int64 value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored long.
         */
        [[nodiscard]] long long GetValueInt64(const std::string& key) const;

        /**
         * @brief Gets the LeaderboardOutcome value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored LeaderboardOutcome.
         */
        [[nodiscard]] LeaderboardOutcome GetValueOutcome(const std::string& key) const;

        /**
         * @brief Gets the float value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored float.
         */
        [[nodiscard]] float GetValueSingle(const std::string& key) const;

        /**
         * @brief Gets the Stream pointer associated with the specified key.
         *
         * @param key The key to look up.
         * @return Pointer to the stored Stream.
         */
        [[nodiscard]] System::IO::Stream* GetValueStream(const std::string& key) const;

        /**
         * @brief Gets the string value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored string.
         */
        [[nodiscard]] std::string GetValueString(const std::string& key) const;

        /**
         * @brief Gets the TimeSpan value associated with the specified key.
         *
         * @param key The key to look up.
         * @return The stored TimeSpan.
         */
        [[nodiscard]] System::TimeSpan GetValueTimeSpan(const std::string& key) const;

        /**
         * @brief Stores a DateTime value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, System::DateTime value);

        /**
         * @brief Stores a double value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, double value);

        /**
         * @brief Stores an int value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, int value);

        /**
         * @brief Stores a long value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, long long value);

        /**
         * @brief Stores a LeaderboardOutcome value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, LeaderboardOutcome value);

        /**
         * @brief Stores a float value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, float value);

        /**
         * @brief Stores a string value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, const std::string& value);

        /**
         * @brief Stores a TimeSpan value for the specified key.
         *
         * @param key The key.
         * @param value The value to store.
         */
        void SetValue(const std::string& key, System::TimeSpan value);

        /**
         * @brief Adds a value for the specified key.
         *
         * Task 8.1: real XNA's `PropertyDictionary` implements this via explicit
         * `IDictionary<string,object>.Add`/`ICollection<KeyValuePair<string,object>>.Add`
         * (both of which just forward to `Dictionary<TKey,TValue>.Add`) — reachable in C# only
         * through an interface cast, never directly on a `PropertyDictionary` variable. C++ has
         * no equivalent of explicit interface implementation, so this is exposed as an ordinary
         * public method instead (a documented, pragmatic mapping deviation — see CLAUDE.md's
         * guidance on interfaces without an exact C++ equivalent); the actual throw-on-duplicate
         * behavior matches `Dictionary<TKey,TValue>.Add` faithfully. Unlike `SetValue`/the
         * indexer setter, which silently overwrite an existing key.
         *
         * @param key The key to add.
         * @param value The value to associate with key.
         * @throws System::ArgumentException if key already exists.
         */
        void Add(const std::string& key, std::any value);

        /**
         * @brief Removes the value for the specified key.
         *
         * Task 8.1: same explicit-interface-only-in-C#, ordinary-method-in-C++ deviation as
         * Add() above (matches `IDictionary<string,object>.Remove(string)`).
         *
         * @param key The key to remove.
         * @return true if the key was found and removed; otherwise false.
         */
        bool Remove(const std::string& key);

        /**
         * @brief Removes all key/value pairs.
         *
         * Task 8.1: same explicit-interface-only-in-C#, ordinary-method-in-C++ deviation as
         * Add() above (matches `ICollection<KeyValuePair<string,object>>.Clear()`).
         */
        void Clear();

        /**
         * @brief Gets a snapshot of every key currently in the dictionary.
         *
         * Task 8.1: real XNA's explicit `IDictionary<string,object>.Keys` returns a live
         * `ICollection<string>` view backed by the dictionary itself; `sharp-runtime` has no
         * equivalent live-view collection type, so this returns a point-in-time snapshot vector
         * instead (a documented, pragmatic mapping deviation).
         *
         * @return A vector containing every key, in unspecified order.
         */
        [[nodiscard]] std::vector<std::string> Keys() const;

        /**
         * @brief Gets a snapshot of every value currently in the dictionary.
         *
         * Task 8.1: same live-view-vs-snapshot deviation as Keys() above (matches explicit
         * `IDictionary<string,object>.Values`).
         *
         * @return A vector containing every value, in unspecified order (same order as Keys()).
         */
        [[nodiscard]] std::vector<std::any> Values() const;

        /**
         * @brief Gets whether this collection is read-only.
         *
         * Always true, matching FNA's own hardcoded `ICollection<KeyValuePair<string,object>>
         * .IsReadOnly` getter — a real upstream inconsistency (Add/Remove/Clear are all working
         * mutators despite this), preserved faithfully rather than corrected, per this project's
         * behavior-fidelity rule.
         *
         * @return Always true.
         */
        [[nodiscard]] bool getIsReadOnlyProperty() const;

        /**
         * @brief Always throws, matching FNA's own unimplemented
         * `ICollection<KeyValuePair<string,object>>.CopyTo` stub.
         *
         * @param array Unused.
         * @param arrayIndex Unused.
         * @throws System::NotImplementedException always.
         */
        void CopyTo(std::vector<std::pair<std::string, std::any>>& array, int arrayIndex) const;

        /** @brief Returns an iterator to the beginning of the dictionary. */
        NOXNA [[nodiscard]] auto begin() { return dictionary_.begin(); }
        /** @brief Returns an iterator past the end of the dictionary. */
        NOXNA [[nodiscard]] auto end()   { return dictionary_.end(); }
        /** @brief Returns a const iterator to the beginning of the dictionary. */
        NOXNA [[nodiscard]] auto begin() const { return dictionary_.begin(); }
        /** @brief Returns a const iterator past the end of the dictionary. */
        NOXNA [[nodiscard]] auto end()   const { return dictionary_.end(); }

        /**
         * @brief Creates a PropertyDictionary from the given map.
         *
         * @param dict Initial key-value pairs.
         * @return A new PropertyDictionary wrapping the map.
         */
        NOXNA static PropertyDictionary CreateInternal(std::map<std::string, std::any> dict);

    private:
        explicit PropertyDictionary(std::map<std::string, std::any> dict);

        std::map<std::string, std::any> dictionary_;
    };
}
