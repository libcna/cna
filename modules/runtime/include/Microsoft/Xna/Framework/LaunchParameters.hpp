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
        /** @brief Creates launch parameters from the current process command line. */
        LaunchParameters();

        /**
         * @brief Creates launch parameters from an explicit argument list.
         * @param args The argument strings to parse into key/value pairs.
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
         */
        void Add(const std::string& key, const std::string& value);

    private:
        static std::vector<std::string> GetCommandLineArgs();
        void Parse(const std::vector<std::string>& args);
        static std::string TrimStartFlags(const std::string& value);
    };
}
