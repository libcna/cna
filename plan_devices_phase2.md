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

**Status (2026-07-02): every task is done except Task P2-7**, which is
blocked in this dev container (no Android NDK / iOS toolchain available)
and needs a different environment/CI to complete. All of Phases 1–4 and 6
are fully done; Phase 5 is done except P2-7.

---

## Phase 1: Housekeeping (already partially done)

### Task P2-1 — Status tracking (done as part of opening this plan)
`plan_devices.md`'s Task Summary table now has a Status column (all 31 ✅).
`AUDIT.md` now has a `Microsoft::Devices::Sensors` / `Microsoft::Devices`
section (previously missing entirely, since FNA has no equivalent to diff
against). No further action needed unless new gaps are found in Task P2-2.

---

## Phase 2: API-completeness audit

### Task P2-2 — Independent verification of Sensors API completeness ✅ Done

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

**Resolution (2026-07-02):** Checked every class/struct/enum listed above
against archived Microsoft Learn "previous-versions" MSDN pages (the
`microsoft.devices.sensors.*`/`microsoft.devices.vibratecontroller` family —
same official doc source class as the rest of XNA, high confidence) plus one
MonoGame cross-check for `SensorState`'s enum values (medium confidence, no
direct MSDN enum page found). Full per-class verdicts recorded in `AUDIT.md`.

**Confirmed complete, no action needed:** `SensorBase<T>`, `Accelerometer`
(class shape/`IsSupported`/`State`), `Compass`, `Gyroscope`, `Motion` (class
shape, `IsSupported`, presence/absence of `Calibrate`), `AccelerometerReading`,
`CompassReading`, `GyroscopeReading`, `MotionReading` (including confirming
`DeviceAcceleration`/`DeviceRotationRate` naming, not the previously-assumed
`Acceleration`/`RotationRate`), `AttitudeReading`, `SensorState` (6 values:
`NotSupported`/`Ready`/`Initializing`/`NoData`/`NoPermissions`/`Disabled`).
Also confirmed: the WP7 7.0 legacy `*ReadingChanged`/`*EventArgs` pattern was
genuinely Accelerometer-only — Compass/Gyroscope/Motion are 7.1 Mango-only
additions and never had a 7.0-era API to be legacy-compatible with. No new
file needed for that question; `AUDIT.md`'s existing note already says this
correctly.

**Real gaps found** (four; each promoted to its own task below — Phase 7):
1. `VibrateController` is implemented fully static; the real API is an
   instance API (`VibrateController.Default.Start(...)`) with a static
   `Default` singleton property. Also `Start(TimeSpan)` should throw
   `ArgumentException` outside `[0, 5s]`, not silently clamp. → **Task P2-14**.
2. `Accelerometer.ReadingChanged` (the real, if `[Obsolete]`-tagged, WP7 7.0
   legacy event using the already-built `AccelerometerReadingEventArgs`) was
   never wired onto `Accelerometer` itself. → **Task P2-15**.
3. `SensorFailedException` (and therefore `AccelerometerFailedException`) is
   missing the real API's `ErrorId` property. → **Task P2-16**.
4. `getStateProperty()` is exposed on `Compass`/`Gyroscope`/`Motion`, but
   three independently-fetched official member-list pages for those three
   classes do not list a `State` property — only `Accelerometer`'s page
   documents one. → **Task P2-17**.

**Left unverified (no authoritative source found; not treated as bugs):**
`ISensorReading.Timestamp`/`SensorReadingEventArgs<T>.SensorReading` exact
member names (inferred only from cross-class consistency + widely-cited
tutorial usage), `AccelerometerFailedException : SensorFailedException`
inheritance (assumed from naming convention only), and CNA's own
`MaxSensorCount = 10` simultaneous-instance cap (not documented anywhere in
the real API, but not contradicted either — appears to be a CNA-original
safety constraint from `plan_devices.md` Phase 1). No action taken on these;
re-flag if better sources are ever found.

---

## Phase 3: Known bug fixes

### Task P2-3 — Fix Accelerometer.hpp Dispose() name-hiding + add AccelerometerTests.cpp ✅ Done

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

