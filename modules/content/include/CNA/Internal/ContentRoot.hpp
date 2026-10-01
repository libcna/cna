// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Internal
{
    /**
     * @brief The directory a content root names: a relative root under the title's directory, as
     * XNA's ContentManager opens it through TitleContainer, when that directory exists there.
     *
     * Otherwise the root as given, which the filesystem resolves against the working directory --
     * where CNA looked before, so a program run beside its content (CNA's own tests, from the
     * repository root) still finds it. An absolute or empty root is returned unchanged.
     *
     * @param rootDirectory The content manager's RootDirectory, UTF-8.
     * @param titlePath The title's directory, UTF-8; empty for none.
     * @return The root to read from, UTF-8.
     */
    [[nodiscard]] std::string ResolveContentRoot(const std::string& rootDirectory, const std::string& titlePath);
}
