// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/VibrateController.hpp"

#include <algorithm>
#include <mutex>

#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>

#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Devices
{
    namespace
    {
        // File-local state: the single haptic device opened by Start(), reused
        // across calls so Stop() can act on it. Closed by
        // VibrateController::~VibrateController() (Task P5-11) when the
        // process-lifetime singleton itself is destroyed at normal program
        // termination — confirmed safe since this codebase never calls
        // SDL_Quit() anywhere (grepped the whole tree), so SDL's subsystems
        // stay valid until well after this runs. Previously left
        // permanently unclosed based on an unverified assumption (Task
        // P4-9) that SDL_Quit() might run first; that assumption was wrong.
        //
        // Task P6-6 re-examined this with a sharper question: could a HOST
        // application (using CNA as a library) call the umbrella
        // SDL_Quit() directly in its own code, independent of anything
        // CNA does? Confirmed this destructor's pattern — a per-class
        // SDL_InitSubSystem()/SDL_QuitSubSystem() pair, never the umbrella
        // SDL_Init()/SDL_Quit() — is an established, project-wide
        // convention: Microsoft::Xna::Framework::Graphics::GraphicsDevice
        // does the identical thing for SDL_INIT_VIDEO (its own constructor/
        // Dispose()). The residual risk of a host app calling SDL_Quit()
        // directly is therefore a characteristic shared identically by
        // GraphicsDevice, not something unique to or fixable by
        // VibrateController alone within a Microsoft::Devices-only scope.
        SDL_Haptic* g_haptic = nullptr;

        // File-local state: the currently-uploaded SDL_HAPTIC_LEFTRIGHT effect
        // started by StartLeftRight(), if any. SDL_StopHapticEffects(g_haptic)
        // (used by Stop()) already stops its playback, but doesn't free the
        // uploaded effect slot — Stop() also calls SDL_DestroyHapticEffect()
        // on this ID for that. -1 means "none uploaded".
        SDL_HapticEffectID g_leftRightEffectId = -1;

        // Guards g_haptic and g_leftRightEffectId (Task P4-9): unlike the
        // sensor classes, VibrateController has no SDL event-watch callback,
        // so the only risk is ordinary unsynchronized access from multiple
        // application threads (e.g. one thread calling Start() while another
        // calls Stop()). Each public VibrateController:: method locks this
        // for its entire body; the anonymous-namespace helpers below that
        // touch either variable (AcquireHapticDeviceForProbe(),
        // DestroyLeftRightEffectIfAny()) do not lock it themselves — they are
        // only ever called from within an already-locked method, and this
        // mutex is not recursive.
        std::mutex g_mutex;

        // True once this process has made a successful
        // SDL_InitSubSystem(SDL_INIT_HAPTIC) call. Paired with exactly one
        // SDL_QuitSubSystem() call from ~VibrateController() (Task P5-11).
        // Unlike the sensor classes (Accelerometer/Gyroscope, Task P4-8),
        // this doesn't need SDL's own ref-counting to aggregate across
        // multiple independent holders — VibrateController is a true
        // process-wide singleton with exactly one call path in this
        // codebase ever touching SDL_INIT_HAPTIC, so a single own-state
        // flag is sufficient and simpler than always calling through to
        // SDL_InitSubSystem() (as Accelerometer/Gyroscope now do) and
        // relying on SDL's ref-count.
        bool g_subsystemHeld = false;

        void ValidateVibrationDuration(const System::TimeSpan& duration)
        {
            static const System::TimeSpan MaxVibrationDuration = System::TimeSpan::FromSeconds(5);

            if (duration < System::TimeSpan::Zero || duration > MaxVibrationDuration)
            {
                throw System::ArgumentOutOfRangeException(
                    "duration",
                    duration.ToString(),
                    "'duration' must be between TimeSpan.Zero and TimeSpan.FromSeconds(5).");
            }
        }

        // Task P5-11: replaced the previous SDL_WasInit() guard with
        // g_subsystemHeld's own bookkeeping — same "don't bypass real
        // ref-counting with a guard that can't tell your own calls apart
        // from anyone else's" principle Task P4-8 established for the
        // sensor classes, just tracked via a single bool here since
        // there's only one caller of SDL_InitSubSystem(SDL_INIT_HAPTIC)
        // in this codebase (see g_subsystemHeld's own comment). Caller
        // must already hold g_mutex.
        bool EnsureHapticSubsystemInitialized()
        {
            if (g_subsystemHeld)
            {
                return true;
            }

            if (!SDL_InitSubSystem(SDL_INIT_HAPTIC))
            {
                return false;
            }

            g_subsystemHeld = true;
            return true;
        }

        // Returns true if hapticId is a currently-connected joystick/gamepad's
        // own haptic motor.
        //
        // On Linux (and likely similarly on other desktop backends), a
        // rumble-capable game controller is enumerated by SDL_GetHaptics()
        // as its own independent haptic device, entirely separate from the
        // joystick/gamepad subsystem that
        // Microsoft::Xna::Framework::Input::GamePad::SetVibration() uses
        // (SDL_RumbleGamepad). Task P4-10: correlates by ID via
        // SDL_OpenHapticFromJoystick() — SDL's own documented way to get a
        // specific joystick's haptic device (third_party/SDL/include/SDL3/
        // SDL_haptic.h's own usage example does exactly this) — rather than
        // the previous device-*name* string comparison, which could not
        // distinguish two physically distinct controllers reporting an
        // identical product name.
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

        // Returns a haptic device usable for a capability/name probe:
        // g_haptic if a device is already open, otherwise a temporarily
        // opened one. Sets openedTemporary so the caller knows whether it
        // must close the returned device again — probing must not hold a
        // device open as a side effect (that's what Start() is for).
        // Caller must already hold g_mutex.
        SDL_Haptic* AcquireHapticDeviceForProbe(bool& openedTemporary)
        {
            if (g_haptic != nullptr)
            {
                openedTemporary = false;
                return g_haptic;
            }

            openedTemporary = true;

            if (!EnsureHapticSubsystemInitialized())
            {
                return nullptr;
            }

            return OpenFirstHapticDevice();
        }

        // Destroys the currently-uploaded StartLeftRight() effect, if any.
        // Shared by Stop(), Start()/Start(duration,intensity) (so the plain
        // rumble path never runs layered on top of a still-active
        // StartLeftRight() effect), and StartLeftRight()'s own re-entry path
        // (replacing a previous dual-motor effect with a new one). Caller
        // must already hold g_mutex.
        void DestroyLeftRightEffectIfAny()
        {
            if (g_haptic != nullptr && g_leftRightEffectId >= 0)
            {
                SDL_DestroyHapticEffect(g_haptic, g_leftRightEffectId);
            }

            g_leftRightEffectId = -1;
        }
    } // namespace

    VibrateController* VibrateController::getDefaultProperty()
    {
        static VibrateController instance;
        return &instance;
    }

    VibrateController::~VibrateController()
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (g_haptic != nullptr)
        {
            SDL_CloseHaptic(g_haptic);
            g_haptic = nullptr;
        }

        if (g_subsystemHeld)
        {
            SDL_QuitSubSystem(SDL_INIT_HAPTIC);
            g_subsystemHeld = false;
        }
    }

    void VibrateController::Start(const System::TimeSpan& duration)
    {
        Start(duration, 1.0f);
    }

    void VibrateController::Start(const System::TimeSpan& duration, float intensity)
    {
        ValidateVibrationDuration(duration);
        const float clampedIntensity = std::clamp(intensity, 0.0f, 1.0f);
        const Uint32 durationMs = static_cast<Uint32>(duration.getTotalMillisecondsProperty());

        std::lock_guard<std::mutex> lock(g_mutex);

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

        // Mutually exclusive with StartLeftRight(): the two use independent
        // SDL haptic effect slots, so a still-active dual-motor effect must
        // be stopped explicitly or both would vibrate simultaneously.
        DestroyLeftRightEffectIfAny();

        SDL_PlayHapticRumble(g_haptic, clampedIntensity, durationMs);
    }

    void VibrateController::Stop()
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (g_haptic != nullptr)
        {
            SDL_StopHapticEffects(g_haptic);
            DestroyLeftRightEffectIfAny();
        }
    }

    bool VibrateController::getIsSupportedProperty()
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        bool openedTemporary = false;
        SDL_Haptic* device = AcquireHapticDeviceForProbe(openedTemporary);

        const bool supported = device != nullptr;

        if (openedTemporary && device != nullptr)
        {
            SDL_CloseHaptic(device);
        }

        return supported;
    }

    std::string VibrateController::getDeviceNameProperty()
    {
        std::lock_guard<std::mutex> lock(g_mutex);

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

    void VibrateController::StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration)
    {
        ValidateVibrationDuration(duration);
        const float clampedLarge = std::clamp(largeMotor, 0.0f, 1.0f);
        const float clampedSmall = std::clamp(smallMotor, 0.0f, 1.0f);
        const Uint32 durationMs = static_cast<Uint32>(duration.getTotalMillisecondsProperty());

        std::lock_guard<std::mutex> lock(g_mutex);

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

        if ((SDL_GetHapticFeatures(g_haptic) & SDL_HAPTIC_LEFTRIGHT) == 0)
        {
            return; // Silent no-op: device doesn't support dual-motor rumble.
        }

        // Mutually exclusive with Start()/Start(duration,intensity): stop
        // any still-active simple rumble before uploading the dual-motor
        // effect, so both never run on the same motor(s) at once.
        // SDL_StopHapticRumble() is the only way to stop specifically the
        // rumble path — its effect slot (haptic->rumble_id) is private to
        // SDL, so a general SDL_StopHapticEffect(id) isn't reachable here.
        SDL_StopHapticRumble(g_haptic);

        // Replaces any previous StartLeftRight() effect (re-entry case).
        DestroyLeftRightEffectIfAny();

        SDL_HapticEffect effect{};
        effect.leftright.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = durationMs;
        effect.leftright.large_magnitude = static_cast<Uint16>(clampedLarge * 65535.0f);
        effect.leftright.small_magnitude = static_cast<Uint16>(clampedSmall * 65535.0f);

        g_leftRightEffectId = SDL_CreateHapticEffect(g_haptic, &effect);
        if (g_leftRightEffectId < 0)
        {
            return; // Silent no-op: effect could not be uploaded.
        }

        SDL_RunHapticEffect(g_haptic, g_leftRightEffectId, 1);
    }
} // namespace Microsoft::Devices
