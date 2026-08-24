// SPDX-License-Identifier: MS-PL

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

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

    /**
     * @brief Resolves an existing path one component at a time without ASCII case sensitivity.
     *
     * An exact match always wins. If a component has exactly one case-insensitive match in its
     * parent directory, that spelling is used. Missing or ambiguous components leave the original
     * normalized path unchanged so the caller's normal not-found behavior remains authoritative.
     *
     * @param path The relative or absolute XNA content path to resolve.
     * @return The existing host spelling, or the normalized input when it cannot be resolved.
     */
    [[nodiscard]] inline std::string ResolveExistingXnaPath(const std::string& path)
    {
        namespace fs = std::filesystem;

        const fs::path requested(NormalizeXnaPathSeparators(path));
        std::error_code ec;
        if (fs::exists(requested, ec) && !ec)
        {
            return requested.string();
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
                return requested.string();
            }

            std::string wanted = component.string();
            std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

            fs::path match;
            const fs::directory_iterator end;
            for (; entry != end; entry.increment(ec))
            {
                if (ec)
                {
                    return requested.string();
                }

                std::string candidate = entry->path().filename().string();
                std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                    [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                if (candidate == wanted)
                {
                    if (!match.empty())
                    {
                        return requested.string();
                    }
                    match = entry->path().filename();
                }
            }

            if (match.empty())
            {
                return requested.string();
            }
            resolved /= match;
        }

        return resolved.string();
    }
}
