# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS.

**Baseline `Microsoft::Devices` API surface is complete and tested against
the documented WP7 spec.** Remaining work (see `plan_devices_phase4.md`) is
hardening, deeper testing, and cross-platform verification — not API
completeness. Don't re-litigate API-shape questions already closed by
`plan_devices.md`/`plan_devices_phase2.md`/`plan_devices_phase3.md`.

**Main goal (current phase):** the `Microsoft::Devices` namespace —
`Microsoft::Devices::Sensors` (Accelerometer, Compass, Gyroscope, Motion, and
their reading/event-args/exception types) plus
`Microsoft::Devices::VibrateController` — is functionally complete against
the documented Windows Phone 7 XNA API spec. Branch: `feature/devices`.
Two plans are fully closed (`plan_devices.md`, `plan_devices_phase2.md`,
except one environment-blocked task); a third, `plan_devices_phase3.md`, is
**done** as of 2026-07-03 — 11 of 12 tasks fully complete (all 3 confirmed
real bugs fixed, the 1 decision task resolved, all 6 test-coverage-gap
tasks filled), and the 12th (Task P3-12, low-priority research) is
partially resolved (`CalibrationEventArgs` confirmed correct;
`SensorFailedException`'s exact constructor signature remains an educated
guess after a genuine research effort, judged not worth pursuing further).
**A fourth plan, `plan_devices_phase4.md`, is open** (user-authored
hardening plan: event-callback lifetime safety, real event-path testing,
a real timestamp bug, SDL sensor-subsystem ownership, `VibrateController`
hardening, cross-platform build, a demo screen — 14 tasks across 8 phases).
Tasks P4-1 through P4-7 are done as of 2026-07-03 (doc cleanup; callback
lifetime-safety fix + synthetic-event test hook; real event-path tests for
`CurrentValueChanged`/`ReadingChanged` on both `Accelerometer` and
`Gyroscope`, including `Stop()`'s effect; the confirmed `Timestamp` bug is
fixed). Tasks P4-8 through P4-14 have not started yet.

**Important architectural decisions:**
- Public API names/signatures must match XNA 4.0 (or, for `Microsoft::Devices`,
  the documented WP7 SDK) exactly; C# properties become `getXProperty()` /
  `setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the
  shared base for all sensor classes (`CurrentValue`, `IsDataValid`,
  `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`).
- `VibrateController` is a singleton reached via `getDefaultProperty()`
  (matches the real WP7 `VibrateController.Default` instance API — fixed
  2026-07-02, it used to be incorrectly static-only). It does not derive
  `SensorBase<T>`/`IDisposable` — it does not follow the sensor pattern.
- FNA (the usual local reference tree for XNA behavior) implements **no**
  equivalent of `Microsoft::Devices` at all (it's WP7-only) — this namespace
  has no local reference tree to diff against; API completeness is judged
  from archived Microsoft Learn "previous-versions" WP7 SDK docs instead
  (fetched directly per-class, not from memory — see Section 6).
- Tests live under `tests/` mirroring the `include`/`src` namespace path
  1:1, using Google Test, one file per class.

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly with the `EASYGL` backend
(`cmake-build-debug`) as of the last verified build (2026-07-03, HEAD
`0c5bc25` + uncommitted Task P3-11 work). Also verified clean under
`VULKAN` (`cmake-build-vulkan`) and `BGFX` (`cmake-build-bgfx`) as of
2026-07-02 — the graphics backend choice has zero effect on
`Microsoft::Devices::*` compilation, confirmed empirically. (Vulkan/BGFX
not re-verified after any of the Task P3-* work this session, since it all
only touches `Microsoft::Devices` headers/cpp/tests, same reasoning as
before — worth a real re-verification pass before considering this phase
fully closed out, see Section 8.)

**Tests:** last full `ctest` run (`EASYGL`): **1985 tests total, 97%
passing.** The only failures are a fixed set of **64 pre-existing
`EasyGL_*` graphics tests** that cannot run headless (no display/GPU in
this dev environment) — present before this phase began, unrelated to
`Microsoft::Devices`. No regressions have been introduced across the whole
`plan_devices_phase3.md` pass (1964 → 1970 → 1953 (Task P3-2 removed 20
setter tests, a churn not a loss) → 1966 → 1972 → 1985; see Section 3 for
the per-task breakdown). Under `VULKAN`/`BGFX`, the targeted
Devices/Sensors/VibrateController suite was **139/139** passing on both as
of 2026-07-02 (not re-run since); full-suite counts differ from `EASYGL`
only because backend-specific demo/smoke-test
executables weren't built (not a regression).

**Working:**
- Full `Microsoft::Devices::Sensors` namespace: `Accelerometer` (real,
  SDL3-backed — `SDL_SENSOR_ACCEL`, Android landscape axis remap, untested
  on real Android hardware), `AccelerometerReading`,
  `AccelerometerReadingEventArgs` (WP7 7.0 legacy, now wired to
  `Accelerometer.ReadingChanged`), `AccelerometerFailedException`,
  `SensorFailedException` (now has `ErrorId`), `SensorBase<T>`,
  `SensorReadingEventArgs<T>`, `ISensorReading`, `SensorState`,
  `CalibrationEventArgs`, `CompassReading`/`Compass` (stub, see below),
  `GyroscopeReading`/`Gyroscope` (real, SDL3-backed — `SDL_SENSOR_GYRO`),
  `AttitudeReading`, `MotionReading`, `Motion` (stub, see below). All have
  passing test suites.
- `Microsoft::Devices::VibrateController` — singleton
  (`getDefaultProperty()`), SDL3 haptic-backed. XNA-compliant
  `Start(TimeSpan)`/`Stop()`, plus `NOXNA` extensions:
  `Start(TimeSpan, float intensity)`, `getIsSupportedProperty()`,
  `getDeviceNameProperty()`, `StartLeftRight(largeMotor, smallMotor,
  duration)`. Filters out haptic devices that are also connected gamepads
  so it can't compete with `GamePad::SetVibration`.

**Does not work / not done yet:**
- `Compass` and `Motion` are permanent stubs — SDL3 exposes no magnetometer
  API on any platform, so both are always `SensorState::NotSupported` and
  `Start()` always throws. This is by design, not a gap, until SDL3 gains
  magnetometer support.
- Android/iOS cross-compilation has **never** been verified — no Android
  NDK / iOS toolchain is available in this dev container.
  `Accelerometer.cpp`/`Gyroscope.cpp`'s `#ifdef __ANDROID__` branches have
  never been compiled by any compiler.