**Resolution (2026-07-02):** Added `using SensorBase<AccelerometerReading>::Dispose;`
to `Accelerometer.hpp` (same spot/pattern as `Compass.hpp`). Wrote
`tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` with all 7 required
cases, modeled directly on `GyroscopeTests.cpp`. `CNA` + `CnaTests` build
clean; `AccelerometerTests*` 7/7 pass — including
`DisposeSucceedsAndSecondDisposeThrows`, which exercises the no-arg
`Dispose()` call that would not have compiled before this fix. Full `ctest`:
1942 tests total (up from 1935), same 64 pre-existing headless `EasyGL_*`
failures, no regressions. `NEXT.md`/`AUDIT.md` updated.

### Task P2-4 — Fix Accelerometer.cpp's GetTypeNameCPP naming convention ✅ Done

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

**Resolution (2026-07-02):** Changed to
`GetTypeNameCPP(Accelerometer, "Microsoft.Devices.Sensors.Accelerometer")`.
Added a `GetTypeName` test case to `AccelerometerTests.cpp` (from Task P2-3)
asserting the corrected dotted string. `CNA` + `CnaTests` build clean;
`AccelerometerTests*` 8/8 pass. Full `ctest`: 1943 tests total (up from
1942), same 64 pre-existing headless `EasyGL_*` failures, no regressions.

---

## Phase 4: Code-quality / checklist compliance review

### Task P2-5 — CHECKLIST.md compliance spot-check ✅ Done

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

**Resolution (2026-07-02):** Reviewed all 32 files (17 headers + 15 `.cpp`)
under `include/Microsoft/Devices/` and `src/Microsoft/Devices/` against
every checklist item above, with extra scrutiny on the classes most heavily
touched this session (`Accelerometer`, `SensorFailedException`,
`AccelerometerFailedException`, `Compass`, `Gyroscope`, `Motion`,
`VibrateController` — Tasks P2-3/P2-4/P2-15/P2-16/P2-17/P2-10–P2-13).
SPDX headers (32/32), `CNAHelper.hpp` presence wherever `NOXNA` is used, and
absence of bare `///` on public declarations were all verified
programmatically across every file — zero gaps. Doxygen completeness and
`GetTypeName()` dot-convention correctness were verified by direct read of
all 17 headers — all correct (including confirming `VibrateController` and
the two exception types correctly have *no* `GetTypeName()`, since none of
them derive `System::Object`). `SensorReadingEventArgs.cpp` is an
intentionally-near-empty template placeholder — expected, not a gap.

Two minor, pre-existing (not introduced this session) findings, both fixed:
1. `ISensorReading.hpp` and `SensorState.hpp` both carried a stale
   `@note Status: Partial.` doc-comment line, left over from before Task
   P2-2's audit confirmed both are ✅ complete. Removed both.
2. `AccelerometerFailedException.hpp` had two small style inconsistencies
   vs. every sibling file in the namespace: a bare relative
   `#include "SensorFailedException.hpp"` instead of the full-path
   `#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"` style
   used everywhere else, and a stray trailing `};` closing the namespace
   instead of `} // namespace Microsoft::Devices::Sensors`. Normalized both.

No functional bugs found — this task's own prediction ("expect it to mostly
confirm compliance") held. `CNA` + `CnaTests` build clean after the fixes.
Full `ctest`: 1964 tests total (unchanged), same 64 pre-existing headless
`EasyGL_*` failures, no regressions.

---

## Phase 5: Cross-platform build verification

### Task P2-6 — Verify Vulkan/BGFX desktop builds pick up the new files ✅ Done

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

**Resolution (2026-07-02):** Neither build directory existed yet (no stale
cache to worry about). **Vulkan:** `find_package(Vulkan REQUIRED)` located
the system Vulkan SDK (1.4.309) cleanly; `CNA` + `CnaTests` built clean.
**BGFX:** `FetchContent` pulled `bgfx.cmake` from GitHub (~6 minutes to
configure, since it clones bgfx + submodules); `CNA` + `CnaTests` built
clean after that (a genuinely long build — bgfx itself is a large C++
codebase — run in the background). Both confirm the task's own prediction:
the backend choice has zero effect on `Microsoft::Devices::*` compilation.

