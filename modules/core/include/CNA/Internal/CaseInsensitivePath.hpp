// SPDX-License-Identifier: MS-PL

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include "CNA/Internal/PathUtf8.hpp"

namespace CNA::Internal
{
    /**
     * @brief Normalizes Windows directory separators for a host filesystem lookup.
     *
     * @param path The path supplied through an XNA API.
     * @return The path with every backslash replaced by a forward slash.
     */
    [[nodiscard]] inline std::string NormalizeXnaPathSeparators(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    namespace Detail
    {
        /**
         * @brief Lowercases the ASCII letters of a UTF-8 string and leaves everything else alone.
         *
         * Deliberately not `std::tolower`, which consults the C locale and can therefore rewrite
         * bytes above 0x7F — every continuation byte of a UTF-8 sequence is above 0x7F, so a
         * locale-aware fold can corrupt a non-ASCII filename into matching the wrong entry. The
         * promise this walker makes is ASCII case-insensitivity, and this is exactly that.
         *
         * @param value UTF-8 text.
         * @return The same text with `A`–`Z` lowered.
         */
        [[nodiscard]] inline std::string FoldAsciiCase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](char c) {
                    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                });
            return value;
        }
    }

    /**
     * @brief Resolves an existing native path one component at a time without ASCII case
     *        sensitivity.
     *
     * The promise is a path that opens the file XNA content named with any ASCII casing -- not the
     * casing stored on disk. A path the host already opens is returned as requested: an exact
     * match, and on a case-insensitive filesystem (NTFS, APFS by default) every casing, with no
     * directory scan. Otherwise, if a component has exactly one case-insensitive match in its
     * parent directory, that spelling is used. Missing or ambiguous components leave the original
     * path unchanged so the caller's normal not-found behavior remains authoritative.
     * (plans/plan_graphics_shared_cleanup.md GSC-0007, formerly WINNATIVE-F26.)
     *
     * Every comparison is made on UTF-8 text obtained with PathToUtf8(), never on
     * `path::string()`: the walker converts *every entry it enumerates*, not only the one it is
     * looking for, so on Windows a single non-ASCII sibling used to abort the search for an
     * ordinary ASCII file.
     *
     * @param requested The native path to resolve.
     * @return A spelling that opens the existing file, or @p requested when it cannot be resolved.
     */
    [[nodiscard]] inline std::filesystem::path ResolveExistingNativePath(
        const std::filesystem::path& requested)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        if (fs::exists(requested, ec) && !ec)
        {
            return requested;
        }

        fs::path resolved = requested.is_absolute() ? requested.root_path() : fs::path{};
        for (const fs::path& component : requested.relative_path())
        {
            const fs::path exact = resolved / component;
            ec.clear();
            if (fs::exists(exact, ec) && !ec)
            {
                resolved = exact;
                continue;
            }

            const fs::path parent = resolved.empty() ? fs::path(".") : resolved;
            ec.clear();
            fs::directory_iterator entry(parent, ec);
            if (ec)
            {
                return requested;
            }

            const std::string wanted = Detail::FoldAsciiCase(PathToUtf8(component));

            fs::path match;
            const fs::directory_iterator end;
            for (; entry != end; entry.increment(ec))
            {
                if (ec)
                {
                    return requested;
                }

                const std::string candidate =
                    Detail::FoldAsciiCase(PathToUtf8(entry->path().filename()));
                if (candidate == wanted)
                {
                    if (!match.empty())
                    {
                        return requested;
                    }
                    match = entry->path().filename();
                }
            }

            if (match.empty())
            {
                return requested;
            }
            resolved /= match;
        }

        return resolved;
    }

    /**
     * @brief Resolves an existing path one component at a time without ASCII case sensitivity.
     *
     * The UTF-8 spelling of ResolveExistingNativePath(); see that function for the resolution
     * rules. Input and result are UTF-8 (docs/filesystem-path-model.md rule 2).
     *
     * @param path The relative or absolute XNA content path to resolve, as UTF-8.
     * @return A spelling that opens the existing file, as generic UTF-8, or the normalized input
     *         when it cannot be resolved.
     */
    [[nodiscard]] inline std::string ResolveExistingXnaPath(const std::string& path)
    {
        const std::string normalized = NormalizeXnaPathSeparators(path);
        const std::optional<std::filesystem::path> requested = TryPathFromUtf8(normalized);
        if (!requested)
        {
            // Text that cannot name a path here resolves to itself, which is what the caller's
            // own not-found handling already copes with.
            return normalized;
        }
        return PathToGenericUtf8(ResolveExistingNativePath(*requested));
    }
}
