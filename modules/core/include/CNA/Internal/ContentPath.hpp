// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace CNA::Internal
{
    /**
     * @brief Converts a native filesystem path to deterministic generic UTF-8 text.
     *
     * This representation is suitable for diagnostics, logical identities, and persistent
     * manifests. It is not used to reopen the path without first converting it back through
     * ContentPathFromUtf8().
     *
     * @param path Native filesystem path.
     * @return Generic path text encoded as UTF-8.
     */
    [[nodiscard]] inline std::string ContentPathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string value = path.generic_u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    /**
     * @brief Reconstructs a native filesystem path from persistent UTF-8 path text.
     *
     * @param value Generic path text encoded as UTF-8.
     * @return Native filesystem path.
     */
    [[nodiscard]] inline std::filesystem::path ContentPathFromUtf8(std::string_view value)
    {
        std::u8string utf8;
        utf8.reserve(value.size());
        for (const unsigned char byte : value)
        {
            utf8.push_back(static_cast<char8_t>(byte));
        }
        return std::filesystem::path(utf8);
    }
}