Targeted `ctest -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController"`:
**139/139 pass under both Vulkan and BGFX** — identical count to the EASYGL
baseline, zero Devices/Sensors/VibrateController regressions. Full `ctest`
run in each (for context, not a pass/fail bar — failure counts differing
from EASYGL's 64 is expected, per this task's own text): Vulkan 1899/1912
passed (13 failures, all "Unable to find executable" for Vulkan-specific
demo/smoke-test binaries not built by the `CNA`/`CnaTests` targets — e.g.
`cna_demo_2d`, `cna_test_vulkan_instanced`, etc.); BGFX 1903/1906 passed (3
failures, same pattern — `cna_demo_2d`, `cna_test_bgfx_render_target_usage`,
`cna_test_bgfx_vertex_format`). None of these missing-executable failures
are `Microsoft::Devices`-related; they're separate `add_executable` smoke
targets outside `CNA`/`CnaTests`, out of this task's scope to build.
`cmake-build-vulkan`/`cmake-build-bgfx` left on disk (not committed —
`.gitignore`d like `cmake-build-debug`) for future reference.

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

### Task P2-8 — Review: haptic-device conflict with GamePad ✅ Done

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

**Resolution:** Confirmed the risk mechanistically (no real gamepad hardware
was available to physically reproduce it in this dev container) by reading
the vendored SDL3 Linux haptic backend directly:
`third_party/SDL/src/haptic/linux/SDL_syshaptic.c`'s `SDL_SYS_HapticInit()`
scans `/dev/input/event0..31` independently of the joystick subsystem,
opening each node with its own fd and probing `EV_IsHaptic()` via
`ioctl(EVIOCGBIT(EV_FF, ...))`. A rumble-capable game controller therefore
*is* enumerated by `SDL_GetHaptics()` as its own device, with zero
correlation to the `SDL_JoystickID`/`SDL_Gamepad` that `GamePad::SetVibration`
(`SDL_RumbleGamepad`) separately manages — confirming the risk is real, not
theoretical.

**Fix implemented** (not just documented — a clean-enough approach was
found): `VibrateController.cpp` now has `IsConnectedGamepadHapticDevice()`,
which cross-references each candidate `SDL_HapticID`'s name
(`SDL_GetHapticNameForID`) against every currently connected joystick's name
(`SDL_GetJoysticks()` + `SDL_GetJoystickNameForID()`, both non-invasive — no
joystick is opened just to check). `OpenFirstHapticDevice()` skips any
haptic device whose name matches a connected joystick. Name-matching was
chosen over ID correlation because SDL3 exposes no direct
`SDL_HapticID`-to-`SDL_JoystickID` mapping without opening the joystick
(`SDL_OpenHapticFromJoystick` requires an already-open `SDL_Joystick*`), and
opening every connected joystick just to probe it was judged too invasive
for what should be a lightweight `Start()` call. Residual caveat (documented
in code, not fixed): two physically distinct controllers that happen to
report the identical product name would both be excluded/included together;
accepted as a rare edge case not worth the complexity of a more invasive fix.

Net effect: if the *only* haptic-capable device present is a game
controller, `VibrateController::Start()` now correctly falls into its
existing silent no-op path (matching "no phone hardware present" desktop
semantics) instead of ever moving the controller's motors — leaving
`GamePad::SetVibration` as the sole path for controller rumble. Verified:
`CNA` + `CnaTests` build clean, `VibrateControllerTests` 6/6 pass, full
`ctest` still at the same pre-existing 64 headless `EasyGL_*` failures (no
regressions). `VibrateController.hpp`'s class doc comment and
`VibrateControllerTests.cpp` both updated with notes on this behavior.

### Task P2-9 — Confirm hardcoded full-strength rumble is intentional ✅ Done

`VibrateController::Start()` always calls
`SDL_PlayHapticRumble(g_haptic, 1.0f, durationMs)` — full strength, no
intensity parameter, matching the real WP7 `VibrateController.Start(TimeSpan)`
API (which has no intensity concept — WP7-era phone vibration motors were
single-intensity on/off buzzers). Confirm this reasoning in a code comment
(if not already clear) and do not change the XNA-compliant `Start(TimeSpan)`
overload's behavior — variable intensity belongs in a NOXNA overload instead
(Task P2-10).

