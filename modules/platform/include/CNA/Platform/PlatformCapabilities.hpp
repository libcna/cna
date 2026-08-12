// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformCapability.hpp"

#include <vector>

namespace CNA::Platform {

    /**
     * @brief What a platform implementation can actually do.
     *
     * Obtained once from `IPlatform::GetCapabilities()` and cached by the owning subsystem —
     * `GraphicsDeviceManager`, `GameWindow`, `InputManager`. Querying capabilities inside a
     * per-frame path is a defect, not a style preference, which is why this is a plain aggregate
     * returned by value rather than a set of virtual calls.
     *
     * Every field defaults to `false`. An implementation states what it *can* do; a capability
     * it forgets to mention reads as unsupported, which is the safe direction — the failure mode
     * is a refused call, not a crash in an unimplemented path.
     *
     * Unsupported means **refused**, never silently ignored. A game may reasonably branch on
     * these values, so an implementation that reports `true` and then no-ops is worse than one
     * that reports `false` honestly.
     */
    struct PlatformCapabilities
    {
        /** @brief More than one window can exist at a time. */
        bool multipleWindows = false;
        /** @brief Windows report a display scale, and drawable pixel size may exceed logical size. */
        bool highDpi = false;
        /** @brief Multiple displays can be enumerated and targeted. */
        bool multipleDisplays = false;
        /** @brief Borderless fullscreen is available in addition to exclusive fullscreen. */
        bool borderlessFullscreen = false;
        /** @brief A renderer can obtain a usable native window handle. */
        bool nativeWindowHandle = false;
        /** @brief A CPU pixel buffer can be presented to the window. */
        bool surfacePresentation = false;
        /** @brief An OpenGL context can be created for a window. */
        bool openGlContext = false;
        /** @brief A Vulkan surface can be created for a window. */
        bool vulkanSurface = false;
        /** @brief The system clipboard can be read and written. */
        bool clipboard = false;
        /** @brief Text input events, as distinct from raw key events, are delivered. */
        bool textInput = false;
        /** @brief Input method editor composition and candidate events are delivered. */
        bool ime = false;
        /** @brief Keyboard state is exact — every press has a matching release, none synthesised. */
        bool exactKeyboardState = false;
        /** @brief Mouse coordinates are pixel-accurate rather than quantised to a coarser grid. */
        bool pixelAccurateMouse = false;
        /** @brief Relative (pointer-locked) mouse mode is available. */
        bool relativeMouse = false;
        /** @brief System and custom cursor shapes can be set. */
        bool cursorShapes = false;
        /** @brief The pointer can be read and moved in desktop coordinates, across windows. */
        bool globalPointer = false;
        /** @brief The currently connected input devices can be enumerated, not merely watched. */
        bool inputDeviceEnumeration = false;
        /** @brief Gamepads can be enumerated and read. */
        bool gamepad = false;
        /** @brief Raw, unmapped joysticks can be enumerated and read. */
        bool joystick = false;
        /** @brief Connected gamepads can rumble. */
        bool gamepadRumble = false;
        /** @brief Connected gamepads expose motion sensors. */
        bool gamepadSensors = false;
        /** @brief Standalone haptic devices are available. */
        bool haptics = false;
        /** @brief Device sensors (accelerometer, gyroscope) are available. */
        bool sensors = false;
        /** @brief Battery and power-source state can be queried. */
        bool powerInfo = false;
        /** @brief A native message box can be shown. */
        bool messageBox = false;
        /** @brief A native file or folder dialog can be shown. */
        bool nativeFileDialog = false;
        /** @brief A system tray icon with a menu can be created. */
        bool tray = false;
        /** @brief Cameras can be enumerated and opened. */
        bool camera = false;
        /** @brief The platform manages the application entry point (SDL-on-Android style). */
        bool managedEntrypoint = false;
    };

    /**
     * @brief Reads one capability from a capability set by enum value.
     *
     * The bridge between the cached struct and the enumerable enum. Implemented with an
     * exhaustive `switch` and no `default` arm, so a PlatformCapability added without a matching
     * struct field fails to compile instead of silently reading as unsupported.
     *
     * @param capabilities The capability set to read.
     * @param capability Which capability to read.
     * @return True if @p capabilities reports @p capability as supported.
     */
    [[nodiscard]] bool Supports(const PlatformCapabilities& capabilities, PlatformCapability capability);

    /**
     * @brief Returns every PlatformCapability value, in declaration order.
     *
     * Lets the conformance suite (and any capability-matrix report) enumerate the whole set
     * mechanically, so a newly added capability cannot be forgotten by a test that hardcodes a
     * list.
     *
     * @return A stable, complete list of every capability.
     */
    [[nodiscard]] const std::vector<PlatformCapability>& AllCapabilities();

} // namespace CNA::Platform
