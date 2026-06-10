// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework
{
    /// Dictionary of command-line launch parameters parsed as key/value pairs.
    class LaunchParameters : public std::unordered_map<std::string, std::string>
    {
    public:
        /// Creates launch parameters from the current process command line.
        LaunchParameters();

        /// Creates launch parameters from an explicit argument list.
        NOXNA explicit LaunchParameters(const std::vector<std::string>& args);

        /// Returns true when the dictionary contains the specified key.
        [[nodiscard]] bool ContainsKey(const std::string& key) const;

        /// Adds a key/value pair.
        void Add(const std::string& key, const std::string& value);

    private:
        static std::vector<std::string> GetCommandLineArgs();
        void Parse(const std::vector<std::string>& args);
        static std::string TrimStartFlags(const std::string& value);
    };
}
