// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/NativeWindowSystem.hpp"

#include <cstdint>
#include <string>

namespace CNA::Platform {

    /**
     * @brief An opaque description of a native window, handed to a graphics renderer once at
     * initialization.
     *
     * This is the entire contract between the platform layer and a renderer's window
     * dependency. A renderer receives one of these and then talks to its own graphics API
     * directly; it never calls back through the platform to draw, and it never learns which
     * platform implementation created the window. That is what allows Vulkan, OpenGL, DirectX,
     * GDI or Glide to work identically whether the window came from SDL3, a future SDL2 or
     * native implementation, or no window system at all.
     *
     * The struct is trivially copyable and owns nothing. Its lifetime is the window's lifetime;
     * a renderer that stores it must not outlive the window it describes.
     *
     * ### What each field means, per system
     *
     * A pointer field that is not listed for a system is null. Reading a field that does not
     * apply is a bug, which is what the typed accessors below exist to prevent.
     *
     * | System     | `display`      | `window`          | `windowId`        | `surface`      |
     * |------------|----------------|-------------------|-------------------|----------------|
     * | `Win32`    | —              | `HWND`            | —                 | —              |
     * | `X11`      | `Display*`     | —                 | `Window` (XID)    | —              |
     * | `Wayland`  | `wl_display*`  | —                 | —                 | `wl_surface*`  |
     * | `Cocoa`    | —              | `NSWindow*`       | —                 | —              |
     * | `Android`  | —              | `ANativeWindow*`  | —                 | —              |
     * | `Web`      | —              | —                 | —                 | —              |
     * | `Headless` | —              | —                 | —                 | —              |
     * | `Terminal` | —              | —                 | —                 | —              |
     *
     * ### Why X11's window is an integer and not a pointer
     *
     * An X11 `Window` is an XID — a 32-bit unsigned integer server-side resource identifier, not
     * an address. Storing it in the `window` pointer field and casting back is the classic
     * interop bug in this area: it happens to work on LP64 where a pointer is wide enough to
     * carry the value, and it silently truncates or traps elsewhere. It also makes a null check
     * meaningless, because XID `0` (`None`) is a legitimate-looking null pointer while a valid
     * XID is not a dereferenceable address. So X11's window lives in its own integer field,
     * `windowId`, and `window` stays null for X11.
     */
    struct NativeWindowHandle
    {
        /** @brief Which windowing system the other fields belong to. Never assume; always check. */
        NativeWindowSystem system = NativeWindowSystem::Unknown;

        /** @brief X11 `Display*` or Wayland `wl_display*`; null on every other system. */
        void* display = nullptr;

        /** @brief Win32 `HWND`, Cocoa `NSWindow*` or Android `ANativeWindow*`; null on every other system. */
        void* window = nullptr;

        /** @brief Wayland `wl_surface*`; null on every other system. */
        void* surface = nullptr;

        /** @brief X11 `Window` XID — an integer resource id, not an address. Zero on every other system. */
        std::uint64_t windowId = 0;
    };

    /** @brief A validated Win32 native window. */
    struct Win32NativeWindow
    {
        /** @brief The window handle, as a `HWND`. Never null in a successfully retrieved value. */
        void* hwnd = nullptr;
    };

    /** @brief A validated X11 native window. */
    struct X11NativeWindow
    {
        /** @brief The X11 `Display*`. Never null in a successfully retrieved value. */
        void* display = nullptr;
        /** @brief The X11 `Window` XID. Never zero in a successfully retrieved value. */
        std::uint64_t window = 0;
    };

    /** @brief A validated Wayland native window. */
    struct WaylandNativeWindow
    {
        /** @brief The `wl_display*`. Never null in a successfully retrieved value. */
        void* display = nullptr;
        /** @brief The `wl_surface*`. Never null in a successfully retrieved value. */
        void* surface = nullptr;
    };

    /** @brief A validated Cocoa native window. */
    struct CocoaNativeWindow
    {
        /** @brief The `NSWindow*`. Never null in a successfully retrieved value. */
        void* window = nullptr;
    };

    /** @brief A validated Android native window. */
    struct AndroidNativeWindow
    {
        /** @brief The `ANativeWindow*`. Never null in a successfully retrieved value. */
        void* window = nullptr;
    };

    /**
     * @brief Retrieves a handle as a Win32 window, if it is one.
     *
     * Renderers use these accessors rather than casting `window` themselves. That is the whole
     * point: a renderer asked to initialize against a handle from a different windowing system
     * gets a `false` it must handle, instead of a plausible-looking pointer that crashes on
     * first use.
     *
     * @param handle The handle to inspect.
     * @param out Receives the validated window; untouched when this returns false.
     * @return True if @p handle is a Win32 handle with a non-null window.
     */
    [[nodiscard]] bool TryGetWin32(const NativeWindowHandle& handle, Win32NativeWindow& out);

    /**
     * @brief Retrieves a handle as an X11 window, if it is one.
     *
     * @param handle The handle to inspect.
     * @param out Receives the validated display and XID; untouched when this returns false.
     * @return True if @p handle is an X11 handle with a non-null display and a non-zero XID.
     */
    [[nodiscard]] bool TryGetX11(const NativeWindowHandle& handle, X11NativeWindow& out);

    /**
     * @brief Retrieves a handle as a Wayland window, if it is one.
     *
     * @param handle The handle to inspect.
     * @param out Receives the validated display and surface; untouched when this returns false.
     * @return True if @p handle is a Wayland handle with a non-null display and surface.
     */
    [[nodiscard]] bool TryGetWayland(const NativeWindowHandle& handle, WaylandNativeWindow& out);

    /**
     * @brief Retrieves a handle as a Cocoa window, if it is one.
     *
     * @param handle The handle to inspect.
     * @param out Receives the validated window; untouched when this returns false.
     * @return True if @p handle is a Cocoa handle with a non-null window.
     */
    [[nodiscard]] bool TryGetCocoa(const NativeWindowHandle& handle, CocoaNativeWindow& out);

    /**
     * @brief Retrieves a handle as an Android window, if it is one.
     *
     * @param handle The handle to inspect.
     * @param out Receives the validated window; untouched when this returns false.
     * @return True if @p handle is an Android handle with a non-null window.
     */
    [[nodiscard]] bool TryGetAndroid(const NativeWindowHandle& handle, AndroidNativeWindow& out);

    /**
     * @brief Reports whether a handle describes a real native window.
     *
     * False for `Unknown`, `Headless`, `Terminal` and `Web`, and for any handle whose required
     * fields are unset. A renderer that cannot work without a native window should check this
     * and refuse with a clear message, rather than failing later inside its own graphics API.
     *
     * @param handle The handle to inspect.
     * @return True if the handle carries a usable native window for its declared system.
     */
    [[nodiscard]] bool HasNativeWindow(const NativeWindowHandle& handle);

    /**
     * @brief Builds a short human-readable description of a handle, for diagnostics.
     *
     * Reports the system name and which fields are populated. Deliberately does **not** print
     * pointer values: they are meaningless across processes and invite treating a log line as a
     * usable handle.
     *
     * @param handle The handle to describe.
     * @return A description such as `"X11(display=set, windowId=set)"` or `"Headless"`.
     */
    [[nodiscard]] std::string Describe(const NativeWindowHandle& handle);

} // namespace CNA::Platform
