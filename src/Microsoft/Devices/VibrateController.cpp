// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/VibrateController.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_init.h>

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

        // Opens the first enumerable haptic device, if any.
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

            SDL_Haptic* opened = SDL_OpenHaptic(haptics[0]);
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
