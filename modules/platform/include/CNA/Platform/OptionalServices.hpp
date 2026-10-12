// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Platform {

    // The camera and the system tray are optional services, installed on first use rather than
    // with the platform, so that a program that never uses them does not link them
    // (docs/link-size-architecture.md). Their capabilities (`PlatformCapabilities::camera`,
    // `::tray`, IPlatform::Supports()) report platform support whether or not a service is
    // enabled; the service itself (IPlatform::GetCamera(), GetTray()) exists once it is.
    // CNA::Devices::Camera and SystemTray enable their service before using it, so an application
    // never calls these itself. Asking whether cameras are supported starts the platform's camera
    // subsystem; a program that never asks about the camera does not start it.

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
