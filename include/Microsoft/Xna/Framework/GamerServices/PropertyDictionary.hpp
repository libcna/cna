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