**Resolution (2026-07-02):** Satisfied as a natural byproduct of implementing
Task P2-10 — the new `Start(const System::TimeSpan&, float intensity)`
overload's Doxygen comment in `VibrateController.hpp` directly states this
reasoning ("CNA targets more capable hardware than 2010-era WP7 phones
(whose vibration motors were single-intensity on/off buzzers, hence the
plain WP7 Start(TimeSpan) has no intensity concept)"). The XNA-compliant
`Start(const System::TimeSpan&)` overload's own behavior is unchanged (it
delegates to the new overload with `intensity = 1.0f`). No separate code
change was needed for this task.

### Task P2-10 — NOXNA: `Start(duration, intensity)` overload ✅ Done

Add:
```cpp
NOXNA static void Start(const System::TimeSpan& duration, float intensity);
```
`intensity` clamped to `[0.0f, 1.0f]`, passed directly to
`SDL_PlayHapticRumble` instead of the hardcoded `1.0f`. The existing
XNA-compliant `Start(const System::TimeSpan&)` should internally delegate to
this with `intensity = 1.0f` (no behavior change for XNA-API callers). Add
Doxygen noting this is a CNA-specific extension beyond the WP7 API surface.

**Resolution (2026-07-02):** Added as an **instance** method per the Phase 7
ordering note (`NOXNA void Start(const System::TimeSpan&, float intensity);`),
not `static`. `Start(const System::TimeSpan&)` now simply calls
`Start(duration, 1.0f)` — zero behavior change for XNA-API callers, verified
by the pre-existing duration tests still passing unchanged. `intensity`
clamped via `std::clamp` (new `<algorithm>` include).

### Task P2-11 — NOXNA: capability introspection ✅ Done

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

**Resolution (2026-07-02):** Added both, as instance methods
(`getIsSupportedProperty()`, `getDeviceNameProperty()`). Factored a shared
private `AcquireHapticDeviceForProbe(bool& openedTemporary)` helper: reuses
`g_haptic` if a device is already open (from a prior `Start()`/
`StartLeftRight()` call), otherwise opens one temporarily and reports via
`openedTemporary` so the caller closes it again afterward — satisfies "don't
hold the device open as a side effect of probing." Empirically confirmed in
this dev container (via a standalone throwaway probe program linked against
the built library, not committed) that `getIsSupportedProperty()` returns
`false` and `getDeviceNameProperty()` returns `""` here — genuinely no
haptic hardware, confirming the original task assumption.

### Task P2-12 — NOXNA: dual-motor left/right rumble ✅ Done

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

**Resolution (2026-07-02):** Added as an instance method. New file-static
`SDL_HapticEffectID g_leftRightEffectId = -1` (SDL's own "no effect"
sentinel, matching `SDL_CreateHapticEffect`'s documented -1-on-failure
return). Checks `SDL_GetHapticFeatures(g_haptic) & SDL_HAPTIC_LEFTRIGHT`
before creating the effect — silent no-op if the device doesn't support
dual-motor rumble (same silent-no-op philosophy as the rest of this class).
`Stop()` now also calls `SDL_DestroyHapticEffect()` on a tracked ID (after
resetting it to `-1`) in addition to the existing
`SDL_StopHapticEffects(g_haptic)` blanket stop — the plan's note that
`SDL_StopHapticEffects` alone doesn't free the uploaded effect slot is
correct; destroying it explicitly is necessary cleanup, not redundant.

### Task P2-13 — Tests for all Phase 6 additions ✅ Done

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

