# plan_devices_phase2.md — Devices Follow-up: Audit, Bug Fixes, Cross-Platform Verification

## Context

`plan_devices.md` (31 tasks: full `Microsoft::Devices::Sensors` namespace +
`VibrateController`) is complete — see its Task Summary table for the
per-task status. During that work, a few gaps and one real bug were found
but were out of that plan's scope to fix.

`VibrateController` works correctly for its current, minimal, strictly-XNA
Start(TimeSpan)/Stop() surface — but it hasn't had a dedicated review pass,
and SDL3 exposes a much richer haptic API than the plain rumble helper it
currently uses (see Phase 6 below, added after re-investigating SDL3's
`SDL_haptic.h` in depth). Phase 6 covers reviewing/fixing that class and
proposing `NOXNA` extensions beyond the base WP7 API — CNA targets more
capable hardware than 2010-era WP7 phones (which only ever had a single-
intensity on/off buzzer motor, hence the minimal real XNA API), so there is
real headroom to expose more of SDL3's haptic capabilities as CNA-specific
additions without touching the XNA-compliant surface.

This plan has no build-verified deadline; work through phases in order,
verifying build + tests after each task, same as `plan_devices.md`.

---

## Phase 1: Housekeeping (already partially done)

### Task P2-1 — Status tracking (done as part of opening this plan)
`plan_devices.md`'s Task Summary table now has a Status column (all 31 ✅).
`AUDIT.md` now has a `Microsoft::Devices::Sensors` / `Microsoft::Devices`
section (previously missing entirely, since FNA has no equivalent to diff
against). No further action needed unless new gaps are found in Task P2-2.

---

## Phase 2: API-completeness audit

### Task P2-2 — Independent verification of Sensors API completeness

