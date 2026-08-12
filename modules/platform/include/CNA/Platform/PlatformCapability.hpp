// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Platform {

    /**
     * @brief Every capability a platform implementation may or may not provide.
     *
     * This enum exists alongside the PlatformCapabilities struct rather than instead of it, and
     * the split is deliberate. The struct is what callers read and cache, once, because a
     * capability query must never appear in a per-frame path. This enum is what makes the set
     * *enumerable*: it lets a test walk every capability exhaustively, and it forces
     * `Supports()`'s switch to name each one, so adding a capability is a compiler diagnostic
     * rather than a silently inherited `false`.
     *
     * A capability reported as unsupported is a promise that the corresponding calls **refuse
     * deterministically** — never that they quietly do nothing.
     */
    enum class PlatformCapability
    {
        /** @brief More than one window can exist at a time. */
        MultipleWindows,
        /** @brief Windows report a display scale, and drawable pixel size may exceed logical size. */
        HighDpi,
        /** @brief Multiple displays can be enumerated and targeted. */
        MultipleDisplays,
        /** @brief Borderless (desktop-spanning) fullscreen is available in addition to exclusive fullscreen. */
        BorderlessFullscreen,
        /** @brief A renderer can obtain a usable native window handle. */
        NativeWindowHandle,
        /** @brief A CPU pixel buffer can be presented to the window (IPlatformSurfacePresenter). */
        SurfacePresentation,
        /** @brief An OpenGL context can be created for a window. */
        OpenGlContext,
        /** @brief A Vulkan surface can be created for a window. */
        VulkanSurface,
        /** @brief The system clipboard can be read and written. */
        Clipboard,
        /** @brief Text input events, as distinct from raw key events, are delivered. */
        TextInput,
        /** @brief Input method editor composition and candidate events are delivered. */
        Ime,
        /**
         * @brief Keyboard state is exact — every key press has a matching release.
         *
         * False on platforms that deliver key presses without releases and must therefore
         * synthesise key-up from a timeout. XNA's `Keyboard::GetState()` is a level query, so a
         * game that depends on precise held-key state needs this.
         */
        ExactKeyboardState,
        /** @brief Mouse coordinates are pixel-accurate rather than quantised to a coarser grid. */
        PixelAccurateMouse,
        /** @brief Relative (pointer-locked) mouse mode is available. */
        RelativeMouse,
        /** @brief System and custom cursor shapes can be set. */
        CursorShapes,
        /**
         * @brief The pointer can be read and moved in desktop coordinates, across windows.
         *
         * Distinct from the window-scoped pointer calls, which every windowing platform has.
         * This is what makes a drag survive the pointer leaving the window, and a platform with
         * exactly one full-screen surface — a terminal, a console — genuinely does not have it.
         */
        GlobalPointer,
        /**
         * @brief The currently connected input devices can be enumerated.
         *
         * Distinct from receiving `DeviceEvent`s, which report *changes*. A platform may deliver
         * hot-plug events perfectly and still be unable to answer "what is attached right now",
         * which is the question a touch-capability probe asks at startup.
         */
        InputDeviceEnumeration,
        /** @brief Gamepads can be enumerated and read. */
        Gamepad,
        /** @brief Connected gamepads can rumble. */
        GamepadRumble,
        /** @brief Connected gamepads expose motion sensors. */
        GamepadSensors,
        /** @brief Standalone haptic devices are available. */
        Haptics,
        /** @brief Device sensors (accelerometer, gyroscope) are available. */
        Sensors,
        /** @brief Battery and power-source state can be queried. */
        PowerInfo,
        /** @brief A native message box can be shown. */
        MessageBox,
        /** @brief A native file or folder dialog can be shown. */
        NativeFileDialog,
        /** @brief A system tray icon with a menu can be created. */
        Tray,
        /** @brief Cameras can be enumerated and opened. */
        Camera,
        /**
         * @brief The platform manages the application entry point.
         *
         * True where the host renames `main()` or otherwise calls into the application itself,
         * as SDL does on Android. Applications must include the CNA entrypoint header for this
         * to work; see docs/platform-entrypoint-audit.md.
         */
        ManagedEntrypoint
    };

    /**
     * @brief Returns the stable, human-readable name of a capability.
     *
     * Used in diagnostics and in the refusal message a capability-gated call produces, so a
     * caller learns which capability was missing rather than only that something failed.
     *
     * @param capability The capability to name.
     * @return A stable name such as `"HighDpi"`.
     */
    [[nodiscard]] const std::string& ToString(PlatformCapability capability);

} // namespace CNA::Platform
