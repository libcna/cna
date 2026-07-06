// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Detail/SdlHapticVibrateBackend.hpp"

#include <algorithm>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>

namespace Microsoft::Devices::Detail
{
    namespace
    {
        // Returns true if hapticId is a currently-connected joystick/gamepad's
        // own haptic motor.
        //
        // On Linux (and likely similarly on other desktop backends), a
        // rumble-capable game controller is enumerated by SDL_GetHaptics() as
        // its own independent haptic device, entirely separate from the
        // joystick/gamepad subsystem that
        // Microsoft::Xna::Framework::Input::GamePad::SetVibration() uses
        // (SDL_RumbleGamepad). Correlates by ID via
        // SDL_OpenHapticFromJoystick() — SDL's own documented way to get a
        // specific joystick's haptic device (third_party/SDL/include/SDL3/
        // SDL_haptic.h's own usage example does exactly this) — rather than a
        // device-*name* string comparison, which could not distinguish two
        // physically distinct controllers reporting an identical product
        // name.
        bool IsConnectedGamepadHapticDevice(SDL_HapticID hapticId)
        {
            int joystickCount = 0;
            SDL_JoystickID* joysticks = SDL_GetJoysticks(&joystickCount);
            if (joysticks == nullptr)
            {
                return false;
            }

            bool matchesGamepad = false;

            for (int i = 0; i < joystickCount && !matchesGamepad; ++i)
            {
                SDL_Joystick* joystick = SDL_OpenJoystick(joysticks[i]);
                if (joystick == nullptr)
                {
                    continue;
                }

                SDL_Haptic* joystickHaptic = SDL_OpenHapticFromJoystick(joystick);
                if (joystickHaptic != nullptr)
                {
                    matchesGamepad = SDL_GetHapticID(joystickHaptic) == hapticId;

                    // Probe only — don't hold this joystick's haptic device
                    // open as a side effect of checking, same discipline as
                    // AcquireHapticDeviceForProbe() below.
                    SDL_CloseHaptic(joystickHaptic);
                }

                SDL_CloseJoystick(joystick);
            }

            SDL_free(joysticks);
            return matchesGamepad;
        }
    } // namespace

    SdlHapticVibrateBackend::~SdlHapticVibrateBackend()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // SDL_CloseHaptic() implicitly invalidates any effect still uploaded
        // on this device (SDL3 does not require destroying uploaded effects
        // before closing their owning device) — no separate
        // SDL_DestroyHapticEffect() call is needed here.
        if (haptic_ != nullptr)
        {
            SDL_CloseHaptic(haptic_);
            haptic_ = nullptr;
        }
        leftRightEffectId_ = -1;

