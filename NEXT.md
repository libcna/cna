# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS.

**Main goal (current phase):** the `Microsoft::Devices` namespace —
`Microsoft::Devices::Sensors` (Accelerometer, Compass, Gyroscope, Motion, and
their reading/event-args/exception types) plus
`Microsoft::Devices::VibrateController` — is functionally complete against
the documented Windows Phone 7 XNA API spec. Branch: `feature/devices`.
Two plans are fully closed (`plan_devices.md`, `plan_devices_phase2.md`,
except one environment-blocked task); a third, `plan_devices_phase3.md`, is
open with concrete follow-up work (real bugs + test-coverage gaps found by a
deeper research pass) and **nothing in it has been started yet**.

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
`44ad496` + uncommitted Task P3-1 work). Also verified clean under `VULKAN`
(`cmake-build-vulkan`) and `BGFX` (`cmake-build-bgfx`) as of 2026-07-02 —
the graphics backend choice has zero effect on `Microsoft::Devices::*`
compilation, confirmed empirically. (Vulkan/BGFX not re-verified after
Task P3-1 since it only touches `Microsoft::Devices::Sensors` headers/cpp,
same reasoning as before.)

**Tests:** last full `ctest` run (`EASYGL`): **1970 tests total, 97%
passing.** The only failures are a fixed set of **64 pre-existing
`EasyGL_*` graphics tests** that cannot run headless (no display/GPU in
this dev environment) — present before this phase began, unrelated to
`Microsoft::Devices`. No regressions have been introduced. Under `VULKAN`/
`BGFX`, the targeted Devices/Sensors/VibrateController suite was **139/139**
passing on both as of 2026-07-02 (not re-run after Task P3-1); full-suite
counts differ from `EASYGL` only because backend-specific demo/smoke-test
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
- Two confirmed real bugs from the newest research pass are **not yet
  fixed** (tracked in `plan_devices_phase3.md`; a third, Task P3-1, was
  fixed 2026-07-03): see Section 4.

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
- Last pushed commit: `44ad496` on `feature/devices`. Task P3-1's changes
  (`SensorBase.hpp`, the 4 sensor `.cpp` constructors, 4 test files,
  `AUDIT.md`, `plan_devices_phase3.md`, this file) are **not yet committed**
  as of this writing.

---

## 4. Current blocker / main problem

No blocker prevents work from continuing — build and tests are green. The
most important known problems, found by `plan_devices_phase3.md`'s research
pass:

