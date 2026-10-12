// SPDX-License-Identifier: MS-PL
#pragma once
// Whether the SDL3 platform supports its optional camera and tray services, answered without the
// services themselves: Sdl3Platform reports these capabilities from here, and links the providers
// (Sdl3Camera.cpp, Sdl3Tray.cpp) only when a program enables them (CNA/Platform/OptionalServices.hpp).

namespace CNA::Platform::Sdl3 {

    /** @brief Whether a system tray icon can be created on this target. @return True on desktop targets. */
    [[nodiscard]] bool TraySupported() noexcept;

    /**
     * @brief Whether cameras can be enumerated and opened: a camera driver is compiled in and SDL's
     *        camera subsystem starts. Asking starts it (see EnsureCameraSubsystem()).
     * @return True when both hold.
     */
    [[nodiscard]] bool CameraSupported();

    /** @brief Starts SDL's camera subsystem once, and answers whether it is up. @return Whether it is. */
    [[nodiscard]] bool EnsureCameraSubsystem();

    /** @brief Whether SDL's camera subsystem is running, without starting it. @return Whether it is. */
    [[nodiscard]] bool CameraSubsystemRunning();
}