        if (subsystemHeld_)
        {
            SDL_QuitSubSystem(SDL_INIT_HAPTIC);
            subsystemHeld_ = false;
        }
    }

    bool SdlHapticVibrateBackend::EnsureHapticSubsystemInitialized()
    {
        if (subsystemHeld_)
        {
            return true;
        }

        if (!SDL_InitSubSystem(SDL_INIT_HAPTIC))
        {
            return false;
        }

        subsystemHeld_ = true;
        return true;
    }

    // NOTE: on Android, SDL3's bundled Android haptic backend automatically
    // polls Context.VIBRATOR_SERVICE and registers the phone's own vibration
    // motor as a haptic device once SDL_INIT_HAPTIC is initialized.
    // SDL_InitHapticRumble()/SDL_PlayHapticRumble() below therefore already
    // reach Vibrator.vibrate(milliseconds) on Android with no custom
    // JNI/Java bridge code needed here (re-confirmed, Task VIB-003). No
    // #ifdef __ANDROID__ branch is required: the same code path serves both
    // Desktop and Android.
    SDL_Haptic* SdlHapticVibrateBackend::OpenFirstHapticDevice()
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

    // Returns a haptic device usable for a capability/name probe: haptic_ if
    // a device is already open, otherwise a temporarily opened one. Sets
    // openedTemporary so the caller knows whether it must close the
    // returned device again — probing must not hold a device open as a
    // side effect (that's what Start() is for). Caller must already hold
    // mutex_.
    SDL_Haptic* SdlHapticVibrateBackend::AcquireHapticDeviceForProbe(bool& openedTemporary)
    {
        if (haptic_ != nullptr)
        {
            openedTemporary = false;
            return haptic_;
        }

        openedTemporary = true;

        if (!EnsureHapticSubsystemInitialized())
        {
            return nullptr;
        }

        return OpenFirstHapticDevice();
    }

    // Destroys the currently-uploaded StartLeftRight() effect, if any.
    // Shared by Stop(), Start() (so the plain rumble path never runs
    // layered on top of a still-active StartLeftRight() effect), and
    // StartLeftRight()'s own re-entry path (replacing a previous dual-motor
    // effect with a new one). Caller must already hold mutex_.
    void SdlHapticVibrateBackend::DestroyLeftRightEffectIfAny()
    {
        if (haptic_ != nullptr && leftRightEffectId_ >= 0)
        {
            SDL_DestroyHapticEffect(haptic_, leftRightEffectId_);
        }

        leftRightEffectId_ = -1;
    }

    void SdlHapticVibrateBackend::Start(const System::TimeSpan& duration, float intensity)
    {
        const Uint32 durationMs = static_cast<Uint32>(duration.getTotalMillisecondsProperty());

        std::lock_guard<std::mutex> lock(mutex_);

        if (!EnsureHapticSubsystemInitialized())
        {
            return; // Silent no-op: haptic subsystem unavailable on this platform.
        }

        if (haptic_ == nullptr)
        {
            haptic_ = OpenFirstHapticDevice();
        }

        if (haptic_ == nullptr)
        {
            return; // Silent no-op: no haptic device found.
        }

        if (!SDL_InitHapticRumble(haptic_))
        {
            return; // Silent no-op: device doesn't support simple rumble.
        }

        // Mutually exclusive with StartLeftRight(): the two use independent
        // SDL haptic effect slots, so a still-active dual-motor effect must
        // be stopped explicitly or both would vibrate simultaneously.
        DestroyLeftRightEffectIfAny();

        SDL_PlayHapticRumble(haptic_, intensity, durationMs);
    }

    void SdlHapticVibrateBackend::Stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (haptic_ != nullptr)
        {
            SDL_StopHapticEffects(haptic_);
            DestroyLeftRightEffectIfAny();
        }
    }

    bool SdlHapticVibrateBackend::IsSupported()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        bool openedTemporary = false;
        SDL_Haptic* device = AcquireHapticDeviceForProbe(openedTemporary);

        // Deliberately NOT also calling SDL_InitHapticRumble() here to
        // confirm the device can actually initialize simple rumble, even
        // though that would make this property more precise (a device that
        // opens but can't init rumble currently still reports "supported").
        // Considered and rejected (Task VIB-005, re-examined): SDL_InitHapticRumble()
        // is not a read-only query -- per third_party/SDL/src/haptic/
        // SDL_haptic.c, it calls SDL_CreateHapticEffect(), which uploads a
        // real effect onto the physical device/driver. SDL_CloseHaptic()
        // implicitly invalidates that upload afterward, so it wouldn't leak,
        // but there is no way to confirm from this container -- no haptic
        // hardware is available here, ever (see NEXT.md) -- whether effect
        // *creation* itself causes any visible/physical actuation on real
        // force-feedback drivers, as opposed to merely allocating an effect
        // slot. Left unchanged rather than risk an unverifiable physical
        // side effect from a property getter a caller would reasonably
        // expect to be inert.
        const bool supported = device != nullptr;

        if (openedTemporary && device != nullptr)
        {
            SDL_CloseHaptic(device);
        }

        return supported;
    }

    std::string SdlHapticVibrateBackend::GetDeviceName()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        bool openedTemporary = false;
        SDL_Haptic* device = AcquireHapticDeviceForProbe(openedTemporary);

        std::string name;
        if (device != nullptr)
        {
            const char* deviceName = SDL_GetHapticName(device);
            if (deviceName != nullptr)
            {
                name = deviceName;
            }
        }

        if (openedTemporary && device != nullptr)
        {
            SDL_CloseHaptic(device);
        }

        return name;
    }

    void SdlHapticVibrateBackend::StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration)
    {
        const Uint32 durationMs = static_cast<Uint32>(duration.getTotalMillisecondsProperty());

        std::lock_guard<std::mutex> lock(mutex_);

        if (!EnsureHapticSubsystemInitialized())
        {
            return; // Silent no-op: haptic subsystem unavailable on this platform.
        }

        if (haptic_ == nullptr)
        {
            haptic_ = OpenFirstHapticDevice();
        }

        if (haptic_ == nullptr)
        {
            return; // Silent no-op: no haptic device found.
        }

        if ((SDL_GetHapticFeatures(haptic_) & SDL_HAPTIC_LEFTRIGHT) == 0)
        {
            return; // Silent no-op: device doesn't support dual-motor rumble.
        }

        // Mutually exclusive with Start(): stop any still-active simple
        // rumble before uploading the dual-motor effect, so both never run
        // on the same motor(s) at once. SDL_StopHapticRumble() is the only
        // way to stop specifically the rumble path — its effect slot
        // (haptic->rumble_id) is private to SDL, so a general
        // SDL_StopHapticEffect(id) isn't reachable here.
        SDL_StopHapticRumble(haptic_);

        // Replaces any previous StartLeftRight() effect (re-entry case).
        DestroyLeftRightEffectIfAny();

        SDL_HapticEffect effect{};
        effect.leftright.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = durationMs;
        effect.leftright.large_magnitude = static_cast<Uint16>(largeMotor * 65535.0f);
        effect.leftright.small_magnitude = static_cast<Uint16>(smallMotor * 65535.0f);

        leftRightEffectId_ = SDL_CreateHapticEffect(haptic_, &effect);
        if (leftRightEffectId_ < 0)
        {
            return; // Silent no-op: effect could not be uploaded.
        }

        SDL_RunHapticEffect(haptic_, leftRightEffectId_, 1);
    }
} // namespace Microsoft::Devices::Detail