- All three confirmed real bugs are fixed (Tasks P3-1, P3-4, P3-5), the one
  decision task is resolved (P3-2), and every test-coverage-gap task is
  filled (Tasks P3-6 through P3-11) — all 2026-07-03: see Section 4. Task
  P3-3 was confirmed already-satisfied (no change needed). Task P3-12 is
  partially resolved (`CalibrationEventArgs` confirmed correct;
  `SensorFailedException`'s constructor signature stays an educated guess).
  **`plan_devices_phase3.md` has no further actionable work.**

---

## 3. Recent changes

- `plan_devices_phase2.md` — **all 17 tasks done** (2026-07-02): fixed
  `Accelerometer`'s `Dispose()` name-hiding bug + added its missing test
  file; corrected its `GetTypeName()` dot-convention; independently
  re-verified `Microsoft::Devices::Sensors`/`VibrateController` API
  completeness against archived WP7 SDK docs, finding and fixing four real
  gaps (`VibrateController`'s static→singleton API shape,
  `Accelerometer.ReadingChanged` wiring, `SensorFailedException.ErrorId`,
  `Compass`/`Gyroscope`/`Motion`'s `getStateProperty()` `NOXNA` tagging);
  implemented `VibrateController`'s Phase 6 `NOXNA` extensions (variable
  intensity, capability introspection, dual-motor rumble); ran a
  `CHECKLIST.md` compliance spot-check (2 trivial style fixes); verified
  Vulkan/BGFX desktop builds. Only `Task P2-7` (Android/iOS) remains open,
  blocked by missing toolchain.
- **Incidental fix, unrelated to `Microsoft::Devices`:** `sharp-runtime`
  (sibling repo) grew `System::IAsyncResult` to 4 pure-virtual members
  mid-session, breaking `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp`'s
  internal `SelectorResult`/`ContainerResult` helpers. Fixed minimally
  (`std::any asyncState` + a pre-signaled `EventWaitHandle` member) since
  `CNA` is one static-library target and this blocked all verification.
- `plan_devices_phase3.md` (new, 2026-07-02) — a third, deeper research pass
  (API-completeness re-audit against archived WP7 docs, line-by-line
  implementation review cross-checked against vendored SDL3 source, and a
  test-coverage gap analysis against `CHECKLIST.md`) found real issues the
  earlier passes didn't catch.
- **Task P3-1 done (2026-07-03):** `SensorBase<T>::getCurrentValueProperty()`
  now throws `System::InvalidOperationException` when the owning sensor is
  unsupported, matching the documented WP7 behavior. Added `isSupported_` +
  `setIsSupportedProperty()` to `SensorBase.hpp`; all 4 derived constructors
  set it from their own `getIsSupportedProperty()` result. 6 new tests
  across `Accelerometer`/`Compass`/`Gyroscope`/`Motion`. No existing test
  touched `getCurrentValueProperty()` before this, so zero test churn.
  1970 tests total now (up from 1964), same 64 pre-existing headless
  failures, no regressions. Full writeup:
  `plan_devices_phase3.md` Task P3-1's "Resolution" section.
- **Task P3-4 done (2026-07-03):** `Accelerometer`/`Gyroscope`'s shared
  static sensor state (`g_sensor_`/`g_sensorId_`/`eventWatchRegistered_`/
  `startedInstances_`) is now guarded by a `static std::mutex`, closing the
  thread-safety race against the SDL event-watch callback (SDL's own
  `SDL_AddEventWatch()` doc warns it may run off-thread). `SensorEventWatch()`
  copies `startedInstances_` under the lock, then iterates/calls
  `ProcessSensorUpdateEvent()` unlocked (avoids deadlock if a
  `CurrentValueChanged` handler re-enters `Start()`/`Stop()`). No new tests
  possible (can't be exercised headless without real concurrent hardware
  events, per the task's own guidance) — verified via full `ctest`, same
  1970 tests, no regressions. Known residual gap (judged out of scope):
  per-instance `started_` still isn't synchronized, so a narrow
  dangling-pointer window remains if `Dispose()` runs concurrently with an
  in-flight event callback; full writeup in `plan_devices_phase3.md` Task
  P3-4's "Resolution" section.
- **Task P3-5 done (2026-07-03):** `VibrateController::Start()`/
  `Start(duration, intensity)` and `StartLeftRight()` now stop each other's
  SDL haptic effect before starting their own, instead of running on
  independent effect slots that could vibrate simultaneously. Added a
  shared private `DestroyLeftRightEffectIfAny()` helper (used by `Stop()`,
  `Start*`, and `StartLeftRight()`'s re-entry path); `StartLeftRight()`
  additionally calls `SDL_StopHapticRumble(g_haptic)` before uploading its
  effect (confirmed to exist in the vendored SDL3 by reading, not editing,
  `third_party/SDL/include/SDL3/SDL_haptic.h`, per that directory's own
  `CLAUDE.md`). 3 new sequence-safety tests (both call orders plus a
  repeated-alternation stress case) — 23 `VibrateControllerTests` total, up
  from 20. Full writeup: `plan_devices_phase3.md` Task P3-5's "Resolution"
  section.
- **Also added, 2026-07-03 (process change, not a code fix):** `CLAUDE.md`
  now has a "Git Commits" section codifying that a commit should follow
  immediately after each finished task rather than waiting for an explicit
  request, staging only that task's files by name.
- **Task P3-2 done (2026-07-03):** the user was explicitly asked to choose
  between the plan's two options and picked **option B** (the non-default,
  higher-cost one): the 5 reading types' `setXProperty()` methods
  (`AccelerometerReading`, `CompassReading`, `GyroscopeReading`,
  `AttitudeReading`, `MotionReading`) are now `private` + `friend class
  <OwningSensorClass>` (`Accelerometer`/`Compass`/`Gyroscope`/`Motion`/
  `Motion` respectively — `Motion` owns both `MotionReading` and
  `AttitudeReading`, since `AttitudeReading` is `MotionReading.Attitude`
  and there's no separate "AttitudeSensor" class), matching the real WP7
  API's `internal set`. `ISensorReading::getTimestampProperty()` stayed a
  public pure-virtual getter, untouched — the real interface never declared
  a setter. Every affected `*ReadingTests.cpp`'s direct `SetXxx` test cases
  (20 total) were removed and replaced with a one-line comment pointing at
  the pre-existing `ParameterizedConstructorStoresValues` test, since every
  reading type already had a full-field constructor that initializes fields
  directly (not through the now-private setters) — no test-only factory
  function was needed. Added a row to `CHECKLIST.md`'s deviations table
  documenting that C++ `friend` is narrower than C#'s assembly-scoped
  `internal`. Full writeup: `plan_devices_phase3.md` Task P3-2's
  "Resolution" section.
- **Tasks P3-6/P3-7/P3-8/P3-9 done (2026-07-03):** requested and implemented
  together since they touch the same 4 sensor test files. Added, per class:
  `CurrentValueChangedSubscriptionDoesNotThrow` (P3-6, all 4 —
  `Accelerometer`/`Gyroscope` exercise `Start()`/`Stop()` with a subscriber
  attached on the supported-hardware branch); `GetTypeName` (P3-7,
  `Compass`/`Gyroscope`/`Motion` — `Accelerometer` already had one);
  `CalibrateSubscriptionDoesNotThrow` (P3-8, `Compass`/`Motion` only, the
  only 2 classes with a `Calibrate` event); `DisposingOneOfTenAllowsAnotherConstruction`
  (P3-9, all 4 — proves `instanceCount_` decrements on `Dispose()`, not just
  that the 10-cap triggers). 13 new tests (1966 total, up from 1953). Full
  writeup: `plan_devices_phase3.md` Tasks P3-6/P3-7/P3-8/P3-9's shared
  "Resolution" section (under Task P3-9).
- **Task P3-10 done (2026-07-03):** added `GetHashCodeDifferentForUnequalInstances`
  to the 6 reading/event-args test files that have a `GetHashCode()` method
  (`AccelerometerReadingTests`, `CompassReadingTests`, `GyroscopeReadingTests`,
  `AttitudeReadingTests`, `MotionReadingTests`,
  `AccelerometerReadingEventArgsTests` — `CalibrationEventArgsTests` was
  found to have no `GetHashCode()` to test at all, an empty marker class).
  Each test varies every field, not just one, to make a hash collision
  vanishingly unlikely. 6 new tests, no collisions hit. 1972 tests total,
  up from 1966. Full writeup: `plan_devices_phase3.md` Task P3-10's
  "Resolution" section.
- **Task P3-11 done, Task P3-3 confirmed already-satisfied (2026-07-03):**
  P3-11's bundle — `StopAfterDisposeThrows` (all 4 sensor classes),
  `StartThenDisposeDoesNotCrash` (`Accelerometer`), a 2nd independently-varied
  inequality case each for `AttitudeReading`/`MotionReading`/`CompassReading`,
  negative-`ErrorId` round-trips for both exception test files,
  `VibrateController`'s `UnsupportedImpliesEmptyDeviceName` consistency
  test, and `StartLeftRight()` zero-magnitude/zero-duration boundary tests.
  **One correction to the plan's own premise:** the "extend `ToString()` to
  cover every field" sub-item turned out not to apply — reading the actual
  `AttitudeReading`/`MotionReading` `.cpp` files showed their `ToString()`
  format strings only ever included a subset of fields in the first place
  (`Pitch`/`Roll`/`Yaw`; `DeviceAcceleration`/`Gravity`), so the existing
  tests already covered everything actually in the output; no change made.
  13 new tests (1985 total, up from 1972). Task P3-3 needed no change either
  — `AUDIT.md`'s `AccelerometerFailedException` row already had the
  "unverified" caveat dropped from an earlier task. **With this,
  `plan_devices_phase3.md` is done except Task P3-12** (low priority). Full
  writeup: `plan_devices_phase3.md` Tasks P3-11/P3-3's "Resolution" sections.
- **Task P3-12 partially resolved (2026-07-03, via a research-only fork
  agent — no code touched):** `CalibrationEventArgs` **confirmed correct**
  — its real class page (MSDN `hh220788`, vs.110, found via the
  `Microsoft.Devices.Sensors` namespace listing page `ff403003`) shows
  exactly one constructor (parameterless) and no class-specific members,
  matching CNA's existing empty-marker-class implementation exactly.
  `SensorFailedException`'s exact constructor signature **stays an
  educated guess** — its class page (`hh239255`) has no Constructors
  section in either the `vs.110` or `vs.105` doc-family generation, nor
  does its subclass `AccelerometerFailedException`'s page (`ff628070`);
  consistent absence across 2 generations and 2 classes in the inheritance
  chain points to a systematic archival gap in this doc set's exception
  Constructors tables, not evidence the constructors don't exist (an
  exception with zero public constructors couldn't be thrown, and WP7
  tutorials do throw it). Not pursued further — low priority, no evidence
  of an actual bug, and CNA's `(message, errorId)` shape is the only
  sensible one given the confirmed get-only `ErrorId` property (`hh239104`).
  `AUDIT.md`'s `CalibrationEventArgs` row and `plan_devices_phase3.md` Task
  P3-12 updated with full source links. **This was the last open item in
  `plan_devices_phase3.md` — the plan has no further actionable work.**
- `plan_devices_phase4.md` **created (2026-07-03)** — user-authored
  hardening plan, 14 tasks across 8 phases. Two SDL3 facts confirmed during
  research that changed the shape of 2 tasks: SDL3 already ref-counts
  `SDL_InitSubSystem`/`SDL_QuitSubSystem` internally (Task P4-8's real fix
  is to stop CNA's `EnsureSensorSubsystemInitialized()` from bypassing that
  via an `SDL_WasInit()` guard, not build a second counter); SDL3 has
  `SDL_OpenHapticFromJoystick()` for direct ID-based haptic↔gamepad
  correlation (Task P4-10). Also found a genuinely new bug during research
  (not carried over from phase3): `Accelerometer`/`Gyroscope`'s `Timestamp`
  is built from `SDL_GetTicksNS()` (monotonic) fed into a `DateTime(ticks)`
  constructor expecting .NET-epoch ticks — Task P4-7.
- **Task P4-1 done (2026-07-03):** doc cleanup — checked
  `plan_devices_phase2.md`/`plan_devices_phase3.md` first and found neither
  actually stale (every task's original text is intentionally preserved
  with a Resolution appended, not overwritten). Narrowed to: an
  unambiguous "baseline complete" statement at the top of this file's
  Section 1; Section 5 restructured with a "Genuinely open work, grouped"
  block (5 named categories, each pointing at its `plan_devices_phase4.md`
  task); a closing pointer added to `plan_devices_phase3.md`.
- **Task P4-2 done (2026-07-03):** closed the `Accelerometer`/`Gyroscope`
  callback-lifetime use-after-free window Task P3-4 explicitly left open.
  Added a per-instance `inFlightCallback_` flag + shared
  `std::condition_variable`: `SensorEventWatch()` marks an instance
  in-flight before calling out to it unlocked, clears it after; `Dispose()`
  (after `Stop()` already removed the instance from `startedInstances_`)
  waits for any in-flight callback on this instance to finish before
  letting the object's lifetime end. Also split `ProcessSensorUpdateEvent()`
  into hardware-validity guards + a new `DispatchSensorReading()` doing the
  actual conversion+dispatch, and added 2 `NOXNA` test-only hooks to both
  classes — `InjectSyntheticSensorUpdate()` and `SetStartedForTesting()` —
  enabling Tasks P4-3 through P4-6's real event-path tests (not yet
  written). One known, documented, accepted limitation: a handler that
  reentrantly `Dispose()`s its own sender from within its own callback
  would deadlock — not solved, judged out of scope. Verified: `CNA` +
  `CnaTests` build clean, no behavior change to any existing public API.
  Full `ctest` — 1985 tests, same 64 pre-existing headless failures, zero
  regressions (no new tests from this task itself).
- **Tasks P4-3 through P4-6 done (2026-07-03):** real event-path tests
  using P4-2's synthetic hooks — `Accelerometer`/`Gyroscope`'s
  `CurrentValueChangedReceivesExpectedReading` (P4-3/P4-5), `Accelerometer`'s
  `ReadingChangedReceivesMatchingXYZ` (P4-4), and
  `StopPreventsSubsequentSyntheticEventFromDispatching` on both classes
  (P4-6). These are the first tests in this codebase to ever observe
  `CurrentValueChanged`/`ReadingChanged` actually *fire* with real data —
  every prior subscription test only confirmed subscribing doesn't crash.
  2 deliberate scope trims, both documented in
  `plan_devices_phase4.md`'s per-task Resolution: `getCurrentValueProperty()`
  is not asserted in any of these (it independently throws on this
  genuinely-unsupported machine per Task P3-1, orthogonal to what these
  tests verify), and P4-4 doesn't assert `Timestamp` (built from
  `SDL_GetTicksNS()`, which Task P4-7 — next — is about to replace; locking
  a test to the soon-to-change mechanism would need immediate rewriting).
  5 new tests (1990 total, up from 1985). Full writeup:
  `plan_devices_phase4.md` Tasks P4-3–P4-6's Resolution sections.