**Resolution (2026-07-02):** Added 10 new tests. One deliberate deviation
from this task's original text: rather than hardcoding
`EXPECT_FALSE(getIsSupportedProperty())`, the test only asserts no-crash
(mirrors `AccelerometerTests`/`GyroscopeTests`' branch-on-live-result
convention, not `CompassTests`/`MotionTests`' hardcoded-`NotSupported`
convention) — `VibrateController`'s haptic availability, like
`Accelerometer`'s sensor availability, is a genuine hardware question, not a
permanent stub by design like `Compass`/`Motion`'s missing magnetometer API,
so hardcoding `false` would be a latent flake on any environment/CI runner
that does have haptic hardware. (Empirically confirmed `false` in *this*
specific dev container per Task P2-11's resolution note, but the test
itself doesn't rely on that.) Covered: 4 intensity-clamping cases + 1
intensity-overload-still-validates-duration case, `getIsSupportedProperty()`,
`getDeviceNameProperty()`, 2 `StartLeftRight` cases (normal + out-of-range
magnitudes) + `Stop()`, 1 `StartLeftRight` duration-validation case. `CNA` +
`CnaTests` build clean; `VibrateControllerTests*` 20/20 pass (up from 10).
Full `ctest`: 1964 tests total (up from 1954), same 64 pre-existing headless
`EasyGL_*` failures, no regressions.

This closes out all of Phase 6 (P2-10 through P2-13). Only `Task P2-9`
(confirm-only, no code change needed) remains open in Phase 6.

---

## Phase 7: Gaps found by Task P2-2's audit (2026-07-02)

**Ordering note (resolved 2026-07-02):** Task P2-14 is done —
`VibrateController` now exposes the `Default`-singleton/instance pattern
(`getDefaultProperty()`). Phase 6's remaining NOXNA extensions (P2-10
through P2-13, not yet implemented) should be added as instance methods on
that same singleton for API consistency, not as separate free-standing
statics — the original Phase 6 task text below still describes them as
`static`, which is now stale; treat every `NOXNA static ...` signature shown
there as `NOXNA <ReturnType> ...` (instance method) instead when
implementing.

### Task P2-14 — Fix `VibrateController`'s API shape: static-only → `Default` singleton + instance methods ✅ Done

**Bug (confirmed via archived MSDN `ff402741`/`ff403287`, high confidence):**
real `Microsoft.Devices.VibrateController` is `public class VibrateController`
with a static `Default` property returning a singleton instance; `Start`/`Stop`
are **instance** methods called as `VibrateController.Default.Start(...)`.
CNA currently implements the whole class as static-only
(`VibrateController::Start(...)`, `= delete`d default constructor, per
`NEXT.md`), which is an XNA API **shape** deviation, not just an internal
detail — violates this project's "must preserve original XNA 4.0 class
names, method signatures, and behavior" rule.

Also confirmed: real `Start(TimeSpan duration)` throws `ArgumentException`
for `duration < TimeSpan.Zero` or `duration > TimeSpan.FromSeconds(5)`. CNA's
current doc comment says it silently clamps instead — also wrong.

**Steps:**
1. Add a private/hidden default constructor (not `= delete`d) and a static
   `getDefaultProperty()` returning a reference/pointer to a
   function-local-static singleton instance (mirrors how other XNA
   singletons are already done elsewhere in this codebase — grep for an
   existing `getDefaultProperty()`/singleton pattern to match conventions
   before inventing a new one).
2. Convert `Start(const System::TimeSpan&)` and `Stop()` from static to
   instance methods on that singleton. Existing internal
   static state (`g_haptic`, `instanceCount_`-equivalents, etc.) can stay as
   file-static implementation detail — only the *public* shape changes.
3. Change `Start()`'s out-of-range behavior from silent clamp to throwing
   `std::out_of_range` or the project's `System::ArgumentException` mapping
   (check `sharp-runtime` for the existing `System::ArgumentException` type
   before adding a new one) for `duration < TimeSpan::Zero` or
   `duration > TimeSpan::FromSeconds(5)`.
4. Update every existing call site in this codebase (tests, any sample/demo
   code) from `VibrateController::Start(...)` to
   `VibrateController::getDefaultProperty()->Start(...)` (or `.Start(...)`,
   depending on whether the getter returns pointer or reference — match
   existing singleton convention).
5. Rewrite `tests/Microsoft/Devices/VibrateControllerTests.cpp` for the new
   shape; add cases for the `ArgumentException` boundary (exactly `0s`,
   exactly `5s` should NOT throw; `-1ms` and `5.001s` should).
6. Update `VibrateController.hpp`'s class doc comment, `NEXT.md`, and
   `AUDIT.md`'s `VibrateController` row to drop the "static-only" claim.
7. Build + full `ctest`; confirm no regression beyond the pre-existing
   headless `EasyGL_*` baseline.

