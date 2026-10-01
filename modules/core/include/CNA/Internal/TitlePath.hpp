// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <string>

namespace CNA::Internal
{
    /**
     * @brief Records the title's directory -- XNA's TitleLocation.Path -- for modules below the
     * runtime module, which owns it.
     *
     * The content manager resolves a relative RootDirectory against it, as XNA's ContentManager
     * does through TitleContainer.
     *
     * @param path The title's directory, UTF-8.
     */
    void SetTitlePath(std::string path);

    /**
     * @brief Gets the title's directory, once TitleLocation has decided it.
     *
     * @return The directory, or nothing before TitleLocation has been asked or told.
     */
    [[nodiscard]] std::optional<std::string> TryGetTitlePath();
}