- **Task P4-7 done (2026-07-03):** fixed the confirmed `Timestamp` bug —
  `Accelerometer`/`Gyroscope` readings now get `Timestamp` from
  `System::DateTimeOffset::getUtcNowProperty()` (real wall-clock time)
  instead of the previous `SDL_GetTicksNS()`-derived value that always
  landed near `0001-01-01`. Went further than the minimal fix: since
  `timestampNs` had no other use once the old conversion was gone, it was
  removed entirely from the whole call chain
  (`SensorEventWatch()`/`ProcessSensorUpdateEvent()`/`DispatchSensorReading()`/
  `InjectSyntheticSensorUpdate()`) rather than left as a dead parameter —
  this required updating the 5 Task P4-3–P4-6 test call sites to drop the
  now-removed argument. Added 2 new tests (one per class),
  `CurrentValueChangedReceivesWallClockTimestamp`, asserting the received
  `Timestamp` falls between two `getUtcNowProperty()` samples taken
  immediately around the injection call. 1992 tests total, up from 1990.
  `AUDIT.md`'s `AccelerometerReading`/`GyroscopeReading` rows updated. Full
  writeup: `plan_devices_phase4.md` Task P4-7's Resolution section.
- Last pushed commit: `60f476b` on `feature/devices` (the
  `plan_devices_phase4.md` creation commit). Tasks P4-1 (`f291187`), P4-2
  (`3965e57`), and P4-3–P4-6 (`43eec74`) are committed locally but **not
  yet pushed**. Task P4-7's changes (`Accelerometer.hpp`/`.cpp`,
  `Gyroscope.hpp`/`.cpp`, `AccelerometerTests.cpp`, `GyroscopeTests.cpp`,
  `AUDIT.md`, `plan_devices_phase4.md`, this file) are **not yet
  committed** as of this writing.

