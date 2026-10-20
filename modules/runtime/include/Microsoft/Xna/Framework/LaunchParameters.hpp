// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Dictionary of command-line launch parameters parsed as key/value pairs. */
    class LaunchParameters : public std::unordered_map<std::string, std::string>
    {
    public:
        /** @brief Creates launch parameters from process arguments, excluding the executable name. */
        LaunchParameters();

        /**
         * @brief Creates launch parameters from an explicit argument list.
         * @param args Argument strings without an executable-name entry. Nonempty keys without
         *        a value are retained with an empty value; the first parsed duplicate is kept.
         */
        CNAEXT explicit LaunchParameters(const std::vector<std::string>& args);

        /**
         * @brief Returns true when the dictionary contains the specified key.
         * @param key The key to search for.
         * @return true if the key is present; false otherwise.
         */
        [[nodiscard]] bool ContainsKey(const std::string& key) const;

        /**
         * @brief Adds a key/value pair.
         * @param key The parameter name.
         * @param value The parameter value.
         * @throws System::ArgumentException The key already exists; its original value is unchanged.
         */
        void Add(const std::string& key, const std::string& value);

    private:
        static std::vector<std::string> GetCommandLineArgs();
        void Parse(const std::vector<std::string>& args);
        static std::string TrimStartFlags(const std::string& value);
    };
}
