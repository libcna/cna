//
// Created by robertvokac on 6/1/25.
//

#pragma once
#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

namespace CNA
{
    /**
     * @brief Represents a target platform supported by CNA.
     *
     * @note Status: Implemented
     */
    enum class Platform
    {
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
    constexpr Platform getCurrentPlatform()
    {
#if defined(__EMSCRIPTEN__)
        return Platform::Web;
#elif defined(__ANDROID__)
        return Platform::Android;
#elif defined(__APPLE__)
#  if TARGET_OS_IPHONE
        return Platform::iOS;
#  else
        return Platform::Desktop;
#  endif
#else
        return Platform::Desktop;
#endif
    }
} // CNA