---

## 4. Current blocker / main problem

No blocker prevents work from continuing — build and tests are green. All
three real bugs `plan_devices_phase3.md`'s research pass found are now
fixed, and the plan itself has no further actionable work (Task P3-12's
research is as complete as it can get without a source that doesn't exist
in any archive found so far — see Section 3):

**Problem 1 — fixed 2026-07-03 (Task P3-4):** `Accelerometer`/`Gyroscope`'s
shared static sensor state (`startedInstances_`, `g_sensor_`, `g_sensorId_`,
`eventWatchRegistered_`) is now guarded by a `static std::mutex` against the
SDL event-watch callback (SDL's own `SDL_AddEventWatch()` doc warns it "may
run in a different thread"). See Section 3 for the fix summary and
`plan_devices_phase3.md` Task P3-4 for the full resolution writeup,
including a known narrow residual gap (per-instance `started_` still
unsynchronized) that was judged out of scope for this task.

**Problem 2 — fixed 2026-07-03 (Task P3-1):** `SensorBase<T>::getCurrentValueProperty()`
now throws `System::InvalidOperationException` when the sensor isn't
supported, matching the documented WP7 behavior (MSDN `hh239261`). See
Section 3 for the fix summary and `plan_devices_phase3.md` Task P3-1 for
the full resolution writeup.

**Problem 3 — fixed 2026-07-03 (Task P3-5):** `VibrateController::Start()`/
`Start(duration, intensity)` and `StartLeftRight()` now stop each other's
SDL haptic effect before starting their own — previously independent effect
slots (`SDL_InitHapticRumble`'s internal `haptic->rumble_id` vs. this
codebase's own `g_leftRightEffectId`) that could run simultaneously. See
Section 3 for the fix summary and `plan_devices_phase3.md` Task P3-5 for
the full resolution writeup.

**What has been tried:** all three problems are fixed (Tasks P3-4, P3-1,
P3-5, all 2026-07-03).

---

## 5. Known bugs and limitations

**Genuinely open work, grouped (all tracked in `plan_devices_phase4.md`):**

- **Fixed 2026-07-03:** event-callback lifetime safety.
  `Accelerometer`/`Gyroscope`'s SDL event-watch callback could (in
  principle, on platforms where SDL delivers sensor events off-thread) call
  `ProcessSensorUpdateEvent()` on an instance the main thread was
  concurrently disposing — a narrow use-after-free window, documented and
  deliberately left open by `plan_devices_phase3.md` Task P3-4. Closed via
  a per-instance in-flight-callback flag `Dispose()` now waits on. See
  `plan_devices_phase4.md` Task P4-2 (done) — including one documented,
  accepted residual limitation (a handler reentrantly disposing its own
  sender would deadlock).
- **Fixed 2026-07-03:** real event-path test coverage.
  `CurrentValueChanged`/`ReadingChanged` are now observed actually *firing*
  with real data, via Task P4-2's synthetic-injection hooks — see
  `plan_devices_phase4.md` Tasks P4-3 through P4-6 (all done).
- **Fixed 2026-07-03:** timestamp correctness. `Accelerometer`/`Gyroscope`
  readings' `Timestamp` now comes from `System::DateTimeOffset::getUtcNowProperty()`
  (real wall-clock time); previously built from `SDL_GetTicksNS()`
  (monotonic nanoseconds since SDL init) fed into a `DateTime(ticks)`
  constructor that expects ticks since the .NET epoch — the resulting
  value used to be nowhere near the actual wall-clock time the reading
  occurred. See `plan_devices_phase4.md` Task P4-7 (done).
- **Hardware-in-the-loop verification.** Nothing in this codebase — sensor
  axis conventions, actual vibration motor behavior, the gamepad-exclusion
  filter — has ever run against real accelerometer/gyroscope/haptic
  hardware. See `plan_devices_phase4.md` Tasks P4-13/P4-14.
- **Android/iOS cross-compilation.** No NDK/toolchain available in this dev
  container (`plan_devices_phase2.md` Task P2-7, blocked). See
  `plan_devices_phase4.md` Tasks P4-11/P4-12.

**Resolved (historical record, `plan_devices_phase3.md`, all 2026-07-03):**

- **Fixed 2026-07-03:** thread-safety race in `Accelerometer`/`Gyroscope`'s
  shared sensor state vs. the SDL event-watch callback, via a
  `static std::mutex`. See Section 4, Problem 1 / Task P3-4 (done). A
  narrow residual gap (per-instance `started_` still unsynchronized) was
  judged out of scope — see the task's "Resolution" section for detail.
- **Fixed 2026-07-03:** `SensorBase<T>.CurrentValue` now throws
  `InvalidOperationException` when unsupported. See Section 4, Problem 2 /
  Task P3-1 (done).
- **Fixed 2026-07-03:** `VibrateController`'s `Start()`/`StartLeftRight()`
  now cancel each other's SDL effects before starting. See Section 4,
  Problem 3 / Task P3-5 (done).
- **Fixed 2026-07-03:** the 5 reading types (`AccelerometerReading`,
  `CompassReading`, `GyroscopeReading`, `AttitudeReading`, `MotionReading`)
  previously had fully public `setXProperty()` methods; the real WP7 API
  has `internal set` on these. User picked the higher-cost fix (private +
  `friend class <OwningSensorClass>`) over the zero-code-change
  documentation default when asked. See Section 3 and `plan_devices_phase3.md`
  Task P3-2 (done) for the full writeup, including the 20 removed direct
  `SetXxx` tests (replaced by existing constructor-based coverage) and the
  new `CHECKLIST.md` deviations-table row on `friend` vs. `internal` scope.
- **Fixed 2026-07-03:** all test-coverage gaps identified by
  `plan_devices_phase3.md`'s research pass are filled (Tasks P3-6 through
  P3-11 — `CurrentValueChanged`/`Calibrate` subscription tests,
  `GetTypeName()` on `Compass`/`Gyroscope`/`Motion`, dispose-decrement
  verification, `GetHashCode()` different-hash cases, and the smaller
  bundle in P3-11). See Section 3 for details.
