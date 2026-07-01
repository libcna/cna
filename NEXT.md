# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++ reimplementation of the XNA 4.0 programming model built on SDL3
and a pluggable graphics backend. The namespace is `Microsoft::Xna::Framework`
(and `Microsoft::Devices` for sensors/vibration). It targets desktop Linux,
Android, and iOS.

**Current phase:** `feature/devices` — implementing the full
`Microsoft::Devices::Sensors` namespace (Accelerometer, Compass, Gyroscope,
Motion, VibrateController) according to the XNA / Windows Phone 7 API spec.
Plan: `plan_devices.md` (31 tasks) — **all 31 tasks are now complete.** See
Section 5 for known gaps found along the way that are worth a follow-up phase
(they were out of `plan_devices.md`'s scope, so not fixed here).

**Key architectural rules:**
- Public API names must match XNA 4.0 exactly.
- C# properties → `getXProperty()` / `setXProperty()`.
- Non-XNA extensions tagged `NOXNA`.
- Tests live under `tests/` mirroring the namespace path, using Google Test.
- Backend selection: compile-time via `CNA_GRAPHICS_BACKEND`.

---

## 2. Current status

**Build:** `CNA` library and `CnaTests` build cleanly with EASYGL backend
(`cmake-build-debug`).

**Tests:** 11/11 `AccelerometerReadingTests` + 3/3 `SensorFailedExceptionTests`
+ 4/4 `AccelerometerFailedExceptionTests` + 14/14
`AccelerometerReadingEventArgsTests` + 3/3 `CalibrationEventArgsTests` + 13/13
`CompassReadingTests` + 7/7 `CompassTests` + 11/11 `GyroscopeReadingTests` +
7/7 `GyroscopeTests` + 14/14 `AttitudeReadingTests` + 13/13
`MotionReadingTests` + 7/7 `MotionTests` + 6/6 `VibrateControllerTests` pass
(1935 total ctest cases). All previously existing tests continue to pass
(the only ctest failures are pre-existing `EasyGL_*` graphics tests that
can't run headless — no display/GPU in this environment — unrelated to this
work; failure count held steady at 64 throughout this entire phase).

**Working:**
- Full SDL3-backed `Accelerometer` implementation (Start/Stop/Dispose, event
  watch, Android axis remap).
- `AccelerometerReading` — complete: constructors, getters/setters, `operator==`,
  `operator!=`, `ToString()`, `GetHashCode()`, `GetTypeName()`.
- `SensorBase<T>`, `SensorReadingEventArgs<T>`, `ISensorReading`, `SensorState` —
  complete.
- `SensorFailedException` — complete: fixed default constructor to pass
  `"Sensor failed."` (was previously empty, contradicting its own "default
  message" doc comment and plan_devices.md Task 3's requirement that `what()`
  be non-empty); 3 tests added (default ctor, `const char*` ctor, catch as
  `System::Exception`).
- `AccelerometerFailedException` — complete: fixed default constructor to pass
  its own specific message `"Accelerometer failed."` (was delegating to
  `SensorFailedException()`'s empty message); 4 tests added (default ctor
  doesn't throw, `const char*` ctor, catch as `SensorFailedException`, catch as
  `System::Exception`).
- `AccelerometerReadingEventArgs` — complete: WP7 7.0 legacy event args
  (`X`/`Y`/`Z : double`, `Timestamp`), inherits `System::EventArgs`, not wired to
  the current `Accelerometer` (see class doc comment). 14 tests added covering
  both constructors, all getter/setter pairs, `operator==`/`operator!=`,
  `ToString()`, `GetHashCode()`, `GetTypeName()`, usable as `EventArgs&`.
- `CalibrationEventArgs` — complete: trivial empty `EventArgs` subclass for
  `Compass.Calibrate` / `Motion.Calibrate` (not yet wired to those classes,
  which don't exist yet). 3 tests added (default ctor, usable as `EventArgs&`,
  `GetTypeName()`).
- `CompassReading` — complete: `ISensorReading`-derived reading with
  `HeadingAccuracy`, `MagneticHeading`, `MagnetometerReading : Vector3`,
  `Timestamp` (override), `TrueHeading`; built directly on the
  `AccelerometerReading` pattern. 13 tests added covering both constructors,
  all getter/setter pairs, `operator==`/`!=`, `ToString()`, `GetHashCode()`,
  `GetTypeName()`.
- `Compass` — complete: `SensorBase<CompassReading>`-derived class mirroring
  the `Accelerometer` pattern. SDL3 has no magnetometer API on any platform, so
  `getIsSupportedProperty()` always returns `false`, `getStateProperty()` is
  always `NotSupported`, and `Start()` always throws `SensorFailedException`.
  Has the `Calibrate` event (`System::EventHandler<CalibrationEventArgs>`,
  currently never raised since the sensor is unsupported). 7 tests added per
  plan_devices.md Task 13 coverage list. Required a name-hiding fix (`using
  SensorBase<CompassReading>::Dispose;`) — see Section 5 for details and the
  matching bug found in `Accelerometer.hpp`.
- `GyroscopeReading` — complete: `ISensorReading`-derived reading with
  `RotationRate : Vector3` (radians/second per axis) and `Timestamp`
  (override); built directly on the `AccelerometerReading` pattern. 11 tests
  added covering both constructors, both getter/setter pairs,
  `operator==`/`!=`, `ToString()`, `GetHashCode()`, `GetTypeName()`.
- `Gyroscope` — complete: real SDL3-backed `SensorBase<GyroscopeReading>`
  implementation, mirrors `Accelerometer` exactly (same static
  `g_sensor_`/`g_sensorId_`/`instanceCount_`/`eventWatchRegistered_`/
  `startedInstances_` pattern, `SDL_SENSOR_GYRO` instead of `SDL_SENSOR_ACCEL`,
  no gravity normalization since SDL already reports rad/s, same
  Android landscape axis remap duplicated as
  `ConvertAndroidGyroscopeToXnaLandscape`). Unlike `Accelerometer`, failures
  throw plain `SensorFailedException` (no dedicated `GyroscopeFailedException`
  class — not requested by the plan). Includes the `using
  SensorBase<GyroscopeReading>::Dispose;` fix proactively (see Section 5).
  `GetTypeName()` uses the dot-separated convention. 7 tests added; they
  branch on the live `getIsSupportedProperty()` result so they pass both
  headless (no gyroscope hardware, as in this dev container) and on real
  hardware.
- `AttitudeReading` — complete: `ISensorReading`-derived reading with
  `Pitch`/`Roll`/`Yaw : float`, `Quaternion`, `RotationMatrix`, `Timestamp`
  (override). Default ctor sets `Quaternion::Identity` and
  `Matrix::getIdentityProperty()` explicitly (both types' own default
  constructors are all-zero, not identity, matching real XNA struct-default
  semantics — `Quaternion` has no default constructor at all). 14 tests added
  covering both constructors, all getter/setter pairs, `operator==`/`!=`,
  `ToString()`, `GetHashCode()`, `GetTypeName()`.
- `MotionReading` — complete: `ISensorReading`-derived fused reading composing
  `Attitude : AttitudeReading`, `DeviceAcceleration`/`DeviceRotationRate`/
  `Gravity : Vector3`, `Timestamp`. Reuses `AttitudeReading::operator==` and
  `GetHashCode()` directly. 13 tests added covering both constructors, all
  getter/setter pairs, `operator==`/`!=`, `ToString()`, `GetHashCode()`,
  `GetTypeName()`.
- `Motion` — complete: `SensorBase<MotionReading>`-derived class, same stub
  shape as `Compass` (Motion needs Accelerometer + Compass + Gyroscope; SDL3
  has no magnetometer, so Compass — and therefore Motion — is unsupported
  everywhere). `getIsSupportedProperty()` always `false`,
  `getStateProperty()` always `NotSupported`, `Start()` always throws
  `SensorFailedException("Motion is not supported on this platform.")`.
  Includes a `// TODO` marking where sensor fusion should be wired up once
  compass support exists, and the `using SensorBase<MotionReading>::Dispose;`
  fix proactively. Has the `Calibrate` event. 7 tests added per
  plan_devices.md Task 25 coverage list.
- `VibrateController` — complete: pure static utility class in
  `Microsoft::Devices` (NOT `Microsoft::Devices::Sensors`) —
  `VibrateController() = delete`, `static Start(TimeSpan)` / `static Stop()`,
  no `SensorBase<T>`/`IDisposable` involvement at all (confirmed from the
  plan: no instance state, no `SensorState`, no instance-count limit).
  `Start()` silently clamps duration to `[TimeSpan::Zero,
  TimeSpan::FromSeconds(5)]` (WP7-documented max) and drives SDL3's haptic
  API (`SDL_GetHaptics`/`SDL_OpenHaptic`/`SDL_InitHapticRumble`/
  `SDL_PlayHapticRumble`; `Stop()` uses `SDL_StopHapticEffects`). **Important
  finding:** `plan_devices.md`'s Task 27 spec named SDL2-era function names
  (`SDL_HapticRumbleInit`, `SDL_HapticStopAll`, `SDL_AndroidSendMessage`) that
  don't exist in the vendored SDL3 — verified the real names against
  `.sdl-prebuilt/install/include/SDL3/SDL_haptic.h` before implementing. Also
  found that **no `#ifdef __ANDROID__` branch is needed at all**: SDL3's
  vendored Android haptic backend
  (`third_party/SDL/src/haptic/android/SDL_syshaptic.c` +
  `SDLHapticHandler.java`) already auto-registers the phone's vibration motor
  via `Context.VIBRATOR_SERVICE` as a haptic device, so the same
  `SDL_PlayHapticRumble` call reaches `Vibrator.vibrate(ms)` on Android with
  no custom JNI/Java bridge code — this repo has no such bridge, and none was
  needed. 6 tests added per plan_devices.md Task 28 coverage list, all
  `EXPECT_NO_THROW` (headless dev container has no haptic hardware, so every
  call takes the silent-no-op path — by design, matches the test spec).
- **Task 29 (CMakeLists.txt update) needed no changes**: both
  `CNA_SOURCES`/`CNA_TEST_SOURCES` are collected via
  `file(GLOB_RECURSE ... CONFIGURE_DEPENDS "src/*.cpp" | "tests/*.cpp")` in
  the root `CMakeLists.txt` (lines ~156, ~1157) — every new file from this
  entire phase (Tasks 1–28) was picked up automatically on each reconfigure.
  No manual registration was ever required, despite the plan's literal
  "add new files to CMakeLists.txt" wording.
- **Tasks 30–31 (final build + test verification): done** — `CNA` and
  `CnaTests` build cleanly; full `ctest` run shows 1935 total tests, 97%
  pass, and the only failures are the same pre-existing 64 headless
  `EasyGL_*` graphics tests present since before this phase began.

**Not yet done:** Nothing from `plan_devices.md` — all 31 tasks complete. See
Section 5 for follow-up items discovered during this phase but out of the
plan's scope (not fixed here).

---

## 3. Recent changes

- `include/Microsoft/Devices/Sensors/AccelerometerReading.hpp` — added
  `operator==`, `operator!=`, `ToString()`, `GetHashCode()`, `GetTypeName()`,
  `#include "CNA/CNAHelper.hpp"`, `#include <string>`.
- `src/Microsoft/Devices/Sensors/AccelerometerReading.cpp` — implemented the
  four new methods; added `<functional>` and `<sstream>` includes.
- `tests/Microsoft/Devices/Sensors/AccelerometerReadingTests.cpp` — replaced
  empty file with 11 complete test cases.
- `plan_devices.md` — added at repo root; 31-task plan covering all missing
  `Microsoft::Devices::Sensors` types plus `VibrateController`.
- `src/Microsoft/Devices/Sensors/SensorFailedException.cpp` (Task 3) — fixed
  default constructor to pass `"Sensor failed."` instead of an empty message.
- `tests/Microsoft/Devices/Sensors/SensorFailedExceptionTests.cpp` (Task 3,
  new) — 3 test cases: default ctor message non-empty, `const char*` ctor
  message match, catch as `System::Exception`.
- `src/Microsoft/Devices/Sensors/AccelerometerFailedException.cpp` (Task 4) —
  fixed default constructor to pass `"Accelerometer failed."` instead of
  delegating to the (formerly empty) base default message.
- `tests/Microsoft/Devices/Sensors/AccelerometerFailedExceptionTests.cpp`
  (Task 4, new) — 4 test cases: default ctor doesn't throw, `const char*` ctor
  message match, catch as `SensorFailedException`, catch as `System::Exception`.
- `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`,
  `src/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.cpp` (Task 5,
  both new) — WP7 7.0 legacy `Accelerometer.ReadingChanged` event args class.
- `tests/Microsoft/Devices/Sensors/AccelerometerReadingEventArgsTests.cpp`
  (Task 6, new) — 14 test cases per plan_devices.md Task 6 coverage list.
- `include/Microsoft/Devices/Sensors/CalibrationEventArgs.hpp`,
  `src/Microsoft/Devices/Sensors/CalibrationEventArgs.cpp` (Task 7, both new).
- `tests/Microsoft/Devices/Sensors/CalibrationEventArgsTests.cpp` (Task 8,
  new) — 3 test cases per plan_devices.md Task 8 coverage list.
- `include/Microsoft/Devices/Sensors/CompassReading.hpp`,
  `src/Microsoft/Devices/Sensors/CompassReading.cpp` (Task 9, both new).
- `tests/Microsoft/Devices/Sensors/CompassReadingTests.cpp` (Task 10, new) —
  13 test cases per plan_devices.md Task 10 coverage list.
- `include/Microsoft/Devices/Sensors/Compass.hpp`,
  `src/Microsoft/Devices/Sensors/Compass.cpp` (Tasks 11–12, both new) —
  `SensorBase<CompassReading>` class; includes a `using
  SensorBase<CompassReading>::Dispose;` to un-hide the base no-arg `Dispose()`
  (see bug note in Section 5).
- `tests/Microsoft/Devices/Sensors/CompassTests.cpp` (Task 13, new) — 7 test
  cases per plan_devices.md Task 13 coverage list.
- `include/Microsoft/Devices/Sensors/GyroscopeReading.hpp`,
  `src/Microsoft/Devices/Sensors/GyroscopeReading.cpp` (Task 14, both new).
- `tests/Microsoft/Devices/Sensors/GyroscopeReadingTests.cpp` (Task 15, new)
  — 11 test cases per plan_devices.md Task 15 coverage list.
- `include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
  `src/Microsoft/Devices/Sensors/Gyroscope.cpp` (Tasks 16–17, both new) —
  real SDL3-backed sensor mirroring `Accelerometer`.
- `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp` (Task 18, new) — 7
  test cases per plan_devices.md Task 18 coverage list.
- `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`,
  `src/Microsoft/Devices/Sensors/AttitudeReading.cpp` (Task 19, both new).
- `tests/Microsoft/Devices/Sensors/AttitudeReadingTests.cpp` (Task 20, new)
  — 14 test cases per plan_devices.md Task 20 coverage list.
- `include/Microsoft/Devices/Sensors/MotionReading.hpp`,
  `src/Microsoft/Devices/Sensors/MotionReading.cpp` (Task 21, both new).
- `tests/Microsoft/Devices/Sensors/MotionReadingTests.cpp` (Task 22, new) —
  13 test cases per plan_devices.md Task 22 coverage list.
- `include/Microsoft/Devices/Sensors/Motion.hpp`,
  `src/Microsoft/Devices/Sensors/Motion.cpp` (Tasks 23–24, both new) —
  `SensorBase<MotionReading>` stub class mirroring `Compass`.
- `tests/Microsoft/Devices/Sensors/MotionTests.cpp` (Task 25, new) — 7 test
  cases per plan_devices.md Task 25 coverage list.
- `include/Microsoft/Devices/VibrateController.hpp`,
  `src/Microsoft/Devices/VibrateController.cpp` (Tasks 26–27, both new) —
  static-only SDL3-haptic-backed vibration control, `Microsoft::Devices`
  namespace (not `Sensors`).
- `tests/Microsoft/Devices/VibrateControllerTests.cpp` (Task 28, new) — 6
  test cases per plan_devices.md Task 28 coverage list.
- Task 29 (CMakeLists.txt): no edit needed — confirmed `GLOB_RECURSE` already
  covers all new files.

---

## 4. Current blocker / main problem

No blocker. The cmake-build-debug directory had a stale CMakeCache pointing to
the old source path `/rv/data/development/github.com/openeggbert/cna`. It was
fixed by deleting `CMakeCache.txt` and reconfiguring:

```bash
cmake -S /rv/data/development/github.com/openeggbert/cna_devices \
      -B /rv/data/development/github.com/openeggbert/cna_devices/cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
```

This must be done once after a fresh clone or if the cache is stale. The cmake
build dirs for bgfx and vulkan may have the same stale-cache problem.

---

## 5. Known bugs and limitations

- `AccelerometerTests.cpp` does not exist yet — the SDL3 Accelerometer class is
  untested (requires hardware or SDL mock). **incomplete**
- **`Accelerometer.hpp` has a name-hiding bug:** declaring `void Dispose(bool
  disposing) override;` without a `using SensorBase<AccelerometerReading>::Dispose;`
  hides the inherited public no-arg `Dispose()` (the actual `System::IDisposable`
  contract method) — `accel.Dispose()` currently fails to compile for any
  caller. Found while fixing the identical bug in `Compass.hpp` (Task 11–13).
  Undetected until now because no test exercises it. Fix: add the same
  one-line `using` declaration when `AccelerometerTests.cpp` is written.
  **incomplete / bug**
- `GetTypeNameCPP(...)` NAME-string convention is inconsistent across the
  codebase: some files use dot-separated .NET-style names (e.g.
  `"Microsoft.Xna.Framework.Graphics.IndexBuffer"`, matching Section 6's
  documented invariant), others use `::` (e.g. `Accelerometer.cpp`, `Cue.cpp`,
  `AudioEngine.cpp`, `SoundBank.cpp`, `WaveBank.cpp`, `DateTime.cpp`,
  `DateTimeOffset.cpp` — grep `GetTypeNameCPP` to find all). All Devices/Sensors
  classes written in this phase (`CompassReading`, `AccelerometerReadingEventArgs`,
  `CalibrationEventArgs`, `Compass`) use the dot convention. Not fixed — a
  pre-existing, cross-cutting issue outside the devices-phase scope.
  **inconsistent / needs a dedicated cleanup pass**
- `Compass` and `Motion` will remain stubs (`NotSupported`) until SDL3 gains
  magnetometer support. **by design / known limitation**
- cmake-build-vulkan and cmake-build-bgfx may have the same stale-cache issue as
  cmake-build-debug had. **suspected / needs verification**

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

**Sensor pattern (Accelerometer is the reference implementation):**
- Static `g_sensor_` / `g_sensorId_` hold the single open SDL sensor handle.
- `static int instanceCount_` enforces the ≤ 10 simultaneous instance limit.
- `static bool eventWatchRegistered_` guards the SDL event filter lifecycle.
- `Start()` opens the sensor and registers the SDL event watch.
- `Stop()` unregisters from the started-instances list.
- `Dispose(bool)` calls `Stop()`, decrements counter, closes the sensor handle
  when the last instance is disposed.
- `ProcessSensorUpdateEvent()` is called from the SDL event filter on every
  `SDL_EVENT_SENSOR_UPDATE`.

**Gyroscope** is implemented (`Gyroscope.hpp`/`.cpp`), following this pattern
exactly with `SDL_SENSOR_GYRO` instead of `SDL_SENSOR_ACCEL`.

**Compass** and **Motion** are implemented as stubs: SDL3 has no magnetometer
API, so both are always `SensorState::NotSupported` and `Start()` always
throws `SensorFailedException`. `Motion.cpp` has a `// TODO` marking where
real sensor fusion (Accelerometer + Compass + Gyroscope) should be wired up
once compass support exists.

**VibrateController** (`include/Microsoft/Devices/VibrateController.hpp`,
`src/Microsoft/Devices/VibrateController.cpp`) is a static-only class (no
`SensorBase<T>`, no instances) that drives SDL3's haptic API directly —
`SDL_GetHaptics`/`SDL_OpenHaptic`/`SDL_InitHapticRumble`/
`SDL_PlayHapticRumble`/`SDL_StopHapticEffects`. One code path serves both
Desktop and Android (SDL3's Android backend auto-exposes the phone's
vibration motor as a haptic device — no `#ifdef __ANDROID__` needed). All
calls silently no-op when no haptic device is available (e.g. this headless
dev container).

**SensorBase<T>** (template, header-only) owns `CurrentValue`, `IsDataValid`,
`TimeBetweenUpdates`, `CurrentValueChanged`, and `Dispose()` — concrete sensors
override `Start()`, `Stop()`, and `Dispose(bool)`.

**Invariants:**
- `GetTypeName()` returns `.`-separated .NET names
  (e.g. `"Microsoft.Devices.Sensors.AccelerometerReading"`).
- `GetHashCode()` returns `std::size_t`.
- Every public API member in every `.hpp` has a Doxygen `/** @brief */` block.

---

## 7. Useful commands

```bash
# Configure (run once, or after stale-cache issue):
cmake -S /rv/data/development/github.com/openeggbert/cna_devices \
      -B /rv/data/development/github.com/openeggbert/cna_devices/cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build library:
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build tests:
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Run only Devices/Sensors tests:
cd cmake-build-debug && ctest --output-on-failure -R "AccelerometerReading|SensorFailed|AccelerometerFailed"
# (AccelerometerReading matches both AccelerometerReadingTests and AccelerometerReadingEventArgsTests)

# Run a single test suite:
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerReadingTests*"
```

---

## 8. Next smallest tasks

`plan_devices.md` is fully complete (31/31 tasks). There is no active plan
file for the next phase. The most valuable next steps are the follow-up
items discovered during this phase (Section 5) — no new plan document exists
for these yet, so treat the items below as a proposed starting point rather
than a numbered plan:

1. **Write `AccelerometerTests.cpp`** (SDL3 `Accelerometer` class has no test
   coverage at all — the only sensor implementation without one).
   - While writing it, you will hit the same `Dispose()` name-hiding compile
     error found in `Compass`/`Gyroscope`/`Motion`: add `using
     SensorBase<AccelerometerReading>::Dispose;` to
     `include/Microsoft/Devices/Sensors/Accelerometer.hpp` (see Section 5).
   - Model the test file on `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
     (branches on live `getIsSupportedProperty()` so it passes both headless
     and on real hardware).

2. **`GetTypeNameCPP(...)` naming-convention cleanup** (optional, larger,
   cross-cutting — see Section 5 for the full file list). Not specific to
   the devices phase; would need its own scoped plan since it touches
   `Cue.cpp`, `AudioEngine.cpp`, `SoundBank.cpp`, `WaveBank.cpp`,
   `DateTime.cpp`, `DateTimeOffset.cpp`, and `Accelerometer.cpp`.

3. Ask the user what the next feature phase should be — devices/sensors is
   done; there's no committed plan for what comes after.

---

## 9. Do not do yet

- Do not add camera, radio, or phone-hardware types to `Microsoft::Devices`.
- Do not implement sensor fusion in `Motion` — keep it as a `NotSupported` stub
  until SDL3 gains magnetometer access.
- Do not restructure `SensorBase<T>` or `ISensorReading` — they are stable and
  used by production code.
- Do not touch the graphics, audio, or input subsystems — they are unrelated to
  the current phase.
- Do not delete or rename existing `.cpp` or `.hpp` files without checking that
  nothing else references them.
- Do not run `cmake --build` without first checking that `CMakeCache.txt`
  references the correct source directory.

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