**Resolution (2026-07-02):** Matched the `Microphone::getDefaultProperty()`
pattern already used elsewhere in this codebase
(`Microsoft::Xna::Framework::Audio::Microphone`) rather than inventing a new
one. `VibrateController`'s default constructor is now private (`= default`,
not `= delete`d); `getDefaultProperty()` returns a pointer to a
function-local-static singleton, never null (unlike `Microphone::Default`,
which can be null — a physical vibration motor's *availability* doesn't
gate whether `Default` exists, only whether `Start()` ends up a no-op).
`Start(const System::TimeSpan&)` and `Stop()` are now instance methods; the
underlying `g_haptic` state stays a file-static implementation detail,
unaffected by the public shape change. Out-of-range duration now throws
`System::ArgumentOutOfRangeException` (a real sharp-runtime type — closer to
.NET's actual range-violation exception than the plainer `ArgumentException`
the audit's summary used loosely) instead of silently clamping; the boundary
is exact — `TimeSpan::Zero` and `TimeSpan::FromSeconds(5)` do not throw,
anything outside that closed interval does. The only call site outside the
library itself was `VibrateControllerTests.cpp`, which was rewritten for the
new shape (10 tests, up from 6): added `getDefaultProperty()` non-null and
same-instance checks, and boundary tests for both ends of the throw range.
`CNA` + `CnaTests` build clean; `VibrateControllerTests*` 10/10 pass. Full
`ctest`: 1947 tests total (up from 1943), same 64 pre-existing headless
`EasyGL_*` failures, no regressions. `VibrateController.hpp`'s class doc
comment, `NEXT.md`, and `AUDIT.md` updated to drop the "static-only" claim.

### Task P2-15 — Wire up `Accelerometer.ReadingChanged` (WP7 7.0 legacy event) ✅ Done

**Gap (confirmed via archived MSDN `ff707930`):** real `Accelerometer` has
`[Obsolete("use CurrentValueChanged")] public event
EventHandler<AccelerometerReadingEventArgs> ReadingChanged`. CNA already
built `AccelerometerReadingEventArgs` "for API completeness" (per
`AUDIT.md`) but never added the actual event to `Accelerometer` — it's real,
if deprecated, public XNA surface, so this is a completeness gap, not a
design choice to skip.

**Steps:**
1. Add `System::EventHandler<AccelerometerReadingEventArgs> ReadingChanged;`
   to `Accelerometer.hpp`, with a Doxygen note that it's the deprecated WP7
   7.0 legacy event, superseded by `CurrentValueChanged` (mirror the C#
   `[Obsolete]` intent in the doc comment — no direct C++ `[[deprecated]]`
   requirement unless it's easy to add consistently).
2. Raise it from the same SDL sensor-update path that already raises
   `CurrentValueChanged`, constructing an `AccelerometerReadingEventArgs`
   from the current `AccelerometerReading`.
3. Add a test in `AccelerometerTests.cpp` (from Task P2-3 — write together if
   P2-3 hasn't landed yet, otherwise extend it) subscribing to
   `ReadingChanged` and asserting it fires alongside `CurrentValueChanged`.
4. Build + test.

**Resolution (2026-07-02):** Added `System::EventHandler<AccelerometerReadingEventArgs>
ReadingChanged;` to `Accelerometer.hpp` (public, alongside `GetTypeNameHPP()`),
with a Doxygen note that it's the deprecated WP7 7.0 legacy event superseded
by `CurrentValueChanged`. Raised it from the end of
`ProcessSensorUpdateEvent()` in `Accelerometer.cpp`, right after the existing
`setCurrentValueProperty(accelerometerReading)` call that raises
`CurrentValueChanged` — same reading data (`X`/`Y`/`Z` from
`AccelerometerReading::getAccelerationProperty()`, `Timestamp` from
`getTimestampProperty()`), guarded by the same `getIsDataValidProperty()` +
`!ReadingChanged.Empty()` pattern already used for `CurrentValueChanged`.
Added `AccelerometerTests.cpp::ReadingChangedSubscriptionDoesNotThrow`: per
this test file's own documented limitation (no accelerometer hardware in
this dev container, so `SDL_EVENT_SENSOR_UPDATE` never actually fires
headless — same class of limitation as `VibrateControllerTests`' untestable
gamepad-conflict scenario), the test can only confirm subscribing doesn't
crash and `Start()`/`Stop()` still behave correctly with a subscriber
attached; it cannot force-fire and assert co-occurrence with
`CurrentValueChanged` without real hardware. `CNA` + `CnaTests` build clean;
`AccelerometerTests*` 9/9 pass (up from 8).

**Incidental fix required to unblock this task's build (unrelated to
`Microsoft::Devices`):** `sharp-runtime` (a sibling repo under concurrent
work elsewhere) grew `System::IAsyncResult` from 2 to 4 pure-virtual members
(`getAsyncStateProperty()`, `getAsyncWaitHandleProperty()`) between the P2-14
and P2-15 sessions, which broke compilation of
`src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp`'s internal
`SelectorResult`/`ContainerResult` helper classes (unrelated to this phase).
Since `CNA` is a single static-library target, this had to be fixed to
verify anything at all. Fixed minimally: both classes' `void* asyncState`
field became `std::any asyncState` (assignment call sites unchanged — a
`void*` converts into `std::any` implicitly) plus the two new overrides;
both gained a `mutable System::Threading::EventWaitHandle asyncWaitHandle`
member, pre-signaled (`{true, EventResetMode::ManualReset}`), since these
result types are always synchronously completed — matching their existing
`getIsCompletedProperty()`/`getCompletedSynchronouslyProperty()` semantics.
No `Microsoft::Xna::Framework::Storage` tests exist to regression-check
against; full `ctest` (all namespaces) shows no new failures beyond the
pre-existing 64 headless `EasyGL_*` baseline, so this is at minimum
non-regressing. Not otherwise investigated further — out of this phase's
scope.

### Task P2-16 — Add `ErrorId` to `SensorFailedException` ✅ Done

**Gap (confirmed via archived MSDN `hh239104`):** real
`SensorFailedException` has `public int ErrorId { get; }`. CNA's
`SensorFailedException`/`AccelerometerFailedException` currently only have
message-based constructors, no `ErrorId`.

**Steps:**
1. Add an `intcs errorId_` member and `getErrorIdProperty()` getter to
   `SensorFailedException`, plus a constructor overload accepting an error
   ID alongside the message (keep the existing message-only constructor for
   source compat with current call sites — check whether the real API
   actually has both a 1-arg and 2-arg constructor, or only ever passes
   `ErrorId` in one specific constructor form, before deciding which
   overloads to keep).
2. Propagate the same addition to `AccelerometerFailedException` if it
   inherits from `SensorFailedException` (unverified assumption — confirm
   the actual inheritance relationship while implementing this, since P2-2's
   research could not find a direct source for it; if it turns out NOT to
   inherit from `SensorFailedException`, `ErrorId` still needs adding to it
   independently since both are real WP7 exception types).
3. Update any current throw sites (`Start()` in each sensor class) to pass a
   reasonable `errorId` value — check what real error-code values (if any)
   are documented; if none are documented, `0`/unspecified is acceptable
   with a note.
4. Add/extend tests asserting `getErrorIdProperty()` round-trips the value
   passed to the constructor.
5. Build + test.

**Resolution (2026-07-02):** Confirmed `AccelerometerFailedException` does
inherit `SensorFailedException` (already true before this task — see its
`.hpp`). Added `SharpRuntime::intcs errorId_ = 0;` and
`getErrorIdProperty()` to `SensorFailedException`, plus a new
`SensorFailedException(const char* str, SharpRuntime::intcs errorId)`
constructor overload; kept the existing message-only constructor unchanged
(defaults `errorId_` to `0`) for source compatibility with every current
throw site. Added the matching `AccelerometerFailedException(const char*,
SharpRuntime::intcs)` overload, delegating to the base. **Existing throw
sites left unchanged** (still use the message-only constructor, so
`ErrorId` reads `0`) — no real WP7 error-code values are documented
anywhere Task P2-2's research found, so inventing numbers would be
guesswork; `0`/unspecified is the honest default until a real source turns
up. Extended `SensorFailedExceptionTests.cpp` and
`AccelerometerFailedExceptionTests.cpp` (3 new tests each: default-ctor and
message-ctor both give `ErrorId == 0`, new ctor round-trips a non-zero
value alongside the message). `CNA` + `CnaTests` build clean; both suites
13/13 pass. Full `ctest`: 1954 tests total (up from 1948), same 64
pre-existing headless `EasyGL_*` failures, no regressions.

### Task P2-17 — Resolve `getStateProperty()` on `Compass`/`Gyroscope`/`Motion` ✅ Done

**Gap (medium-high confidence — absence across 3 independently-fetched
official member-list pages, not one explicit negative statement):** CNA
exposes `getStateProperty()` on `Compass`, `Gyroscope`, and `Motion`, but
none of their documented member lists show a `State` property — only
`Accelerometer`'s page documents one, as an `Accelerometer`-specific member,
not something inherited from `SensorBase<T>` (already independently
confirmed `SensorBase<T>` itself has no `State`).

**Steps:**
1. Before changing anything, spend a little more effort trying to find a
   direct, explicit source (positive or negative) for `State` on these three
   classes — the current evidence is absence-based, not a confirmed
   negative statement, so it's worth one more targeted check.
2. If genuinely undocumented: do **not** silently remove it (existing tests
   and `Compass`/`Gyroscope`/`Motion` callers already depend on it, and it's
   harmless/useful). Instead, tag it `NOXNA` on the declaration in each of
   the three headers, with a Doxygen note that it's a CNA convenience
   extension beyond the documented WP7 API (mirroring `Accelerometer`'s real
   `State`, for API symmetry across the sensor family), per this project's
   "wrap non-XNA functionality in `Microsoft::Xna`/`Microsoft::Devices` with
   `NOXNA`" rule.
3. If a source is found either confirming or denying it definitively, update
   this task's resolution accordingly instead (no `NOXNA` tag needed if it
   turns out to be real, documented API).