- **By design, not a bug:** `Compass` and `Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform.
- **Superseded by `plan_devices_phase4.md` Task P4-10 (not yet fixed):**
  `VibrateController`'s gamepad-exclusion filter matches by device name;
  two physically distinct controllers reporting an identical product name
  would both be excluded/included together. SDL3's
  `SDL_OpenHapticFromJoystick()` gives a real ID-based correlation instead
  of name-matching — confirmed to exist, not yet implemented.
- **Confirmed 2026-07-03:** `CalibrationEventArgs`'s empty-marker-class
  implementation matches the real class exactly (MSDN `hh220788`).
- **Unverified (low priority, no evidence of an actual bug, genuinely
  researched twice and not resolvable further):** `SensorFailedException`'s
  real constructor overload list — the `(message, errorId)` overload added
  in `plan_devices_phase2.md` Task P2-16 remains an educated guess.
  `SensorFailedException`'s and `AccelerometerFailedException`'s doc pages
  both lack a Constructors table in every doc-family generation checked,
  consistent with a systematic archival gap rather than the constructors
  not existing. See `plan_devices_phase3.md` Task P3-12.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/   ← XNA WP7 sensor API headers
src/Microsoft/Devices/Sensors/       ← sensor implementations (SDL3-backed)
tests/Microsoft/Devices/Sensors/     ← Google Test suites per class
include/Microsoft/Devices/           ← VibrateController.hpp
src/Microsoft/Devices/               ← VibrateController.cpp
tests/Microsoft/Devices/             ← VibrateControllerTests.cpp
```

