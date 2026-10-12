// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Platform {

    // The camera and the system tray are installed on first use rather than with the platform:
    // a program that never uses them neither links them nor -- for the camera -- starts the
    // platform's camera subsystem, which enumerating cameras does (docs/link-size-architecture.md).
    // Until a service is enabled, its capability (`PlatformCapabilities::camera`, `::tray`) is
    // false and its accessor (IPlatform::GetCamera(), GetTray()) is null; once enabled, both
    // report what the platform supports, as they always did. CNA::Devices::Camera and SystemTray
    // enable their service before every use, so an application never calls these itself.

    /**
     * @brief Makes the platform's camera service available to this program.
     *
     * Idempotent and thread-safe. A platform without cameras still reports none.
     */
    void EnableCameraService();

    /**
     * @brief Makes the platform's system-tray service available to this program.
     *
     * Idempotent and thread-safe. A platform without a tray still reports none.
     */
    void EnableTrayService();
}
