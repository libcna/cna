// SPDX-License-Identifier: MS-PL
#pragma once
#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

namespace CNA
{
    /**
     * @brief Enumerates the build targets CNA supports.
     *
     * A compile-time classification of the *build*, decided by preprocessor macros — deliberately
     * not the same thing as `CNA::Platform`, the runtime windowing/input/system abstraction in
     * `modules/platform`. This was named `CNA::Platform` until the platform contract arrived, at
     * which point an enum and a namespace shared a name in the same scope. That is ill-formed:
     * any translation unit including both headers failed to compile, and only the accident that
     * none did yet kept the build green.
     */
    enum class TargetPlatform
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
     * @return The build target this binary was compiled for.
     */
    constexpr TargetPlatform getCurrentPlatform()
    {
#if defined(__EMSCRIPTEN__)
        return TargetPlatform::Web;
#elif defined(__ANDROID__)
        return TargetPlatform::Android;
#elif defined(__APPLE__)
#  if TARGET_OS_IPHONE
        return TargetPlatform::iOS;
#  else
        return TargetPlatform::Desktop;
#  endif
#else
        return TargetPlatform::Desktop;
#endif
    }
} // CNA
