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
Plan: `plan_devices.md` (31 tasks).

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

**Tests:** 11/11 `AccelerometerReadingTests` pass. All previously existing tests
continue to pass.

**Working:**
- Full SDL3-backed `Accelerometer` implementation (Start/Stop/Dispose, event
  watch, Android axis remap).
- `AccelerometerReading` — complete: constructors, getters/setters, `operator==`,
  `operator!=`, `ToString()`, `GetHashCode()`, `GetTypeName()`.
- `SensorBase<T>`, `SensorReadingEventArgs<T>`, `ISensorReading`, `SensorState` —
  complete.
- `SensorFailedException`, `AccelerometerFailedException` — implemented, no tests yet.

**Not yet done (per plan_devices.md):**
- Tasks 3–31: tests for exceptions, CalibrationEventArgs, Compass, Gyroscope,
  AttitudeReading, MotionReading, Motion, VibrateController, CMake update, final
  build+test verification.

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
- `SensorFailedException` and `AccelerometerFailedException` have no tests yet.
  **incomplete**
- `Compass`, `Gyroscope`, `Motion`, `VibrateController` are entirely missing.
  **incomplete**
- `Motion` will remain a stub (`NotSupported`) until SDL3 gains magnetometer
  support. **by design / known limitation**
- `AccelerometerReadingEventArgs` (WP7 7.0 legacy) is missing. **incomplete**
- cmake-build-vulkan and cmake-build-bgfx may have the same stale-cache issue as
  cmake-build-debug had. **suspected / needs verification**

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/   ← XNA WP7 sensor API headers
src/Microsoft/Devices/Sensors/       ← sensor implementations (SDL3-backed)
tests/Microsoft/Devices/Sensors/     ← Google Test suites per class
include/Microsoft/Devices/           ← VibrateController (not yet present)
src/Microsoft/Devices/               ← VibrateController impl (not yet present)
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

**Gyroscope** should follow this pattern exactly, using `SDL_SENSOR_GYRO`.

**Compass and Motion** are stubs (SDL3 has no magnetometer API).

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

# Run a single test suite:
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerReadingTests*"
```

---

## 8. Next smallest tasks

Tasks are numbered per `plan_devices.md`.

1. **Task 3 — Tests: SensorFailedException**
   - Goal: write `SensorFailedExceptionTests.cpp`; cover default ctor, `const char*`
     ctor, catch as `System::Exception`.
   - Files: `tests/Microsoft/Devices/Sensors/SensorFailedExceptionTests.cpp` (new)
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="SensorFailedExceptionTests*"`

2. **Task 4 — Tests: AccelerometerFailedException**
   - Goal: write `AccelerometerFailedExceptionTests.cpp`; cover both ctors, catch
     as `SensorFailedException` and `System::Exception`.
   - Files: `tests/Microsoft/Devices/Sensors/AccelerometerFailedExceptionTests.cpp` (new)
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="AccelerometerFailedExceptionTests*"`

3. **Task 5 — Implement AccelerometerReadingEventArgs**
   - Goal: add WP7 7.0 legacy class with `X/Y/Z : double`, `Timestamp`,
     `operator==`, `ToString`, `GetHashCode`, `GetTypeName`.
   - Files: `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`,
     `src/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.cpp` (both new)
   - Verify: build `CNA` target cleanly.

4. **Task 6 — Tests: AccelerometerReadingEventArgs**
   - Files: `tests/Microsoft/Devices/Sensors/AccelerometerReadingEventArgsTests.cpp` (new)
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="AccelerometerReadingEventArgsTests*"`

5. **Task 7 — Implement CalibrationEventArgs**
   - Goal: trivial `EventArgs` subclass; needed by `Compass.Calibrate` and
     `Motion.Calibrate` events.
   - Files: `include/Microsoft/Devices/Sensors/CalibrationEventArgs.hpp`,
     `src/Microsoft/Devices/Sensors/CalibrationEventArgs.cpp` (both new)

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
