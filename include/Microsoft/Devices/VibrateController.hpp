// SPDX-License-Identifier: MS-PL

#pragma once

#include "System/TimeSpan.hpp"

namespace Microsoft::Devices
{
    /**
     * @brief Provides control over the device's vibration motor.
     *
     * Unlike the sensor classes in Microsoft::Devices::Sensors,
     * VibrateController is a pure static utility: it has no instance state,
     * does not derive from SensorBase<T> or System::IDisposable, and is
     * never instantiated.
     *
     * This targets the phone/device vibration motor only. On desktop, a
     * connected game controller's rumble motors are deliberately excluded
     * from device selection so that this class never competes with
     * Microsoft::Xna::Framework::Input::GamePad::SetVibration() for the same
     * physical actuator; if the only haptic-capable device present is a game
     * controller, Start() is a silent no-op, matching the behavior of a
     * desktop machine with no vibration hardware at all.
     */
    class VibrateController final
    {
    public:
        /** @brief Deleted default constructor; this class is static-only. */
        VibrateController() = delete;

    public:
        /**
         * @brief Starts device vibration for the specified duration.
         *
         * @param duration Requested vibration duration. Silently clamped to
         * the inclusive range [System::TimeSpan::Zero,
         * System::TimeSpan::FromSeconds(5)], matching the documented Windows
         * Phone 7 maximum. If the current platform or device exposes no
         * haptic/vibration capability, this call is a silent no-op.
         */
        static void Start(const System::TimeSpan& duration);

        /**
         * @brief Immediately stops any vibration started via Start().
         *
         * If no vibration is currently active, or no haptic device could be
         * opened, this call is a silent no-op.
         */
        static void Stop();
    };
} // namespace Microsoft::Devices
