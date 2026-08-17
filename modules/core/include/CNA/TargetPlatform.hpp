// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/TargetPlatform.hpp
 * @brief Compile-time target platform identification.
 *
 * Besides the CNA::TargetPlatform enumeration, this header defines the CNA_TARGET_* object-like
 * macros the rest of the codebase uses in preprocessor conditions. Apple targets need them:
 * macOS and iOS share the __APPLE__ macro and differ only in a TargetConditionals.h value, and
 * repeating that two-level test in every translation unit is how the two silently drift apart.
 *
 * The prefix is CNA_TARGET_, not CNA_PLATFORM_, on purpose. CNA_PLATFORM_<NAME> is already taken
 * by cmake/PlatformSelection.cmake for the *implementation* axis (SDL3, HEADLESS, TERMINAL …).
 * These macros name the operating system being built for, which is an independent axis — an iOS
 * build still chooses a platform implementation. Sharing one prefix between the two is the same
 * confusion the enum rename above resolved.
 */

#if defined(__APPLE__)
#  include <TargetConditionals.h>
/** @brief Defined as 1 on any Apple target (macOS or iOS). */
#  define CNA_TARGET_APPLE 1
#  if TARGET_OS_IPHONE
/** @brief Defined as 1 when targeting iOS (iPhone/iPad), device or simulator. */
#    define CNA_TARGET_IOS 1
#  else
/** @brief Defined as 1 when targeting macOS. */
#    define CNA_TARGET_MACOS 1
#  endif
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
#elif defined(CNA_TARGET_IOS)
        return TargetPlatform::iOS;
#else
        return TargetPlatform::Desktop;
#endif
    }

    /**
     * @brief Reports whether the current target is an Apple platform.
     *
     * True for both macOS (where getCurrentPlatform() reports TargetPlatform::Desktop) and iOS.
     *
     * @return true when building for macOS or iOS; false otherwise.
     */
    constexpr bool isApplePlatform()
    {
#if defined(CNA_TARGET_APPLE)
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Reports whether the current target is a mobile platform.
     *
     * Mobile platforms have an operating-system-driven application lifecycle: the app is
     * suspended in the background and must not touch the GPU while it is, has no resizable
     * window, and cannot spawn other processes.
     *
     * @return true when building for Android or iOS; false otherwise.
     */
    constexpr bool isMobilePlatform()
    {
        return getCurrentPlatform() == TargetPlatform::Android
            || getCurrentPlatform() == TargetPlatform::iOS;
    }

    /**
     * @brief Returns a stable human-readable name for the current platform.
     *
     * Intended for logs and diagnostics. Unlike getCurrentPlatform(), this distinguishes macOS
     * from the other desktop operating systems.
     *
     * @return One of "Web", "Android", "iOS", "macOS", "Windows", "Linux" or "Desktop".
     */
    constexpr const char* getCurrentPlatformName()
    {
#if defined(__EMSCRIPTEN__)
        return "Web";
#elif defined(__ANDROID__)
        return "Android";
#elif defined(CNA_TARGET_IOS)
        return "iOS";
#elif defined(CNA_TARGET_MACOS)
        return "macOS";
#elif defined(_WIN32)
        return "Windows";
#elif defined(__linux__)
        return "Linux";
#else
        return "Desktop";
#endif
    }
} // CNA
