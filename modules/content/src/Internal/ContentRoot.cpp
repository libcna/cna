// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/ContentRoot.hpp"

#include "CNA/Internal/PathUtf8.hpp"

#include <filesystem>
#include <optional>
#include <system_error>

namespace CNA::Internal
{
    std::string ResolveContentRoot(const std::string& rootDirectory, const std::string& titlePath)
    {
        namespace fs = std::filesystem;
        if (rootDirectory.empty() || titlePath.empty())
        {
            return rootDirectory;
        }

        const std::optional<fs::path> root = TryPathFromUtf8(rootDirectory);
        const std::optional<fs::path> title = TryPathFromUtf8(titlePath);
        if (!root || !title || root->is_absolute() || root->has_root_name())
        {
            return rootDirectory;
        }

        const fs::path underTitle = (*title / *root).lexically_normal();
        std::error_code error;
        if (fs::is_directory(underTitle, error) && !error)
        {
            return PathToGenericUtf8(underTitle);
        }

        return rootDirectory;
    }
}
