//
// Created by robertvokac on 6/1/25.
//

/**
 * @file CNA/Platform.hpp
 * @brief Compile-time target platform identification.
 *
 * Besides the CNA::Platform enumeration, this header defines the CNA_PLATFORM_* object-like
 * macros the rest of the codebase uses in preprocessor conditions. Apple targets need them:
 * macOS and iOS share the __APPLE__ macro and differ only in a TargetConditionals.h value, and
 * repeating that two-level test in every translation unit is how the two silently drift apart.
 */

#pragma once

#if defined(__APPLE__)
#  include <TargetConditionals.h>
/** @brief Defined as 1 on any Apple target (macOS or iOS). */
#  define CNA_PLATFORM_APPLE 1
#  if TARGET_OS_IPHONE
/** @brief Defined as 1 when targeting iOS (iPhone/iPad), device or simulator. */
#    define CNA_PLATFORM_IOS 1
#  else
/** @brief Defined as 1 when targeting macOS. */
#    define CNA_PLATFORM_MACOS 1
#  endif
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
#elif defined(CNA_PLATFORM_IOS)
        return Platform::iOS;
#else
        return Platform::Desktop;
#endif
    }

    /**
     * @brief Reports whether the current target is an Apple platform.
     *
     * True for both macOS (where getCurrentPlatform() reports Platform::Desktop) and iOS.
     *
     * @return true when building for macOS or iOS; false otherwise.
     */
    constexpr bool isApplePlatform()
    {
#if defined(CNA_PLATFORM_APPLE)
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
        return getCurrentPlatform() == Platform::Android || getCurrentPlatform() == Platform::iOS;
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
#elif defined(CNA_PLATFORM_IOS)
        return "iOS";
#elif defined(CNA_PLATFORM_MACOS)
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
