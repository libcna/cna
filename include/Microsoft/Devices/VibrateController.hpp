// SPDX-License-Identifier: MS-PL

#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Devices/Detail/IVibrateBackend.hpp"
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
         * matching the documented Windows Phone 7 maximum (archived MSDN
         * page for this method, `ff403287(v=vs.105)`, re-verified Task
         * VIB-006: "Valid times are between 0 and 5 seconds. Values greater
         * than 5 or less than 0 raise an exception."). If the current
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
         * Note: intensity 0.0f is not special-cased into an implicit Stop()
         * — it still uploads and plays a zero-strength SDL rumble effect for
         * the full duration. In practice this is inert (zero strength has no
         * physical effect), but it is not equivalent to skipping the call
         * entirely; call Stop() directly if that distinction matters to a
         * caller (Task DEVICES-0030).
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
         * @note On Android (Task VIB-003, re-verified 2026-07-06 against
         * SDL3's actual Android haptic backend source), the phone's own
         * built-in vibrator does not receive two independent motor
         * intensities: SDL3's Android `SDL_Haptic` backend blends
         * largeMotor/smallMotor into a single intensity
         * (`large*0.6 + small*0.4`) before handing it to
         * `Context.VIBRATOR_SERVICE`. True independent dual-motor output is
         * only reachable via SDL3's separate gamepad-rumble path
         * (`Microsoft::Xna::Framework::Input::GamePad::SetVibration()`), not
         * this one. See `docs/devices-android.md`'s "Vibration" section for
         * the full source-level trail.
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

    public:
        /**
         * @brief Destroys the vibrate controller, releasing its backend's resources.
         *
         * Only ever runs once (Task P5-11), when the process-lifetime
         * singleton returned by getDefaultProperty() is itself destroyed
         * as part of normal program termination. NOXNA in spirit (the real
         * WP7 VibrateController has no Dispose/destructor concept at all —
         * it's a fire-and-forget static design), but not tagged since it's
         * not a new *public API surface* addition a game would ever call
         * directly.
         */
        ~VibrateController();

        /**
         * @brief Test-only hook (Task VIB-002): replaces the real,
         * platform-default backend with a caller-supplied one (typically a
         * test fake), so Start()/Stop()/StartLeftRight() delegation can be
         * exercised on any host without depending on whatever haptic
         * hardware (or lack of it) happens to be present.
         *
         * Mirrors Compass/Motion's existing `SetBackendForTesting()`
         * pattern. Unlike those sensor classes, `VibrateController` has no
         * persistent "started" state to protect against swapping a live
         * backend out from under an active session — every call is
         * independently fire-and-forget — so no equivalent guard is needed
         * here.
         *
         * @param backend Replacement backend; pass nullptr to restore the
         * platform-default (`Detail::SdlHapticVibrateBackend`) behavior.
         */
        NOXNA void SetBackendForTesting(std::unique_ptr<Detail::IVibrateBackend> backend);

    private:
        /** @brief Private constructor; use getDefaultProperty() to obtain the singleton instance. */
        VibrateController();

        /** @brief Guards backend_ against concurrent replacement via SetBackendForTesting() while another thread is mid-call. */
        std::mutex backendMutex_;

        /**
         * @brief Active backend, selected at construction time.
         *
         * Defaults to `Detail::SdlHapticVibrateBackend`; replaceable via
         * `SetBackendForTesting()`.
         */
        std::unique_ptr<Detail::IVibrateBackend> backend_;
    };
} // namespace Microsoft::Devices
