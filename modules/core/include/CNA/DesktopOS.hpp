//
// Created by robertvokac on 6/1/25.
//

#pragma once

namespace CNA
{
    /** @brief Identifies a desktop operating system. */
    enum class DesktopOS
    {
        /** @brief Microsoft Windows. */
        Windows,

        /** @brief GNU/Linux. */
        Linux,

        /** @brief Apple macOS. */
        MacOSX,

        /** @brief Any other desktop operating system. */
        Other
    };

    /**
     * @brief Returns the current desktop operating system.
     *
     * This function may only be called when the current platform is
     * TargetPlatform::Desktop. If the current platform is not Desktop,
     * an exception is thrown.
     *
     * @return The current DesktopOS value.
     * @throws CNAException Thrown when the current platform is not Desktop.
     */
    DesktopOS getCurrentDesktopOS();
} // CNA
