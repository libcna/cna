//
// Created by robertvokac on 6/1/25.
//

#pragma once
#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

namespace CNA
{
    /** @brief Enumerates the target platforms supported by CNA. */
    enum class Platform
    {
        /** @brief Windows, Linux, or macOS desktop. */
        Desktop,
        /** @brief Android mobile. */
        Android,
        /** @brief Apple iOS. */
        iOS,
        /** @brief Browser via Emscripten/WebAssembly. */
        Web
    };

    /**
     * @brief Returns the current platform determined at compile time.
     *
     * The platform is detected using platform-specific preprocessor macros
     * (__EMSCRIPTEN__, __ANDROID__, __APPLE__ + TARGET_OS_IPHONE).
     *
     * @return The current Platform value.
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
