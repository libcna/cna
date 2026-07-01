// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/VibrateController.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>

namespace Microsoft::Devices
{
    namespace
    {
        // File-local state: the single haptic device opened by Start(), reused
        // across calls so Stop() can act on it. Never explicitly closed; it is
        // released by the OS/SDL_Quit at process exit, matching XNA's fire-and-
        // forget static VibrateController design (no Dispose concept exists).
        SDL_Haptic* g_haptic = nullptr;

        System::TimeSpan ClampVibrationDuration(const System::TimeSpan& duration)
        {
            static const System::TimeSpan MaxVibrationDuration = System::TimeSpan::FromSeconds(5);

            if (duration < System::TimeSpan::Zero)
            {
                return System::TimeSpan::Zero;
            }

            if (duration > MaxVibrationDuration)
            {
                return MaxVibrationDuration;
            }

            return duration;
        }

        bool EnsureHapticSubsystemInitialized()
        {
            if (SDL_WasInit(SDL_INIT_HAPTIC))
            {
                return true;
            }

            return SDL_InitSubSystem(SDL_INIT_HAPTIC);
        }

        // Returns true if a haptic device's name matches a currently
        // connected joystick/gamepad's name.
        //
        // On Linux (and likely similarly on other desktop backends), a
        // rumble-capable game controller is enumerated by SDL_GetHaptics()
        // as its own independent haptic device, entirely separate from the
        // joystick/gamepad subsystem that
        // Microsoft::Xna::Framework::Input::GamePad::SetVibration() uses
        // (SDL_RumbleGamepad). SDL provides no direct SDL_HapticID <->
        // SDL_JoystickID correlation, so device name is the most reliable
        // signal available without invasively opening every connected
        // joystick just to probe it.
        bool IsConnectedGamepadHapticDevice(SDL_HapticID hapticId)
        {
            const char* hapticName = SDL_GetHapticNameForID(hapticId);
            if (hapticName == nullptr)
            {
                return false;
            }

            int joystickCount = 0;
            SDL_JoystickID* joysticks = SDL_GetJoysticks(&joystickCount);
            if (joysticks == nullptr)
            {
                return false;
            }

            bool matchesGamepad = false;
            for (int i = 0; i < joystickCount; ++i)
            {
                const char* joystickName = SDL_GetJoystickNameForID(joysticks[i]);
                if (joystickName != nullptr && SDL_strcmp(joystickName, hapticName) == 0)
                {
                    matchesGamepad = true;
                    break;
                }
            }

            SDL_free(joysticks);
            return matchesGamepad;
        }

        // Opens the first enumerable haptic device that is not also a
        // connected joystick/gamepad, if any.
        //
        // NOTE: on Android, SDL3's bundled Android haptic backend automatically
        // polls Context.VIBRATOR_SERVICE and registers the phone's own
        // vibration motor as a haptic device once SDL_INIT_HAPTIC is
        // initialized. SDL_InitHapticRumble()/SDL_PlayHapticRumble() below
        // therefore already reach Vibrator.vibrate(milliseconds) on Android
        // with no custom JNI/Java bridge code needed here. No #ifdef __ANDROID__
        // branch is required: the same code path serves both Desktop and
        // Android.
        SDL_Haptic* OpenFirstHapticDevice()
        {
            int hapticCount = 0;
            SDL_HapticID* haptics = SDL_GetHaptics(&hapticCount);

            if (haptics == nullptr || hapticCount <= 0)
            {
                if (haptics != nullptr)
                {
                    SDL_free(haptics);
                }
                return nullptr;
            }

            SDL_Haptic* opened = nullptr;

            for (int i = 0; i < hapticCount; ++i)
            {
                if (IsConnectedGamepadHapticDevice(haptics[i]))
                {
                    continue;
                }

                opened = SDL_OpenHaptic(haptics[i]);
                if (opened != nullptr)
                {
                    break;
                }
            }

            SDL_free(haptics);
            return opened;
        }
    } // namespace

    void VibrateController::Start(const System::TimeSpan& duration)
    {
        const System::TimeSpan clampedDuration = ClampVibrationDuration(duration);
        const Uint32 durationMs = static_cast<Uint32>(clampedDuration.getTotalMillisecondsProperty());

        if (!EnsureHapticSubsystemInitialized())
        {
            return; // Silent no-op: haptic subsystem unavailable on this platform.
        }

        if (g_haptic == nullptr)
        {
            g_haptic = OpenFirstHapticDevice();
        }

        if (g_haptic == nullptr)
        {
            return; // Silent no-op: no haptic device found.
        }

        if (!SDL_InitHapticRumble(g_haptic))
        {
            return; // Silent no-op: device doesn't support simple rumble.
        }

        SDL_PlayHapticRumble(g_haptic, 1.0f, durationMs);
    }

    void VibrateController::Stop()
    {
        if (g_haptic != nullptr)
        {
            SDL_StopHapticEffects(g_haptic);
        }
    }
} // namespace Microsoft::Devices