**Problem 1 (highest severity, not yet fixed):** `Accelerometer`/
`Gyroscope`'s shared static sensor state (`startedInstances_`, `g_sensor_`,
`g_sensorId_`, `eventWatchRegistered_` in both `.cpp` files) is read/written
from `Start()`/`Stop()`/`Dispose()` (application thread) AND from the
`SensorEventWatch` SDL event-filter callback, with **zero
synchronization**. SDL's own header doc for `SDL_AddEventWatch()`
(`third_party/SDL/include/SDL3/SDL_events.h`) explicitly warns the callback
"may run in a different thread." No failing test exists for this (can't be
unit-tested headless without real concurrent hardware events) — this is a
latent bug, not a currently-observed crash. Affected files:
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`. See `plan_devices_phase3.md`
Task P3-4 for the full writeup and suggested fix (mutex guard).

**Problem 2 — fixed 2026-07-03 (Task P3-1):** `SensorBase<T>::getCurrentValueProperty()`
now throws `System::InvalidOperationException` when the sensor isn't
supported, matching the documented WP7 behavior (MSDN `hh239261`). See
Section 3 for the fix summary and `plan_devices_phase3.md` Task P3-1 for
the full resolution writeup.

**Problem 3 (not yet fixed):** `VibrateController::Start()`/
`Start(duration, intensity)` and `StartLeftRight()` use independent SDL
haptic effect slots and don't stop each other — confirmed by reading
`third_party/SDL/src/haptic/SDL_haptic.c` (`SDL_InitHapticRumble` allocates
its own `haptic->rumble_id`, separate from this codebase's
`g_leftRightEffectId`). Calling both without an intervening `Stop()` can
physically vibrate both effects at once. See `plan_devices_phase3.md` Task
P3-5.

**What has been tried:** Problem 2 is fixed (Task P3-1, 2026-07-03).
Problems 1 and 3 haven't been attempted yet — they were found by
research/code review this session, not previously attempted fixes.

---

## 5. Known bugs and limitations

- **Confirmed bug, not yet fixed:** thread-safety race in
  `Accelerometer`/`Gyroscope`'s shared sensor state vs. the SDL event-watch
  callback. See Section 4, Problem 1 / `plan_devices_phase3.md` Task P3-4.
- **Fixed 2026-07-03:** `SensorBase<T>.CurrentValue` now throws
  `InvalidOperationException` when unsupported. See Section 4, Problem 2 /
  Task P3-1 (done).
- **Confirmed design gap, not yet fixed:** `VibrateController`'s
  `Start()`/`StartLeftRight()` don't cancel each other's SDL effects. See
  Section 4, Problem 3 / Task P3-5.
- **Needs a decision, not a forced fix:** the 5 reading types
  (`AccelerometerReading`, `CompassReading`, `GyroscopeReading`,
  `AttitudeReading`, `MotionReading`) have fully public `setXProperty()`
  methods; the real WP7 API has `internal set` on these (confirmed for two
  of the five via direct MSDN pages, high confidence the pattern holds for
  all five). Fixing this properly (private + `friend`) would break every
  existing test that calls `setXProperty()` directly — recommended
  resolution is documenting it as an accepted C++ deviation (`CHECKLIST.md`
  already has precedent for this class of C#-vs-C++ visibility gap), not a
  code change. See `plan_devices_phase3.md` Task P3-2.
- **Incomplete (test coverage):** zero test coverage of
  `CurrentValueChanged` (the primary, non-deprecated event) on all 4 sensor
  classes; `Compass`/`Gyroscope`/`Motion` are missing `GetTypeName()`
  tests entirely; `Calibrate` event untested on `Compass`/`Motion`;
  "dispose one of 10, an 11th now succeeds" never verified on any of the 4
  sensor classes; `GetHashCode()` never tests the different-hash case
  anywhere in the namespace; several smaller boundary gaps (negative
  `ErrorId`, `StartLeftRight` zero-duration/zero-magnitude boundaries,
  multi-field inequality/`ToString` coverage on `AttitudeReading`/
  `MotionReading`). Full list: `plan_devices_phase3.md` Tasks P3-6–P3-11.
- **By design, not a bug:** `Compass` and `Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform.
- **Accepted limitation:** `VibrateController`'s gamepad-exclusion filter
  matches by device name; two physically distinct controllers reporting an
  identical product name would both be excluded/included together.
- **Needs verification:** Android/iOS cross-compilation — no NDK/toolchain
  available in this dev container (`plan_devices_phase2.md` Task P2-7,
  blocked). `Accelerometer.cpp`/`Gyroscope.cpp`'s `#ifdef __ANDROID__`
  branches have never been compiled by any compiler.
- **Unverified (low priority, no evidence of an actual bug):**
  `SensorFailedException`'s real constructor overload list (the
  `(message, errorId)` overload added in `plan_devices_phase2.md` Task
  P2-16 is an educated guess, no direct doc page confirms it) and
  `CalibrationEventArgs`'s exact member list (no direct class page found;
  current empty-marker-class implementation is unconfirmed either way). See
  `plan_devices_phase3.md` Task P3-12.

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
static state has a known, unfixed thread-safety gap — see Section 4,
Problem 1.** `Start()` opens the sensor and registers the SDL event watch;
`Stop()` unregisters; `Dispose(bool)` stops, decrements the counter, and
closes the sensor handle when the last instance is disposed.
`ProcessSensorUpdateEvent()` runs from the SDL event filter on every
`SDL_EVENT_SENSOR_UPDATE`, with an Android-specific landscape axis remap
(duplicated per-class, not shared, never build-verified).

**Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`;
`Start()` always throws `SensorFailedException`; still expose the
`Calibrate` event for API completeness even though it's never raised.

**`VibrateController`:** singleton (private default constructor, reached
via `getDefaultProperty()`), no `SensorBase<T>`, no `IDisposable`, lives
directly in `Microsoft::Devices` (not `::Sensors`). Drives SDL3's haptic
API directly. Excludes haptic devices that are also connected
joysticks/gamepads from device selection. **Known unfixed gap:** its plain
`Start()` rumble effect and its `StartLeftRight()` dual-motor effect are
independent SDL effect slots that don't cancel each other — see Section 4,
Problem 3.

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

Full detail for all of these is in `plan_devices_phase3.md`; this is the
recommended order. (Task P3-1 was completed 2026-07-03 — see Section 3.)

1. **Task P3-4 — Guard `Accelerometer`/`Gyroscope`'s shared sensor state**
   - Goal: add a `std::mutex` around `startedInstances_`/`g_sensor_`/
     `g_sensorId_`/`eventWatchRegistered_` in both classes; lock around
     `Start()`/`Stop()`/`Dispose(bool)` and the `SensorEventWatch`
     callback's iteration (copy out the instance pointer(s) while locked,
     call `ProcessSensorUpdateEvent()` unlocked to avoid holding the lock
     across a callout).
   - Files: `src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
     `src/Microsoft/Devices/Sensors/Gyroscope.cpp`.
   - Verify: full `ctest --output-on-failure` (no new test possible — this
     can't be exercised headless without real concurrent hardware events;
     confirm existing suites still pass, don't invent a synthetic
     concurrency test that wouldn't exercise the real race).

2. **Task P3-5 — Make `VibrateController::Start()`/`StartLeftRight()` mutually exclusive**
   - Goal: each `Start*` variant should stop the other's active SDL effect
     before starting its own.
   - Files: `src/Microsoft/Devices/VibrateController.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="VibrateControllerTests*"`.

3. **Task P3-2 — Decide on the reading-type public-setter visibility gap**
   - Goal: this is a decision, not a mechanical fix — read
     `plan_devices_phase3.md` Task P3-2 in full and either (a) document the
     public-setter-vs-`internal set` mismatch as an accepted C++ deviation
     in `CHECKLIST.md`'s deviations table (recommended, zero code change),
     or (b) commit to the private+`friend`+test-rewrite path.
   - Files: `CHECKLIST.md` (if option A) or all 5 reading types + their
     test files (if option B).
   - Verify: full `ctest --output-on-failure` if option B; no build/test
     needed if option A.

4. **Task P3-6/P3-7/P3-9 — Fill the highest-value test-coverage gaps**
   - Goal: add `CurrentValueChanged` subscription tests (all 4 sensor
     classes), `GetTypeName()` tests (`Compass`/`Gyroscope`/`Motion` —
     `Accelerometer` already has one), and dispose-then-11th-succeeds tests
     (all 4 sensor classes). See `plan_devices_phase3.md` for exact test
     shapes to mirror.
   - Files: `tests/Microsoft/Devices/Sensors/*.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="AccelerometerTests*:CompassTests*:GyroscopeTests*:MotionTests*"`.

Remaining smaller items (P3-3, P3-8, P3-10, P3-11, P3-12) are lower
priority — see `plan_devices_phase3.md` directly when ready for those.

---

## 9. Do not do yet

- Do not fix Task P3-2 (reading-type setter visibility) by silently making
  setters private — it will break existing tests; it needs the explicit
  decision described in Task P3-2 above first.
- Do not invent a synthetic concurrency/thread test for Task P3-4 — it
  can't meaningfully exercise the real race without actual concurrent
  hardware events; confirm existing suites still pass instead.
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