**`SensorBase<T>`** (header-only template) owns `CurrentValue`,
`IsDataValid`, `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`, and
(since Task P3-1, 2026-07-03) an internal `isSupported_` flag that gates
`getCurrentValueProperty()`'s `InvalidOperationException`. Concrete sensors
override `Start()`, `Stop()`, and `Dispose(bool)`, and must call
`setIsSupportedProperty()` once from their constructor. **Do not restructure
this class further** — stable, used by production code.

**Invariant:** any class overriding `Dispose(bool)` **must** add `using
SensorBase<T>::Dispose;`, or C++ name-hiding silently breaks the inherited
public no-arg `Dispose()`. This bug has already been found and fixed 4
times (`Accelerometer`, `Compass`, `Gyroscope`, `Motion`) — don't
reintroduce it in any new sensor class.

**Sensor pattern (real, SDL3-backed — `Accelerometer`/`Gyroscope`):** static
`g_sensor_`/`g_sensorId_` hold the single open SDL sensor handle; static
`instanceCount_` enforces a ≤10 simultaneous-instance limit; static
`eventWatchRegistered_` guards the SDL event filter lifecycle; static
`startedInstances_` is the list the event-watch callback iterates. **This
static state is now guarded by a `static std::mutex mutex_`** (fixed
2026-07-03, Task P3-4 — see Section 4, Problem 1) against the SDL
event-watch callback potentially running off-thread; a narrow residual gap
(per-instance `started_` still unsynchronized) remains, documented in the
task's resolution rather than fixed, since closing it fully needs
ownership-safety (`shared_ptr`/`weak_ptr`) beyond this task's scope.
`Start()` opens the sensor and registers the SDL event watch; `Stop()`
unregisters; `Dispose(bool)` stops, decrements the counter, and closes the
sensor handle when the last instance is disposed. `ProcessSensorUpdateEvent()`
runs from the SDL event filter on every `SDL_EVENT_SENSOR_UPDATE`, with an
Android-specific landscape axis remap (duplicated per-class, not shared,
never build-verified).

**Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`;
`Start()` always throws `SensorFailedException`; still expose the
`Calibrate` event for API completeness even though it's never raised.

**`VibrateController`:** singleton (private default constructor, reached
via `getDefaultProperty()`), no `SensorBase<T>`, no `IDisposable`, lives
directly in `Microsoft::Devices` (not `::Sensors`). Drives SDL3's haptic
API directly. Excludes haptic devices that are also connected
joysticks/gamepads from device selection. Its plain `Start()` rumble effect
and its `StartLeftRight()` dual-motor effect are independent SDL effect
slots; **fixed 2026-07-03 (Task P3-5)** so each now stops the other's
effect before starting its own, via a shared `DestroyLeftRightEffectIfAny()`
helper and `SDL_StopHapticRumble()` — see Section 4, Problem 3.

**`GetTypeName()` invariant:** must return `.`-separated fully-qualified
.NET names (e.g. `"Microsoft.Devices.Sensors.Compass"`), tagged `NOXNA`.
Classes deriving `System::Object` (via `SensorBase<T>`) use the
`GetTypeNameHPP()`/`GetTypeNameCPP(Class, "Name")` macro pair; classes that
don't (e.g. `AccelerometerReading`-style value types) declare a plain
`NOXNA std::string GetTypeName() const;` method instead.

**Boundaries — do not cross:**
- `third_party/SDL` is vendored and has its **own `CLAUDE.md` forbidding
  AI-authored code contributions** to that project. Safe to *read* for
  research (this is how every SDL-behavior claim in this document and in
  `plan_devices_phase3.md` was verified), never edit.
- `sharp-runtime` is a sibling repo under separate, concurrent development —
  its public API can change without notice mid-session (already happened
  once this session, see Section 3's incidental fix). If a build breaks in
  a file this plan didn't touch, check there first before assuming you
  broke it.
- Do not expand `Microsoft::Devices` scope to camera, radio, or
  phone-call/photo-picker APIs — explicitly out of scope (not sensor or
  vibration functionality).
- Do not implement sensor fusion in `Motion` — it stays a `NotSupported`
  stub until SDL3 itself gains magnetometer access.

---

## 7. Useful commands

```bash
# Configure (only needed once, or if CMakeCache.txt is stale/points elsewhere):
cmake -S /rv/data/development/github.com/openeggbert/cna_devices \
      -B /rv/data/development/github.com/openeggbert/cna_devices/cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build library:
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build tests:
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Run only Devices/Sensors + VibrateController tests:
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController"

