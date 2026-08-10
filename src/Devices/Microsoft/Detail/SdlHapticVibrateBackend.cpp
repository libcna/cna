// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Detail/SdlHapticVibrateBackend.hpp"

#include "Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp"
#include "Microsoft/Devices/Sensors/Detail/NativeDiagnostic.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>

namespace Microsoft::Devices::Detail
{
    namespace
    {
        // Task DEVPERF-005 (2026-07-18, external audit
        // `audit_devices_2026-07-17.md`): centralizes what every VIB2-003
        // debug-only SDL_Log() call below used to do individually, now also
        // routed through the shared Detail::NativeDiagnosticSink so these
        // failures are test-observable, not just visible in a debug-build
        // log stream. Replaces the raw SDL_Log() calls rather than
        // supplementing them (unlike SdlSensorSubsystem's SDLCORE-009
        // migration) -- no existing test reads this file's SDL_Log() text,
        // so there is nothing to preserve alongside.
        void RecordHapticDiagnostic(SDL_Haptic* haptic, const std::string& operation)
        {
            using Microsoft::Devices::Sensors::Detail::NativeDiagnosticRecord;
            using Microsoft::Devices::Sensors::Detail::NativeDiagnosticSeverity;
            using Microsoft::Devices::Sensors::Detail::NativeDiagnosticSink;

            NativeDiagnosticRecord record;
            record.Backend = "SDL";
            record.Operation = operation;
            record.NativeMessage = SDL_GetError();
            record.DeviceId = std::to_string(static_cast<long long>(SDL_GetHapticID(haptic)));
            record.Timestamp = System::DateTimeOffset::getUtcNowProperty();
            record.Severity = NativeDiagnosticSeverity::Warning;
            NativeDiagnosticSink::Record(record);
        }

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

        /**
         * @brief Clamps a motor-magnitude/intensity value to `[0, 1]`, canonicalizing NaN to `0`.
         *
         * Task VIB2-002: `VibrateController` already canonicalizes NaN/
         * out-of-range input before it reaches this backend (see
         * `VibrateController.cpp`'s `CanonicalizeVibrationMagnitude()`), but
         * this backend's own `Start()`/`StartLeftRight()` are the *only*
         * places any caller's value actually reaches a real SDL call or an
         * integer cast -- a checked conversion here does not depend on that
         * upstream discipline holding for every possible caller (a future
         * direct `IVibrateBackend` caller, a test, ...). `std::isnan` first:
         * `std::clamp` alone leaves NaN unchanged (every comparison against
         * NaN is `false`), unlike true +/-infinity, which it already
         * saturates correctly against a finite bound.
         */
        [[nodiscard]] float SanitizeSdlHapticInput(float value)
        {
            if (std::isnan(value))
            {
                return 0.0f;
            }
            return std::clamp(value, 0.0f, 1.0f);
        }

        /**
         * @brief Converts a `[0, 1]`-range motor magnitude to `SDL_HapticLeftRight`'s `Uint16` scale.
         *
         * `static_cast<Uint16>(magnitude * 65535.0f)` on an unsanitized
         * `magnitude` is undefined behavior if it is NaN (converting a
         * floating value not representable in the destination integer type
         * is undefined, not merely a truncating/wrapping cast the way an
         * already-in-range float-to-integer conversion is) — `SanitizeSdlHapticInput()`
         * guarantees a finite, in-range value reaches this cast.
         */
        [[nodiscard]] Uint16 ToSdlHapticMagnitude(float magnitude)
        {
            return static_cast<Uint16>(SanitizeSdlHapticInput(magnitude) * 65535.0f);
        }

        /**
         * @brief Returns true if `haptic`'s device is still enumerated by `SDL_GetHaptics()`.
         *
         * Task VIB2-004: unlike joystick/gamepad hotplug
         * (`SDL_EVENT_JOYSTICK_REMOVED`), SDL3 has no haptic-specific
         * removal event, and `SDL_GetHapticID()` only returns the instance
         * ID captured when the device was opened (per
         * `third_party/SDL/src/haptic/SDL_haptic.c`, its `CHECK_HAPTIC_MAGIC`
         * guard only rejects an already-closed/never-valid handle, not one
         * whose physical device has since disappeared) — re-querying the
         * live device list and comparing IDs is the only way to detect a
         * mid-session disconnect.
         */
        [[nodiscard]] bool IsHapticDeviceStillConnected(SDL_Haptic* haptic)
        {
            const SDL_HapticID id = SDL_GetHapticID(haptic);
            if (id == 0)
            {
                return false;
            }

            int hapticCount = 0;
            SDL_HapticID* haptics = SDL_GetHaptics(&hapticCount);
            if (haptics == nullptr)
            {
                return false;
            }

            bool stillConnected = false;
            for (int i = 0; i < hapticCount && !stillConnected; ++i)
            {
                stillConnected = haptics[i] == id;
            }

            SDL_free(haptics);
            return stillConnected;
        }
    } // namespace

