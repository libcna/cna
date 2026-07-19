# Audit: src/Microsoft/Devices/Detail/SdlHapticVibrateBackend.cpp

## Metadata
- Source file: `src/Microsoft/Devices/Detail/SdlHapticVibrateBackend.cpp` (626 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ implementation
- XNA/FNA relevance: CNA-internal plumbing; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements `SdlHapticVibrateBackend`'s single-motor rumble, dual-motor left/right effect, stale-device detection/recovery, gamepad-haptic exclusion, and process-exit-safe teardown.

## Executive Verdict
Correct, and thoroughly hardened. `IsConnectedGamepadHapticDevice()` correctly correlates by SDL haptic/joystick ID (via `SDL_OpenHapticFromJoystick()`, SDL's own documented technique) rather than a device-name string comparison, which the comment correctly notes could not distinguish two physically distinct controllers reporting an identical product name — a real, non-obvious correctness point. `ToSdlHapticMagnitude()`'s `SanitizeSdlHapticInput()` pre-check is correctly justified as necessary even though `VibrateController` already canonicalizes NaN/out-of-range input upstream: this file's own comment explains this is the only place any caller's value actually reaches a real SDL call or integer cast, so a defense-in-depth check here doesn't depend on the upstream discipline holding for every possible future `IVibrateBackend` caller.

## Checklist Results
- `~SdlHapticVibrateBackend()` (lines 175-221) correctly consults `DevicesShutdownCoordinator::IsShutdown()` before making any real `SDL_CloseHaptic()`/`SDL_QuitSubSystem()` call, skipping them if the application has already called `SDL_Quit()` — closing a real, source-reasoned (not assumed) heap-use-after-free risk in `SDL_CloseHaptic()` against an already-`SDL_Quit()`-closed device.
- `ReleaseHapticDeviceIfStale()` (lines 343-369) is correctly called at the top of every public entry point (`Start`, `Stop`, `AcquireHapticDeviceForProbe`) before touching `haptic_`, ensuring a disconnected device is discarded before any caller could mistake a stale handle for a live one.
- `IsSupported()`'s own comment (lines 464-496) documents a genuinely careful investigation distinguishing `SDL_HapticRumbleSupported()` (a pure bitmask check, confirmed by reading SDL's actual implementation, no device I/O) from `SDL_InitHapticRumble()` (which *does* upload a real effect) — correctly choosing the read-only one for a property getter a caller would reasonably expect to be inert, and explicitly re-examining and correcting a prior investigation (Task VIB-005) that had overlooked this distinction.
- `Start()`/`StartLeftRight()` are correctly documented and implemented as mutually exclusive on the same physical motor(s): each destroys the other's active effect before starting its own (`DestroyLeftRightEffectIfAny()` in `Start()`; `SDL_StopHapticRumble()` in `StartLeftRight()`), preventing both from running simultaneously on a shared actuator.
- Every SDL call whose return value indicates a real, actionable failure is checked (`SDL_InitHapticRumble`, `SDL_PlayHapticRumble`, `SDL_StopHapticEffects`, `SDL_StopHapticRumble`, `SDL_CreateHapticEffect`, `SDL_RunHapticEffect`) — each routed through `RecordHapticDiagnostic()` for test-observable, non-silent failure reporting (Task DEVPERF-005), correctly still remaining a no-op per this API's `void`-contract, not converted into a thrown exception it was never meant to raise.

## Detailed Findings
None.

## Cross-File Observations
`GetGlobalSdlSubsystemMutex()` (from `SdlSubsystemMutex.hpp`, audited separately) is correctly held for the duration of every method in this class, including the destructor — confirmed no method here releases it before a real SDL call, avoiding the confirmed real `FileDialog`/`MessageBox` mutex-scoping bug pattern documented elsewhere in this project.

## Missing or Weak Tests
Not independently located in this pass; no haptic hardware is available in this environment (per this shard's own repeated, honest disclosure), an accepted limitation for the real SDL device-interaction paths specifically — the pure-logic paths (NaN sanitization, mutual-exclusion, stale-device detection against a fake/mocked device) are more amenable to host testing and likely covered given the extensive `*ForTesting()` surface elsewhere in this shard.

## Positive Findings
The `SDL_HapticRumbleSupported()` vs. `SDL_InitHapticRumble()` distinction (Task VIB2-001, explicitly re-examining and correcting an earlier investigation's blind spot) is a genuinely valuable example of revisiting a previously-closed decision when new evidence (reading a *different* function's actual source) surfaces, rather than treating "already investigated" as permanently settled.

## Final Assessment
No findings.
