// SPDX-License-Identifier: MS-PL

#pragma once

#include <mutex>
#include <string>

#include <SDL3/SDL_haptic.h>

#include "Microsoft/Devices/Detail/IVibrateBackend.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Detail
{
    /**
     * @brief `IVibrateBackend` implementation backed by SDL3's `SDL_Haptic` API (Task VIB-002).
     *
     * This is the only production `IVibrateBackend` implementation in this
     * codebase today, used on every platform (desktop and Android alike).
     * It deliberately excludes any haptic device that is also a connected
     * joystick/gamepad's own rumble motor, so it never competes with
     * `Microsoft::Xna::Framework::Input::GamePad::SetVibration()` for the
     * same physical actuator (see `IsConnectedGamepadHapticDevice()` in the
     * `.cpp`) — if the only haptic-capable device present is a game
     * controller, this backend behaves exactly as if no device were present
     * at all.
     *
     * **Why one backend currently serves both "the phone's vibration motor"
     * and "a generic desktop haptic device" (Task VIB-002's original
     * concern):** on Android, SDL3's own bundled Android haptic backend
     * (`third_party/SDL/src/haptic/android/SDL_syshaptic.c`) enumerates
     * `Context.VIBRATOR_SERVICE` — the phone's own built-in vibrator — as an
     * `SDL_Haptic` device, so `OpenFirstHapticDevice()` below reaches it with
     * no Android-specific code needed (re-confirmed by `VIB-003`). On
     * desktop, the same code instead opens whatever generic force-feedback
     * hardware SDL enumerates (e.g. a USB wheel), which is not "a phone
     * vibrating" at all — this is accepted, documented `NOXNA`-flavored
     * desktop behavior (there being no phone to vibrate on a desktop in the
     * first place), not a compatibility risk: strict XNA `Start(TimeSpan)`
     * on a real Windows Phone device has no desktop code path to begin with,
     * so nothing here can rumble a desktop gadget "as if it were the phone"
     * in a way real WP7 content could ever observe or depend on.
     */
    class SdlHapticVibrateBackend final : public IVibrateBackend
    {
    public:
        SdlHapticVibrateBackend() = default;
        ~SdlHapticVibrateBackend() override;

        SdlHapticVibrateBackend(const SdlHapticVibrateBackend&) = delete;
        SdlHapticVibrateBackend& operator=(const SdlHapticVibrateBackend&) = delete;

        void Start(const System::TimeSpan& duration, float intensity) override;
        void Stop() override;
        [[nodiscard]] bool IsSupported() override;
        [[nodiscard]] std::string GetDeviceName() override;
        void StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration) override;

    private:
        /** @brief Guards every member below against concurrent access from multiple application threads. */
        std::mutex mutex_;

        /** @brief The currently-open haptic device, if any. Reused across calls so Stop() can act on it. */
        SDL_Haptic* haptic_ = nullptr;

        /**
         * @brief The currently-uploaded `SDL_HAPTIC_LEFTRIGHT` effect started by StartLeftRight(), if any.
         *
         * `SDL_StopHapticEffects(haptic_)` (used by Stop()) already stops its
         * playback, but doesn't free the uploaded effect slot — Stop() also
         * calls `SDL_DestroyHapticEffect()` on this ID for that. `-1` means
         * "none uploaded".
         */
        SDL_HapticEffectID leftRightEffectId_ = -1;

        /**
         * @brief True once this backend has made a successful `SDL_InitSubSystem(SDL_INIT_HAPTIC)` call.
         *
         * Paired with exactly one `SDL_QuitSubSystem()` call from the
         * destructor.
         */
        bool subsystemHeld_ = false;

        bool EnsureHapticSubsystemInitialized();
        SDL_Haptic* OpenFirstHapticDevice();
        SDL_Haptic* AcquireHapticDeviceForProbe(bool& openedTemporary);
        void DestroyLeftRightEffectIfAny();
    };
} // namespace Microsoft::Devices::Detail