    SdlHapticVibrateBackend::~SdlHapticVibrateBackend()
    {
        std::lock_guard<std::mutex> lock(GetGlobalSdlSubsystemMutex());

        // Task SDLCORE-011 (2026-07-18, external audit
        // `audit_devices_2026-07-17.md`): if the application has already
        // called Detail::DevicesShutdownCoordinator::Shutdown() (which it
        // must do before its own SDL_Quit() call whenever a
        // VibrateController might still be alive), SDL_Quit() has already
        // reclaimed every SDL-owned resource this destructor would
        // otherwise try to release itself. See DevicesShutdownCoordinator's
        // own doc comment for the full, honestly-scoped rationale: skipping
        // SDL_CloseHaptic() here specifically closes a genuine
        // heap-use-after-free reasoned directly from SDL's own source
        // (confirmed, not assumed) but not empirically reproducible under
        // ASan in this container (needs a real, opened haptic_, never
        // available here); skipping SDL_QuitSubSystem() below was checked
        // and found unnecessary for safety against SDL's *current*
        // refcount-based idempotent implementation, kept anyway as defense
        // that does not depend on that internal detail staying as it is.
        // Member resets below still run unconditionally regardless (harmless
        // bookkeeping, not native calls).
        const bool devicesShutDown = DevicesShutdownCoordinator::IsShutdown();

        // SDL_CloseHaptic() implicitly invalidates any effect still uploaded
        // on this device (SDL3 does not require destroying uploaded effects
        // before closing their owning device) — no separate
        // SDL_DestroyHapticEffect() call is needed here.
        if (haptic_ != nullptr)
        {
            if (!devicesShutDown)
            {
                SDL_CloseHaptic(haptic_);
            }
            haptic_ = nullptr;
        }
        leftRightEffectId_ = -1;

        if (subsystemHeld_)
        {
            if (!devicesShutDown)
            {
                SDL_QuitSubSystem(SDL_INIT_HAPTIC);
            }
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
    // GetGlobalSdlSubsystemMutex().
    SDL_Haptic* SdlHapticVibrateBackend::AcquireHapticDeviceForProbe(bool& openedTemporary)
    {
        // Task VIB2-004: without this, IsSupported()/GetDeviceName() would
        // keep reporting a disconnected device's cached capabilities/name
        // forever (SDL_HapticRumbleSupported() reads a bitmask captured at
        // open time, not a live re-query) -- the exact "retain stale
        // support" failure this task's acceptance criteria call out.
        ReleaseHapticDeviceIfStale();

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
    // effect with a new one). Caller must already hold
    // GetGlobalSdlSubsystemMutex().
    void SdlHapticVibrateBackend::DestroyLeftRightEffectIfAny()
    {
        if (haptic_ != nullptr && leftRightEffectId_ >= 0)
        {
            SDL_DestroyHapticEffect(haptic_, leftRightEffectId_);
        }

        leftRightEffectId_ = -1;
    }

    // Task VIB2-004: closes and discards `haptic_` if its device has
    // disappeared since it was opened, so every caller below always either
    // sees `nullptr` (not-yet-reopened) or a genuinely live device — never a
    // stale one that would silently no-op every SDL call against it (VIB2-003
    // already makes those failures non-crashing, but a stale handle must not
    // be mistaken for a present device by e.g. IsSupported()) or report an
    // uploaded effect (`leftRightEffectId_`) that could not possibly still be
    // running on hardware that's gone. Deliberately resets `leftRightEffectId_`
    // directly rather than routing through DestroyLeftRightEffectIfAny(),
    // which would call SDL_DestroyHapticEffect() against the very handle
    // being closed. Does not attempt to reopen a replacement device -- every
    // caller already has its own "if (haptic_ == nullptr) ..." reopen policy
    // (persist for Start()/StartLeftRight(), temporary-only for
    // AcquireHapticDeviceForProbe()). Caller must already hold
    // GetGlobalSdlSubsystemMutex().
    void SdlHapticVibrateBackend::ReleaseHapticDeviceIfStale()
    {
        if (haptic_ != nullptr && !IsHapticDeviceStillConnected(haptic_))
        {
            // Task DEVPERF-005 (2026-07-18, external audit
            // `audit_devices_2026-07-17.md`): VIB2-004's own original entry
            // had no diagnostic at all for this path -- a stale device being
            // released was entirely silent, unlike VIB2-003's now-migrated
            // failed-SDL-call diagnostics above. Info severity, not
            // Warning/Error: this is expected, correctly-handled behavior
            // (the whole point of ReleaseHapticDeviceIfStale() existing), not
            // an ignored failure -- included so a caller/telemetry consumer
            // can still observe "a haptic device was lost" as an event, not
            // just infer it indirectly from IsSupported() changing.
            Microsoft::Devices::Sensors::Detail::NativeDiagnosticRecord record;
            record.Backend = "SDL";
            record.Operation = "SdlHapticVibrateBackend device released (disconnected)";
            record.DeviceId = std::to_string(static_cast<long long>(SDL_GetHapticID(haptic_)));
            record.Timestamp = System::DateTimeOffset::getUtcNowProperty();
            record.Severity = Microsoft::Devices::Sensors::Detail::NativeDiagnosticSeverity::Info;
            Microsoft::Devices::Sensors::Detail::NativeDiagnosticSink::Record(record);

            SDL_CloseHaptic(haptic_);
            haptic_ = nullptr;
            leftRightEffectId_ = -1;
        }
    }

    void SdlHapticVibrateBackend::Start(const System::TimeSpan& duration, float intensity)
    {
        const Uint32 durationMs = static_cast<Uint32>(duration.getTotalMillisecondsProperty());

        std::lock_guard<std::mutex> lock(GetGlobalSdlSubsystemMutex());

        if (!EnsureHapticSubsystemInitialized())
        {
            return; // Silent no-op: haptic subsystem unavailable on this platform.
        }

        // Task VIB2-004: a previously-opened device may have disconnected
        // since the last call -- discard it before deciding whether a
        // (re)open is needed.
        ReleaseHapticDeviceIfStale();

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
            // Task DEVPERF-005 (2026-07-18, external audit
            // `audit_devices_2026-07-17.md`): previously a completely silent
            // no-op, the one call in this method the initial VIB2-003/005
            // migration pass missed (found by a dedicated sweep for
            // remaining unmigrated native call sites). Still a no-op --
            // Start(TimeSpan, float)'s contract is unchanged -- just now
            // observable.
            RecordHapticDiagnostic(haptic_, "SDL_InitHapticRumble");
            return;
        }

        // Mutually exclusive with StartLeftRight(): the two use independent
        // SDL haptic effect slots, so a still-active dual-motor effect must
        // be stopped explicitly or both would vibrate simultaneously.
        DestroyLeftRightEffectIfAny();

        // Task VIB2-002: see SanitizeSdlHapticInput()'s own doc comment --
        // an unsanitized NaN `intensity` must not reach this real SDL call.
        //
        // Task VIB2-003 (2026-07-17, external audit
        // `audit_devices_2026-07-17.md`): the return value was previously
        // ignored entirely. Start(TimeSpan, float)'s own strict XNA contract
        // is `void` (no return value, does not throw for a runtime playback
        // failure) -- matched here by remaining a silent no-op on failure,
        // same as every other already-established early return in this
        // method, just now with a debug-only diagnostic so a caller has some
        // way to notice "vibration silently did nothing" during development.
        if (!SDL_PlayHapticRumble(haptic_, SanitizeSdlHapticInput(intensity), durationMs))
        {
            RecordHapticDiagnostic(haptic_, "SDL_PlayHapticRumble");
        }
    }

    void SdlHapticVibrateBackend::Stop()
    {
        std::lock_guard<std::mutex> lock(GetGlobalSdlSubsystemMutex());

        // Task VIB2-004: if the device is already gone, release the stale
        // handle and return -- there is nothing left to stop, and issuing
        // SDL calls against a disconnected device would just log doomed
        // failures (VIB2-003) for no benefit.
        ReleaseHapticDeviceIfStale();

        if (haptic_ != nullptr)
        {
            // Task VIB2-003: checked, not just called -- nothing actionable
            // differs on failure (DestroyLeftRightEffectIfAny() below still
            // needs to run regardless, and Stop()'s own contract is `void`),
            // so this is a debug-only diagnostic, matching Start()'s
            // identical treatment of SDL_PlayHapticRumble() above.
            if (!SDL_StopHapticEffects(haptic_))
            {
                RecordHapticDiagnostic(haptic_, "SDL_StopHapticEffects");
            }
            DestroyLeftRightEffectIfAny();
        }
    }

    bool SdlHapticVibrateBackend::IsSupported()
    {
        std::lock_guard<std::mutex> lock(GetGlobalSdlSubsystemMutex());

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
        //
        // Task VIB2-001 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
        // re-examined this exact boundary and found `SDL_HapticRumbleSupported()`
        // -- a *different* function from `SDL_InitHapticRumble()`, overlooked by
        // the prior VIB-005 investigation above. Confirmed genuinely read-only
        // by reading its actual implementation directly (`third_party/SDL/src/
        // haptic/SDL_haptic.c`): `return (haptic->supported & (SDL_HAPTIC_SINE |
        // SDL_HAPTIC_LEFTRIGHT)) != 0;` -- a pure bitmask check against
        // capabilities SDL already queried when opening the device, no
        // `SDL_CreateHapticEffect()` call, no upload, no device I/O at all. A
        // device that opens but exposes only e.g. condition/constant-force
        // effects (no SINE/LEFTRIGHT) previously reported "supported" here even
        // though `Start(TimeSpan)`'s own `SDL_InitHapticRumble()` call would
        // then silently no-op (see `Start()`'s own "device doesn't support
        // simple rumble" early return) -- this closes that gap without
        // reopening VIB-005's original, still-valid concern about
        // `SDL_InitHapticRumble()` itself.
        const bool supported = device != nullptr && SDL_HapticRumbleSupported(device);

        if (openedTemporary && device != nullptr)
        {
            SDL_CloseHaptic(device);
        }

        return supported;
    }

    std::string SdlHapticVibrateBackend::GetDeviceName()
    {
        std::lock_guard<std::mutex> lock(GetGlobalSdlSubsystemMutex());

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

        std::lock_guard<std::mutex> lock(GetGlobalSdlSubsystemMutex());

        if (!EnsureHapticSubsystemInitialized())
        {
            return; // Silent no-op: haptic subsystem unavailable on this platform.
        }

        // Task VIB2-004: see Start()'s identical call for the reasoning.
        ReleaseHapticDeviceIfStale();

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
        //
        // Task VIB2-003: checked for a debug-only diagnostic only -- there is
        // no corrective action available (the rumble path is private to SDL,
        // nothing here to destroy or roll back), and StartLeftRight()'s own
        // contract is `void`.
        if (!SDL_StopHapticRumble(haptic_))
        {
            RecordHapticDiagnostic(haptic_, "SDL_StopHapticRumble");
        }

        // Replaces any previous StartLeftRight() effect (re-entry case).
        DestroyLeftRightEffectIfAny();

        SDL_HapticEffect effect{};
        effect.leftright.type = SDL_HAPTIC_LEFTRIGHT;
        effect.leftright.length = durationMs;
        effect.leftright.large_magnitude = ToSdlHapticMagnitude(largeMotor);
        effect.leftright.small_magnitude = ToSdlHapticMagnitude(smallMotor);

        leftRightEffectId_ = SDL_CreateHapticEffect(haptic_, &effect);
        if (leftRightEffectId_ < 0)
        {
            // Task DEVPERF-005 (2026-07-18, external audit
            // `audit_devices_2026-07-17.md`): previously a completely silent
            // no-op, the second call this file's initial VIB2-003/005
            // migration pass missed (found by the same dedicated sweep as
            // SDL_InitHapticRumble above). Still a no-op --
            // StartLeftRight(...)'s contract is unchanged -- just now
            // observable. NativeCode carries the actual negative id SDL
            // returned, unlike RecordHapticDiagnostic()'s other call sites
            // (whose underlying SDL calls return bool, with no numeric code
            // of their own).
            Microsoft::Devices::Sensors::Detail::NativeDiagnosticRecord record;
            record.Backend = "SDL";
            record.Operation = "SDL_CreateHapticEffect";
            record.NativeCode = leftRightEffectId_;
            record.NativeMessage = SDL_GetError();
            record.DeviceId = std::to_string(static_cast<long long>(SDL_GetHapticID(haptic_)));
            record.Timestamp = System::DateTimeOffset::getUtcNowProperty();
            record.Severity = Microsoft::Devices::Sensors::Detail::NativeDiagnosticSeverity::Warning;
            Microsoft::Devices::Sensors::Detail::NativeDiagnosticSink::Record(record);
            return; // Silent no-op: effect could not be uploaded.
        }

        // Task VIB2-003 (external audit, required work: "Destroy a newly
        // uploaded effect if Run fails when appropriate"): without this, a
        // failed Run() would leave `leftRightEffectId_` pointing at an
        // uploaded-but-never-playing effect. That is not a permanent SDL-side
        // leak (DestroyLeftRightEffectIfAny() would still reclaim it on the
        // next Start()/StartLeftRight()/Stop()/destructor call), but it is
        // observably wrong: Vibrate() would report having started dual-motor
        // playback while nothing is actually running, and the stale id would
        // linger for however long the controller stays otherwise idle.
        if (!SDL_RunHapticEffect(haptic_, leftRightEffectId_, 1))
        {
            RecordHapticDiagnostic(haptic_, "SDL_RunHapticEffect");
            DestroyLeftRightEffectIfAny();
        }
    }
} // namespace Microsoft::Devices::Detail