**Goal:** The `AUDIT.md` entry for this namespace was filled in from general
WP7 Mango (OS 7.1) SDK knowledge, not by diffing against a local reference
tree (none exists — FNA doesn't implement `Microsoft.Devices`). Do an
independent pass to confirm nothing in `Microsoft.Devices.Sensors` or the
vibration-relevant part of `Microsoft.Devices` was missed:
- `Accelerometer`, `AccelerometerFailedException`, `AccelerometerReading`,
  `AccelerometerReadingEventArgs`, `AttitudeReading`, `CalibrationEventArgs`,
  `Compass`, `CompassReading`, `Gyroscope`, `GyroscopeReading`,
  `ISensorReading`, `Motion`, `MotionReading`, `SensorBase<T>`,
  `SensorFailedException`, `SensorReadingEventArgs<T>`, `SensorState`,
  `VibrateController`.
- Check specifically for: any WP7 7.0-legacy sibling classes to
  `AccelerometerReadingEventArgs` that might exist for other sensors: does a
  legacy `CompassReadingEventArgs` or similar exist in the real API, or was
  the WP7 7.0 legacy event-args pattern only ever defined for Accelerometer?
  (If genuinely WP7-7.0-Accelerometer-only, no new file needed — just
  confirm and note it in `AUDIT.md`.)
- Explicitly do **not** expand scope to `Microsoft.Devices.Environment`,
  `PhotoCamera`/`CameraButtons`/`CameraCaptureTask`/`PhotoChooserTask`, radio,
  or phone-call APIs — these are intentionally excluded (not sensor/vibration
  APIs; see `plan_devices.md`'s "Do not do yet" list, preserved in `NEXT.md`).
- If a real gap is found: add it as a new numbered task here before
  implementing, following the same per-file checklist as `plan_devices.md`.
- If no gap is found: update the `AUDIT.md` note to record that this was
  independently double-checked (with date), so the caveat about "not diffed
  against a local reference tree" can be relaxed.

---

## Phase 3: Known bug fixes

### Task P2-3 — Fix Accelerometer.hpp Dispose() name-hiding + add AccelerometerTests.cpp

**Bug:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp` declares
`void Dispose(bool disposing) override;` without a `using
SensorBase<AccelerometerReading>::Dispose;` declaration. This hides the
inherited public no-arg `Dispose()` (the actual `System::IDisposable`
contract method) via C++ name-hiding — `accel.Dispose()` fails to compile for
any caller. The identical bug was found and fixed in `Compass`, `Gyroscope`,
and `Motion` during `plan_devices.md` (search those headers for the `using
SensorBase<...>::Dispose;` pattern to copy).

**Why it's gone unnoticed:** `Accelerometer` is the oldest class in this
namespace and has never had a test file — `AccelerometerTests.cpp` doesn't
exist. This task fixes both at once.

**Steps:**
1. Add the one-line `using SensorBase<AccelerometerReading>::Dispose;` fix to
   `Accelerometer.hpp` (same spot as `Compass.hpp`/`Gyroscope.hpp`/
   `Motion.hpp`).
2. Write `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`, modeled on
   `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp` (branches on the live
   `getIsSupportedProperty()` result so it passes both headless — no
   accelerometer hardware in this dev container — and on real hardware).
   Required coverage, mirroring `GyroscopeTests.cpp`'s shape:
   - `getIsSupportedProperty()` doesn't crash
   - Constructor succeeds (count < 10)
   - `getStateProperty()` reflects support status correctly
   - `Start()` throws `AccelerometerFailedException` when unsupported
     (skip/branch when supported, same pattern as `GyroscopeTests.cpp`)
   - `Stop()` does not crash
   - `Dispose()` succeeds; second `Dispose()` throws `ObjectDisposedException`
   - 11th simultaneous instance throws `SensorFailedException`
3. Build `CNA` + `CnaTests`, run the new suite, then full `ctest` to confirm
   no regressions (failure count should stay at the pre-existing headless
   `EasyGL_*` baseline — 64 as of this writing, but re-check the current
   count since other unrelated work may have changed it).
4. Update `NEXT.md` and `AUDIT.md`'s `Accelerometer` row to drop the "no
   tests yet" note.

### Task P2-4 — Fix Accelerometer.cpp's GetTypeNameCPP naming convention

**Bug:** `Accelerometer.cpp` has
`GetTypeNameCPP(Accelerometer, "Microsoft::Devices::Sensors::Accelerometer")`
— using `::` instead of the project's documented `.`-separated .NET name
convention (`NEXT.md` Section 6 Invariants: `GetTypeName()` returns
`.`-separated names, e.g. `"Microsoft.Devices.Sensors.AccelerometerReading"`).
Every other class added during `plan_devices.md` (`CompassReading`,
`Compass`, `Gyroscope`, `Motion`, etc.) correctly uses dots.

**Steps:**
1. Change to `GetTypeNameCPP(Accelerometer, "Microsoft.Devices.Sensors.Accelerometer")`.
2. If `AccelerometerTests.cpp` already exists from Task P2-3, add/update a
   `GetTypeName()` test case expecting the corrected dotted string.
3. Build + test.

**Explicitly out of scope for this task:** the same `::`-vs-`.` inconsistency
also exists in several files entirely outside `Microsoft::Devices`
(`Cue.cpp`, `AudioEngine.cpp`, `SoundBank.cpp`, `WaveBank.cpp`, `DateTime.cpp`,
`DateTimeOffset.cpp` — grep `GetTypeNameCPP` across the repo to find all).
Fixing those is a separate, larger, cross-cutting cleanup unrelated to the
devices phase — do not touch them here; see Task P2-8 if that cleanup is
ever requested.

---

## Phase 4: Code-quality / checklist compliance review

### Task P2-5 — CHECKLIST.md compliance spot-check

**Goal:** ~20 files were added across `plan_devices.md` in one continuous
session. Do a second-pass review against `CHECKLIST.md`'s per-file
requirements for every new `.hpp`/`.cpp` under `include/Microsoft/Devices/`
and `src/Microsoft/Devices/`:
- `// SPDX-License-Identifier: MS-PL` present at the top of both `.hpp` and
  `.cpp`.
- `#include "CNA/CNAHelper.hpp"` present in every `.hpp` that uses `NOXNA`.
- Every public method/constructor/property/operator/constant has a full
  Doxygen `/** @brief ... */` block (not bare `///`).
- `GetTypeName()` is tagged `NOXNA` and returns the correct dot-separated
  fully-qualified name (cross-check against the P2-4 fix).
- Member order in each `.hpp` roughly matches the order specified in
  `plan_devices.md`'s per-class spec (a reasonable proxy for "C# source
  order" here, since no local C# source exists for this namespace).
- No stray backwards-compatibility shims or unused code introduced.

Report findings; fix anything found as a small follow-up commit (this task
is a review, not a rewrite — expect it to mostly confirm compliance rather
than uncover major issues, since the checklist was followed live during
`plan_devices.md`).

---

## Phase 5: Cross-platform build verification

### Task P2-6 — Verify Vulkan/BGFX desktop builds pick up the new files

**Context:** All of `plan_devices.md` was built and tested only against
`cmake-build-debug` (EASYGL backend, desktop Linux). No `cmake-build-vulkan`
or `cmake-build-bgfx` directory currently exists in this checkout.

**Steps:**
1. Configure fresh build directories:
   ```bash
   cmake -S . -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
   cmake -S . -B cmake-build-bgfx   -DCNA_GRAPHICS_BACKEND=BGFX   -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
   ```
2. Build the `CNA` and `CnaTests` targets in each; the graphics backend
   choice should have zero effect on `Microsoft::Devices::*` compilation
   (it's backend-agnostic), so this is mainly a sanity check that the
   `GLOB_RECURSE` source collection and SDL3 haptic/sensor headers resolve
   identically under different backend configs.
3. Run `ctest` in each; expect the same Devices/Sensors suites to pass, and
   graphics-backend-specific test failure counts to differ from the EASYGL
   baseline (that's expected/unrelated — only check that no
   Devices/Sensors/VibrateController test regresses).
4. Report results; if either backend's build directory already existed with
   a stale cache (per `NEXT.md` Section 4's known issue), delete
   `CMakeCache.txt` and reconfigure first.

### Task P2-7 — Android/iOS cross-compilation (manual/external — not executable in this environment)

**Blocker:** No Android NDK or iOS toolchain is available in this dev
container (`ANDROID_NDK_HOME` unset, no NDK found on disk). This task cannot
be completed here — flag it for whichever environment/CI has the toolchains.

**What to check when it is run:** `Accelerometer.cpp` and `Gyroscope.cpp`
both have `#ifdef __ANDROID__` branches (the landscape axis-remap functions)
that have never been compiled in this session — only the non-Android branch
has been build-verified. Confirm they compile cleanly under the Android NDK
toolchain, and manually verify on a physical device or emulator with a real
accelerometer/gyroscope that `ProcessSensorUpdateEvent()`'s orientation remap
produces sensible values in both `sensorLandscape` rotations (`ROTATION_90`
and `ROTATION_270`) — this can't be verified by unit tests alone since it
depends on live sensor + display-orientation state.

---

---

## Phase 6: VibrateController review + NOXNA extensions

Reference: SDL3's `SDL_HapticEffect` union supports `SDL_HAPTIC_CONSTANT`,
periodic waves (`SINE`/`SQUARE`/`TRIANGLE`/`SAWTOOTHUP`/`SAWTOOTHDOWN`, with
attack/fade envelopes and phase), condition effects
(`SPRING`/`DAMPER`/`INERTIA`/`FRICTION`, axis-position/velocity-driven —
joystick-oriented, likely not useful for a phone/rumble motor),
`SDL_HAPTIC_RAMP` (linear strength ramp), `SDL_HAPTIC_LEFTRIGHT` (independent
large/small motor magnitudes — same shape as `GamePad::SetVibration`), and
`SDL_HAPTIC_CUSTOM` (arbitrary waveform sample data). Introspection:
`SDL_GetHapticFeatures` (bitmask), `SDL_GetMaxHapticEffects`/
`SDL_GetMaxHapticEffectsPlaying`, `SDL_HapticEffectSupported`. Control:
`SDL_SetHapticGain` (0-100 overall scale), `SDL_PauseHaptic`/
`SDL_ResumeHaptic`, `SDL_GetHapticEffectStatus`. None of this is reachable
from the current `VibrateController` — it only ever calls
`SDL_PlayHapticRumble(g_haptic, 1.0f, ms)` (hardcoded full strength).

### Task P2-8 — Review: haptic-device conflict with GamePad

**Finding:** `VibrateController::OpenFirstHapticDevice()`
(`src/Microsoft/Devices/VibrateController.cpp`) opens whichever device
`SDL_GetHaptics()` enumerates first, with no filtering. `GamePad::SetVibration`
(`src/CNA/Internal/Input/SdlInputBridge.cpp:382`) uses a *different* SDL3 API
path — `SDL_RumbleGamepad()` on an `SDL_Gamepad*` handle from
`SDL_OpenGamepad()` — but a haptic-capable game controller connected on
desktop may *also* be enumerable via `SDL_GetHaptics()`. If so,
`VibrateController::Start()` could silently open and buzz the connected
**gamepad** instead of safely no-opping (there's no phone vibrator on
desktop), and could hold a competing open handle to the same physical device
that `GamePad`'s rumble path also targets.

**Steps:**
1. Write a manual/local repro: connect a haptic-capable gamepad on desktop,
   call `VibrateController::Start(TimeSpan::FromMilliseconds(200))`, observe
   whether the gamepad buzzes (this can't be asserted by an automated test
   without real hardware — do this as a manual check, then decide on a fix).
2. If confirmed: filter `OpenFirstHapticDevice()` to skip haptic IDs that are
   also currently open/claimed as a gamepad (e.g. cross-check against
   `SdlInputBridge`'s tracked gamepad handles, or use
   `SDL_GetHapticNameForID`/device metadata to prefer non-joystick devices —
   investigate what's actually available before choosing an approach).
3. If a clean fix isn't feasible without deeper SDL introspection: document
   the caveat prominently in `VibrateController.hpp`'s class doc comment
   (e.g. "on desktop with a connected haptic gamepad and no phone hardware,
   Start() may activate the gamepad's rumble motor instead of no-opping;
   this does not conflict with GamePad's own SetVibration in terms of
   crashing, but the two APIs are unaware of each other").
4. Either way, add a code comment + `NEXT.md`/`AUDIT.md` note recording the
   decision, so it isn't silently rediscovered again.

### Task P2-9 — Confirm hardcoded full-strength rumble is intentional

`VibrateController::Start()` always calls
`SDL_PlayHapticRumble(g_haptic, 1.0f, durationMs)` — full strength, no
intensity parameter, matching the real WP7 `VibrateController.Start(TimeSpan)`
API (which has no intensity concept — WP7-era phone vibration motors were
single-intensity on/off buzzers). Confirm this reasoning in a code comment
(if not already clear) and do not change the XNA-compliant `Start(TimeSpan)`
overload's behavior — variable intensity belongs in a NOXNA overload instead
(Task P2-10).

### Task P2-10 — NOXNA: `Start(duration, intensity)` overload

Add:
```cpp
NOXNA static void Start(const System::TimeSpan& duration, float intensity);
```
`intensity` clamped to `[0.0f, 1.0f]`, passed directly to
`SDL_PlayHapticRumble` instead of the hardcoded `1.0f`. The existing
XNA-compliant `Start(const System::TimeSpan&)` should internally delegate to
this with `intensity = 1.0f` (no behavior change for XNA-API callers). Add
Doxygen noting this is a CNA-specific extension beyond the WP7 API surface.

### Task P2-11 — NOXNA: capability introspection

Add:
```cpp
NOXNA static bool getIsSupportedProperty();
```
Currently `Start()`/`Stop()` always silently no-op with zero way for calling
code to check ahead of time whether vibration is available at all (unlike
every `Microsoft::Devices::Sensors` class, which all expose
`getIsSupportedProperty()`). Wrap `EnsureHapticSubsystemInitialized()` +
"can a haptic device actually be opened" into this query, without holding
the device open as a side effect if the caller is only probing. Optionally
also add `NOXNA static std::string getDeviceNameProperty();` wrapping
`SDL_GetHapticName()` for debug/diagnostic UI.

### Task P2-12 — NOXNA: dual-motor left/right rumble

Add:
```cpp
NOXNA static void StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration);
```
Uses `SDL_HAPTIC_LEFTRIGHT` via `SDL_CreateHapticEffect`/`SDL_RunHapticEffect`
(needs a tracked `SDL_HapticEffectID` so `Stop()` can
`SDL_StopHapticEffect`/`SDL_DestroyHapticEffect` it specifically, alongside
the existing plain-rumble `SDL_StopHapticEffects(g_haptic)` blanket stop).
Mirrors `GamePad::SetVibration`'s two-motor magnitude semantics, but for the
phone/haptic device path — useful for games wanting a strong "hit" pulse on
one motor plus a subtler background rumble on the other simultaneously.
Both `largeMotor`/`smallMotor` clamped to `[0.0f, 1.0f]`, converted to SDL's
`Uint16` magnitude range.

### Task P2-13 — Tests for all Phase 6 additions

Extend `tests/Microsoft/Devices/VibrateControllerTests.cpp` with
`EXPECT_NO_THROW` coverage for each new overload/query (same headless-safe
pattern as the existing 6 tests — this dev container has no haptic hardware,
so every new call must also take its silent-no-op path without throwing):
- `Start(duration, intensity)` for intensity `0.0f`, `0.5f`, `1.0f`, and an
  out-of-range value (e.g. `1.5f`) to confirm clamping doesn't throw.
- `getIsSupportedProperty()` doesn't crash (assert `false` in this headless
  environment, same style as `CompassTests`/`MotionTests`).
- `StartLeftRight(...)` with various magnitude combinations, and a
  `Stop()` afterward.
- If Task P2-8 results in a code change (device filtering), add a comment-
  only note in the test file explaining why the gamepad-conflict scenario
  itself isn't unit-testable (requires real connected hardware).

---

## Verification checklist (apply to every task above)

- Build `cmake --build cmake-build-debug --target CNA` then `--target CnaTests`.
- Run the specific new/changed test suite via `--gtest_filter`.
- Run full `cd cmake-build-debug && ctest --output-on-failure` and confirm
  no new regressions beyond the existing headless `EasyGL_*` baseline.
- Update `NEXT.md` (status, recent changes, known bugs sections) after each
  task, same as throughout `plan_devices.md`.