4. Update `AUDIT.md`'s rows for the three classes to note the `NOXNA` tag (or
   the newly-found confirmation).
5. Build + test (no behavior change expected, just the tag + doc comment).

**Resolution (2026-07-02):** Step 1's "look harder" paid off — fetched each
class's exact "The `X` type exposes the following members" page directly
(the authoritative, exhaustive member-list page, not an inferred-absence
proxy this time): `Compass` (`hh220912`), `Gyroscope` (`hh239201`), `Motion`
(`hh239189`). All three list exactly the same 4 Properties —
`CurrentValue`, `IsDataValid`, `IsSupported`, `TimeBetweenUpdates` — and no
`State`, upgrading this from "medium-high confidence" to definitively
confirmed. Cross-checked `Accelerometer.ReadingChanged`'s own page
(`ff707930`) as a sanity check on the page format while there; unrelated to
this task but reconfirms Task P2-15's implementation matches the real
`[ObsoleteAttribute("use CurrentValueChanged")]` syntax exactly.

Tagged `getStateProperty()` `NOXNA` on all three headers
(`Compass.hpp`/`Gyroscope.hpp`/`Motion.hpp`), each with a Doxygen note
explaining it's a CNA symmetry extension (mirroring `Accelerometer`'s real
`State` property) beyond the documented WP7 API. `Gyroscope.hpp` didn't
already include `CNA/CNAHelper.hpp` (needed for the `NOXNA` macro) —
`Compass.hpp`/`Motion.hpp` already did; added it there. No behavior change,
no new tests needed (`NOXNA` is a no-op marker macro) — existing
`CompassTests`/`GyroscopeTests`/`MotionTests` (21 total) still pass
unchanged. `CNA` + `CnaTests` build clean. Full `ctest`: 1954 tests total
(unchanged from Task P2-16), same 64 pre-existing headless `EasyGL_*`
failures, no regressions.

This closes out all four gaps found by Task P2-2's audit (P2-14, P2-15,
P2-16, P2-17) — every follow-up task from that pass is now done.

---

## Verification checklist (apply to every task above)

- Build `cmake --build cmake-build-debug --target CNA` then `--target CnaTests`.
- Run the specific new/changed test suite via `--gtest_filter`.
- Run full `cd cmake-build-debug && ctest --output-on-failure` and confirm
  no new regressions beyond the existing headless `EasyGL_*` baseline.
- Update `NEXT.md` (status, recent changes, known bugs sections) after each
  task, same as throughout `plan_devices.md`.