# Run one suite directly:
./cmake-build-debug/CnaTests --gtest_filter="GyroscopeTests*"

# Cross-platform build verification (Vulkan/BGFX; BGFX's configure step
# fetches bgfx.cmake from GitHub — takes several minutes; both already exist
# in this checkout under cmake-build-vulkan/cmake-build-bgfx):
cmake --build cmake-build-vulkan --target CNA --target CnaTests -j$(nproc)
cmake --build cmake-build-bgfx   --target CNA --target CnaTests -j$(nproc)
```

No dedicated lint/format tooling is configured for this project as of this
writing.

---

## 8. Next smallest tasks

`plan_devices_phase3.md` has no further actionable work (see Section 3 —
Task P3-12 is as resolved as it can get without a source that doesn't
appear to exist in any archive). `plan_devices_phase4.md` is now open
(user-authored hardening plan, 14 tasks across 8 phases). Tasks P4-1
through P4-7 are done as of this edit. Recommended order for the rest:

1. **Task P4-8 — SDL sensor subsystem ownership.** Real, root-caused bug:
   `EnsureSensorSubsystemInitialized()` bypasses SDL3's own built-in
   subsystem ref-counting via an `SDL_WasInit()` guard.

2. **Tasks P4-9/P4-10 — `VibrateController` hardening** (mutex around
   `g_haptic`/`g_leftRightEffectId`; replace name-matching gamepad
   exclusion with `SDL_OpenHapticFromJoystick()`).

3. **Tasks P4-11–P4-14 — cross-platform build and demo screen**, likely
   still blocked for Android/iOS in this environment (re-check first); the
   demo screen (P4-14) has no environment blocker and can be done any time.

---

## 9. Do not do yet

- Tasks P3-6 through P3-11 are all done (2026-07-03). Do not re-add a
  `ToString()` field-coverage test for `AttitudeReading`/`MotionReading`
  expecting `Quaternion`/`RotationMatrix`/`Attitude`/`DeviceRotationRate`
  substrings — those fields are genuinely not part of either class's
  `ToString()` format string (verified by reading the `.cpp`, not assumed);
  such a test would just fail.
- Task P3-2 is done (2026-07-03, private + friend fix, option B). Do not
  re-open this or add back the removed direct `SetXxx` tests — they were
  intentionally removed because the methods they tested are no longer
  public API; the existing `ParameterizedConstructorStoresValues` tests
  cover the same field-storage behavior.
- Task P3-4 is done (2026-07-03, mutex-based fix). Do not attempt to add a
  synthetic concurrency/thread test for it retroactively — it can't
  meaningfully exercise the real race without actual concurrent hardware
  events; the existing full-suite pass is the only verification this
  environment can give.
- Do not refactor or restructure `SensorBase<T>` or `ISensorReading` further
  — stable, used by production code (Task P3-1's `isSupported_` addition is
  already done, 2026-07-03).
- Do not expand `Microsoft::Devices` to camera, radio, or phone-hardware
  APIs (`PhotoCamera`, `CameraButtons`, `PhotoChooserTask`, etc.) — not
  sensor/vibration functionality, explicitly out of scope.
- Do not implement real sensor fusion in `Motion` — keep it a
  `NotSupported` stub until SDL3 gains magnetometer access.
- Do not edit anything under `third_party/SDL` — vendored, has its own
  `CLAUDE.md` forbidding AI-authored contributions; read-only for research.
- Do not attempt Android/iOS cross-compilation in this environment — no
  NDK/toolchain is available; that verification needs a different
  environment or CI.
- Do not perform the cross-cutting `GetTypeNameCPP` dot/colon cleanup
  outside `Microsoft::Devices` (`Cue.cpp`, `AudioEngine.cpp`, etc.) — a
  separate, larger, unrelated cleanup outside this plan's scope.
- Do not run `cmake --build` without first checking `CMakeCache.txt` points
  at the correct source directory (this repo has hit stale-cache issues
  before).

---

## 10. Resume prompt

```
Read NEXT.md first.
Then inspect only the files listed for the first task in section 8.
Do not refactor unrelated code.
Make one small, verified improvement (implement or test one class at a time).
Run the relevant build/test command from section 7 after each change.
Update NEXT.md after finishing.
```
