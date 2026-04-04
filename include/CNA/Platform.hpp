//
// Created by robertvokac on 6/1/25.
//

#pragma once

namespace CNA {

    /**
     * @brief Represents a target platform supported by CNA.
     *
     * @note Status: Implemented
     */
    enum class Platform {
        Desktop,
        Android,
        iOS,
        Web
    };

    /**
     * @brief Returns the current platform.
     *
     * The platform is determined at compile time using platform-specific
     * preprocessor macros.
     *
     * @return The current platform.
     *
     * @note Status: Implemented
     */
    Platform getCurrentPlatform();

} // CNA