// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Platform {

    /**
     * @brief Identifies which native windowing system a NativeWindowHandle refers to.
     *
     * A graphics renderer receives a NativeWindowHandle at initialization and must know how to
     * interpret its pointers before casting them. This enum is that discriminator: it is what
     * turns a mismatch between "the handle I was given" and "the handle I expected" into a
     * detected error instead of a crash.
     *
     * The set is derived from the native window properties CNA actually consumes today (see
     * docs/platform-renderer-sdl-audit.md and plans/plan_platform.md), not from what a windowing
     * abstraction might theoretically expose.
     */
    enum class NativeWindowSystem
    {
        /** @brief No native window, or a window whose system could not be determined. */
        Unknown,
        /** @brief Microsoft Windows. The window value is an `HWND`. */
        Win32,
        /** @brief X11. Carries a `Display*` plus an integer `Window` XID. */
        X11,
        /** @brief Wayland. Carries a `wl_display*` plus a `wl_surface*`. */
        Wayland,
        /** @brief macOS. The window value is an `NSWindow*`. */
        Cocoa,
        /** @brief Android. The window value is an `ANativeWindow*`. */
        Android,
        /** @brief Web/Emscripten. The drawing target is a canvas selected by the host page, not a pointer. */
        Web,
        /**
         * @brief A platform that renders without any native window at all.
         *
         * Used by the headless platform. Every pointer is null, and a renderer that requires a
         * real native handle must refuse rather than dereference one.
         */
        Headless,
        /**
         * @brief A character-cell terminal with no native graphical window.
         *
         * Every pointer is null. Kept distinct from `Headless` so renderer-selection diagnostics
         * can explain that CPU pixels may be presented through the terminal surface presenter.
         */
        Terminal
    };

    /**
     * @brief Returns the stable, human-readable name of a windowing system.
     *
     * Intended for diagnostics and error messages — in particular for reporting a mismatch
     * between the handle a renderer expected and the handle it received.
     *
     * @param system The windowing system to name.
     * @return A stable name such as `"Win32"` or `"Wayland"`; `"Unknown"` for an unrecognised value.
     */
    [[nodiscard]] const std::string& ToString(NativeWindowSystem system);

} // namespace CNA::Platform
