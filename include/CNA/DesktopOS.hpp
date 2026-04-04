//
// Created by robertvokac on 6/1/25.
//

#pragma once

namespace CNA {

    /**
     * @brief Represents a desktop operating system.
     * @note Status: Verified
     */
    enum class DesktopOS {
        /**
         * @brief Microsoft Windows.
         */
        Windows,

        /**
         * @brief GNU/Linux.
         */
        Linux,

        /**
         * @brief Apple macOS.
         */
        MacOSX,

        /**
         * @brief Any other desktop operating system.
         */
        Other
    };

    /**
     * @brief Returns the current desktop operating system.
     *
     * This function may only be used when the current platform is
     * @c Platform::Desktop. If the current platform is not desktop,
     * an exception is thrown.
     *
     * @return The current desktop operating system.
     *
     * @throws CNAException Thrown when the current platform is not desktop.
     */
    DesktopOS getCurrentDesktopOS();

} // CNA