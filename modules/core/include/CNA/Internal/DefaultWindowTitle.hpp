// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Internal
{
    /**
     * @brief The title a game window gets when the game has not set one.
     *
     * XNA reads the entry assembly's <c>AssemblyTitleAttribute</c> and falls back to the
     * assembly's simple name. This is the same two steps for a native program --
     * CNA::GetAssemblyTitleEXT(), then the running executable's own file name without its
     * directory or extension -- and "Game" as a last resort where neither is available,
     * which is what every CNA window used to be called unconditionally.
     *
     * @return The default window title; never empty.
     */
    [[nodiscard]] std::string GetDefaultWindowTitle();
}
