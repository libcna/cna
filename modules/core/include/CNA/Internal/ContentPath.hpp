// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "CNA/Internal/PathUtf8.hpp"

namespace CNA::Internal
{
    /**
     * @brief Converts a native filesystem path to deterministic generic UTF-8 text.
     *
     * This representation is suitable for diagnostics, logical identities, and persistent
     * manifests. It is not used to reopen the path without first converting it back through
     * ContentPathFromUtf8().
     *
     * Retained as the content pipeline's spelling of PathToGenericUtf8(), which it forwards to
     * unchanged; see PathUtf8.hpp for the path model the two share.
     *
     * @param path Native filesystem path.
     * @return Generic path text encoded as UTF-8.
     */
    [[nodiscard]] inline std::string ContentPathToUtf8(const std::filesystem::path& path)
    {
        return PathToGenericUtf8(path);
    }

    /**
     * @brief Reconstructs a native filesystem path from persistent UTF-8 path text.
     *
     * Retained as the content pipeline's spelling of PathFromUtf8(), which it forwards to
     * unchanged.
     *
     * @param value Generic path text encoded as UTF-8.
     * @return Native filesystem path.
     */
    [[nodiscard]] inline std::filesystem::path ContentPathFromUtf8(std::string_view value)
    {
        return PathFromUtf8(value);
    }
}
