// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

namespace CNA::Devices
{
    /**
     * @brief State of a `Camera` instance's access to its underlying device.
     *
     * Mirrors `Microsoft::Devices::Sensors::SensorState`'s own honesty principle:
     * a consumer can always cheaply ask "can I use this right now" rather than
     * finding out by a confusing runtime failure. CNA extension — no XNA/WP7
     * equivalent exists.
     */
    enum class CameraState
    {
        /** @brief No camera hardware/backend is available on this platform or device. */
        NotSupported,
        /** @brief Never opened, or explicitly closed. */
        Closed,
        /** @brief A device was opened; the platform's permission decision is still pending. */
        Opening,
        /** @brief The user (or platform policy) denied camera access. */
        Denied,
        /** @brief Access approved; frames may be polled via `Camera::TryAcquireFrame()`. */
        Ready,
        /**
         * @brief The device disconnected or failed after being `Ready`.
         *
         * A platform that can observe removal reports it here; other implementations may keep
         * supplying blank frames until the session is closed.
         */
        Lost
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
