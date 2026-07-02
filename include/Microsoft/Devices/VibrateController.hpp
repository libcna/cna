// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices
{
    /**
     * @brief Provides control over the device's vibration motor.
     *
     * Matches the real Windows Phone 7 API shape: VibrateController is not
     * static. There is a single shared instance, reached via
     * getDefaultProperty(), and Start()/Stop() are instance methods called
     * on it (e.g. VibrateController::getDefaultProperty()->Start(...)).
     * The constructor is private because the underlying implementation
     * drives a single physical vibration motor per device — there is
     * nothing a second, independently-constructed instance could
     * meaningfully represent — so getDefaultProperty() is the only way to
     * obtain an instance, mirroring how this codebase's other device
     * singletons (e.g. Microsoft::Xna::Framework::Audio::Microphone) are
     * shaped.
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
        /**
         * @brief Gets the default vibrate controller for the current device.
         *
         * @return Pointer to the single shared VibrateController instance. Never null.
         */
        [[nodiscard]] static VibrateController* getDefaultProperty();

        /**
         * @brief Starts device vibration for the specified duration.
         *
         * @param duration Requested vibration duration, in the inclusive
         * range [System::TimeSpan::Zero, System::TimeSpan::FromSeconds(5)],
         * matching the documented Windows Phone 7 maximum. If the current
         * platform or device exposes no haptic/vibration capability, this
         * call is a silent no-op.
         *
         * @throws System::ArgumentOutOfRangeException If duration is
         * negative or greater than 5 seconds.
         */
        void Start(const System::TimeSpan& duration);

        /**
         * @brief Starts device vibration for the specified duration and intensity.
         *
         * CNA-specific extension beyond the WP7 API surface: exposes SDL3's
         * rumble-strength parameter directly, since CNA targets more capable
         * hardware than 2010-era WP7 phones (whose vibration motors were
         * single-intensity on/off buzzers, hence the plain WP7
         * Start(TimeSpan) has no intensity concept). Start(const
         * System::TimeSpan&) delegates to this overload with intensity 1.0f,
         * so it is unaffected by this extension.
         *
         * @param duration Requested vibration duration, in the inclusive
         * range [System::TimeSpan::Zero, System::TimeSpan::FromSeconds(5)].
         * If the current platform or device exposes no haptic/vibration
         * capability, this call is a silent no-op.
         * @param intensity Rumble strength, clamped to [0.0f, 1.0f].
         *
         * @throws System::ArgumentOutOfRangeException If duration is
         * negative or greater than 5 seconds.
         */
        NOXNA void Start(const System::TimeSpan& duration, float intensity);

        /**
         * @brief Immediately stops any vibration started via Start().
         *
         * If no vibration is currently active, or no haptic device could be
         * opened, this call is a silent no-op.
         */
        void Stop();

        /**
         * @brief Gets whether the current platform/device exposes vibration hardware.
         *
         * CNA-specific extension beyond the WP7 API surface: unlike
         * Start()/Stop(), which always silently no-op when no suitable
         * device is available, this lets calling code check ahead of time.
         * Does not hold a haptic device open as a side effect of probing —
         * if no device is already open from a prior Start() call, one is
         * opened only long enough to test viability, then closed again.
         *
         * @return true if a suitable haptic device is available; otherwise false.
         */
        NOXNA [[nodiscard]] bool getIsSupportedProperty();

        /**
         * @brief Gets the name of the haptic device that Start() would use.
         *
         * CNA-specific extension for debug/diagnostic UI. Same probe-only
         * semantics as getIsSupportedProperty(): does not hold a device open
         * as a side effect if one is not already open.
         *
         * @return Device name, or an empty string if no suitable device is available.
         */
        NOXNA [[nodiscard]] std::string getDeviceNameProperty();

        /**
         * @brief Starts independent large/small motor vibration for the specified duration.
         *
         * CNA-specific extension mirroring
         * Microsoft::Xna::Framework::Input::GamePad::SetVibration()'s
         * two-motor magnitude semantics, but for the phone/haptic device
         * path — useful for games wanting a strong "hit" pulse on one motor
         * plus a subtler background rumble on the other simultaneously. If
         * the current device exposes no dual-motor rumble capability, this
         * call is a silent no-op. Stop() also stops any effect started this
         * way.
         *
         * @param largeMotor Low-frequency motor strength, clamped to [0.0f, 1.0f].
         * @param smallMotor High-frequency motor strength, clamped to [0.0f, 1.0f].
         * @param duration Requested vibration duration, in the inclusive
         * range [System::TimeSpan::Zero, System::TimeSpan::FromSeconds(5)].
         *
         * @throws System::ArgumentOutOfRangeException If duration is
         * negative or greater than 5 seconds.
         */
        NOXNA void StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration);

    private:
        /** @brief Private constructor; use getDefaultProperty() to obtain the singleton instance. */
        VibrateController() = default;
    };
} // namespace Microsoft::Devices
