# Plan: Microsoft::Devices and Microsoft::Devices::Sensors

**Goal:** Complete the `Microsoft::Devices::Sensors` implementation and add
`Microsoft::Devices::VibrateController`. All public API must match the original
XNA 4.0 / Windows Phone 7 specification exactly.

**Date:** 2026-06-30

---

## Scope Decision: Microsoft.Devices namespace

Only **VibrateController** is in scope from `Microsoft::Devices`. Camera,
PhotoCamera, and Radio target phone-exclusive hardware with no SDL3 equivalent
and are not relevant for a general game runtime.

---

## Complete XNA/WP7 Microsoft.Devices.Sensors namespace — gap table

| Type | Kind | CNA state | Notes |
|---|---|---|---|
| `ISensorReading` | interface | ✅ complete | |
| `SensorState` | enum | ✅ complete | |
| `SensorBase<T>` | template class | ✅ complete | |
| `SensorReadingEventArgs<T>` | template class | ✅ complete | |
| `SensorFailedException` | class | ✅ impl, ❌ no tests | |
| `AccelerometerFailedException` | class | ✅ impl, ❌ no tests | only sensor-specific exception in XNA |
| `AccelerometerReading` | struct | ✅ impl, ❌ no tests | missing `==`, `ToString`, `GetHashCode`, `GetTypeName` |
| `Accelerometer` | sealed class | ✅ SDL3 impl, ❌ no tests | |
| `AccelerometerReadingEventArgs` | class | ❌ missing | WP7 7.0 legacy: `X/Y/Z : double`, `Timestamp`; `ReadingChanged` event on old API |
| `CalibrationEventArgs` | class | ❌ missing | used by `Compass.Calibrate` and `Motion.Calibrate` events |
| `CompassReading` | struct | ❌ missing | |
| `Compass` | sealed class | ❌ missing | SDL3 has no magnetometer — stub as `NotSupported` |
| `GyroscopeReading` | struct | ❌ missing | SDL3: `SDL_SENSOR_GYRO` |
| `Gyroscope` | sealed class | ❌ missing | SDL3: `SDL_SENSOR_GYRO`, same pattern as `Accelerometer` |
| `AttitudeReading` | struct | ❌ missing | `Pitch/Roll/Yaw : float`, `Quaternion`, `RotationMatrix`, `Timestamp` |
| `MotionReading` | struct | ❌ missing | `Attitude`, `DeviceAcceleration`, `DeviceRotationRate`, `Gravity`, `Timestamp` |
| `Motion` | sealed class | ❌ missing | sensor fusion; stub as `NotSupported` on all platforms |

**Note on exceptions:** `CompassFailedException` and `GyroscopeFailedException` do **NOT**
exist in the XNA namespace. Only `SensorFailedException` and `AccelerometerFailedException`
are official. `Compass`, `Gyroscope`, and `Motion` throw plain `SensorFailedException`.

---

## Phase 1: Fix AccelerometerReading API completeness

### Task 1 — AccelerometerReading: add `==`, `ToString`, `GetHashCode`, `GetTypeName`

**Files:**
- `include/Microsoft/Devices/Sensors/AccelerometerReading.hpp`
- `src/Microsoft/Devices/Sensors/AccelerometerReading.cpp`

The XNA `AccelerometerReading` is a value type (struct) with value semantics.
Add to `.hpp`:
- `operator==` / `operator!=`
- `ToString()` override (format: `"Acceleration:X:0 Y:0 Z:0"`)
- `GetHashCode()` override
- `NOXNA GetTypeName()` returning `"Microsoft.Devices.Sensors.AccelerometerReading"`
- `#include "CNA/CNAHelper.hpp"` (required because `NOXNA` is used)

Implement all four in `.cpp`.

### Task 2 — Tests: AccelerometerReading

**File:** `tests/Microsoft/Devices/Sensors/AccelerometerReadingTests.cpp`
(file exists but has no test cases — fill it in)

Required coverage:
- Default constructor: `Acceleration` is `Vector3::Zero`, `Timestamp` is default
- Parameterized constructor: values stored correctly
- `getAccelerationProperty()` / `setAccelerationProperty()`
- `getTimestampProperty()` / `setTimestampProperty()`
- `operator==`: equal instances → `true`; unequal → `false`
- `operator!=`: complementary
- `ToString()`: spot-check format
- `GetHashCode()`: equal instances produce equal hashes

---

## Phase 2: Tests for existing exception types

### Task 3 — Tests: SensorFailedException

**File:** `tests/Microsoft/Devices/Sensors/SensorFailedExceptionTests.cpp` (new)

Required coverage:
- Default constructor: `what()` is non-empty
- `const char*` constructor: `what()` matches the string passed
- Can be caught as `System::Exception`

### Task 4 — Tests: AccelerometerFailedException

**File:** `tests/Microsoft/Devices/Sensors/AccelerometerFailedExceptionTests.cpp` (new)

Required coverage:
- Default constructor does not throw
- `const char*` constructor: `what()` matches
- Can be caught as `SensorFailedException`
- Can be caught as `System::Exception`

---

## Phase 3: AccelerometerReadingEventArgs (WP7 7.0 legacy)

### Task 5 — Implement AccelerometerReadingEventArgs

**Files to create:**
- `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`
- `src/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.cpp`

Specification:
- Namespace `Microsoft::Devices::Sensors`
- Inherits `System::EventArgs`
- `// SPDX-License-Identifier: MS-PL` in both files
- Properties:
  - `X : double` — X-axis acceleration
  - `Y : double` — Y-axis acceleration
  - `Z : double` — Z-axis acceleration
  - `Timestamp : System::DateTimeOffset`
- Note: WP7 7.0 legacy event args for the old `ReadingChanged` event. The current
  CNA `Accelerometer` uses the WP7 7.1 `SensorBase` pattern (`CurrentValueChanged` /
  `SensorReadingEventArgs<AccelerometerReading>`). This class exists in the namespace
  for API completeness but is not wired to the current `Accelerometer`.
- `operator==` / `operator!=`
- `ToString()` and `GetHashCode()`
- `NOXNA GetTypeName()` returning `"Microsoft.Devices.Sensors.AccelerometerReadingEventArgs"`
- Full Doxygen on all public members

### Task 6 — Tests: AccelerometerReadingEventArgs

**File:** `tests/Microsoft/Devices/Sensors/AccelerometerReadingEventArgsTests.cpp` (new)

Required coverage:
- Default constructor: `X`, `Y`, `Z` are `0.0`, `Timestamp` is default
- All property getter/setter pairs
- `operator==` and `operator!=`
- `ToString()` format spot-check
- `GetHashCode()` consistency
- Can be used as `System::EventArgs&`

---

## Phase 4: CalibrationEventArgs

### Task 7 — Implement CalibrationEventArgs

**Files to create:**
- `include/Microsoft/Devices/Sensors/CalibrationEventArgs.hpp`
- `src/Microsoft/Devices/Sensors/CalibrationEventArgs.cpp`

Specification:
- Namespace `Microsoft::Devices::Sensors`
- Inherits `System::EventArgs`
- `// SPDX-License-Identifier: MS-PL` in both files
- Default constructor only (the XNA class is intentionally empty)
- `NOXNA GetTypeName()` returning `"Microsoft.Devices.Sensors.CalibrationEventArgs"`
- Full Doxygen on all public members

### Task 8 — Tests: CalibrationEventArgs

**File:** `tests/Microsoft/Devices/Sensors/CalibrationEventArgsTests.cpp` (new)

Required coverage:
- Default construction succeeds without throwing
- Instance can be used as `System::EventArgs&`
- `GetTypeName()` returns expected string

---

## Phase 5: CompassReading

### Task 9 — Implement CompassReading

**Files to create:**
- `include/Microsoft/Devices/Sensors/CompassReading.hpp`
- `src/Microsoft/Devices/Sensors/CompassReading.cpp`

Specification (XNA WP7 API — all properties have internal setter, public getter):
- Namespace `Microsoft::Devices::Sensors`
- Inherits `ISensorReading`
- `// SPDX-License-Identifier: MS-PL` in both files
- Properties:
  - `HeadingAccuracy : double`
  - `MagneticHeading : double`
  - `MagnetometerReading : Microsoft::Xna::Framework::Vector3`
  - `Timestamp : System::DateTimeOffset` (override from ISensorReading)
  - `TrueHeading : double`
- `operator==` / `operator!=`
- `ToString()` and `GetHashCode()`
- `NOXNA GetTypeName()` returning `"Microsoft.Devices.Sensors.CompassReading"`
- Full Doxygen on all public members

### Task 10 — Tests: CompassReading

**File:** `tests/Microsoft/Devices/Sensors/CompassReadingTests.cpp` (new)

Required coverage:
- Default constructor: all numeric fields `0.0`, Timestamp default
- All property getter/setter pairs
- `operator==`: equal → `true`; unequal (differing `HeadingAccuracy`) → `false`
- `operator!=`: complementary
- `ToString()`: spot-check format
- `GetHashCode()`: equal instances produce equal hashes

---

## Phase 6: Compass

### Task 11 — Implement Compass (.hpp)

**File:** `include/Microsoft/Devices/Sensors/Compass.hpp`

Specification:
- `class Compass final : public SensorBase<CompassReading>`
- Namespace `Microsoft::Devices::Sensors`
- `// SPDX-License-Identifier: MS-PL`, `#include "CNA/CNAHelper.hpp"`
- Public members:
  - `static bool getIsSupportedProperty()`
  - `SensorState getStateProperty() const`
  - `Compass()` constructor
  - `void Start() override`
  - `void Stop() override`
  - `void Dispose(bool disposing) override`
  - `NOXNA GetTypeName()`
  - `System::EventHandler<CalibrationEventArgs> Calibrate` (public event)
- Private: `SensorState state_`, `bool started_`, `static int instanceCount_`,
  `static constexpr bytecs MaxSensorCount = 10`
- Full Doxygen on all public members

### Task 12 — Implement Compass (.cpp)

**File:** `src/Microsoft/Devices/Sensors/Compass.cpp`

Platform strategy:
- **All platforms (including Android):** SDL3 has no magnetometer/compass API.
  `getIsSupportedProperty()` returns `false`; `getStateProperty()` returns
  `SensorState::NotSupported`; `Start()` throws `SensorFailedException`.
- Constructor throws `SensorFailedException` if `instanceCount_ >= MaxSensorCount`.
- `Stop()` sets `state_ = SensorState::Disabled`.
- `Dispose(bool)` calls `Stop()` if started, then base `Dispose`.

### Task 13 — Tests: Compass

**File:** `tests/Microsoft/Devices/Sensors/CompassTests.cpp` (new)

Required coverage:
- `getIsSupportedProperty()` returns a `bool` without crashing
- Constructor succeeds (count < 10)
- `getStateProperty()` returns `SensorState::NotSupported` on unsupported platform
- `Start()` throws `SensorFailedException` on unsupported platform
- `Stop()` after no-op `Start()` does not crash
- `Dispose()` succeeds; second `Dispose()` throws `ObjectDisposedException`
- 11th simultaneous instance throws `SensorFailedException`

---

## Phase 7: GyroscopeReading

### Task 14 — Implement GyroscopeReading

**Files to create:**
- `include/Microsoft/Devices/Sensors/GyroscopeReading.hpp`
- `src/Microsoft/Devices/Sensors/GyroscopeReading.cpp`

Specification (XNA WP7 API):
- Namespace `Microsoft::Devices::Sensors`
- Inherits `ISensorReading`
- `// SPDX-License-Identifier: MS-PL` in both files
- Properties:
  - `RotationRate : Microsoft::Xna::Framework::Vector3` — angular velocity in radians/second per axis
  - `Timestamp : System::DateTimeOffset` (override from ISensorReading)
- `operator==` / `operator!=`
- `ToString()` and `GetHashCode()`
- `NOXNA GetTypeName()` returning `"Microsoft.Devices.Sensors.GyroscopeReading"`
- Full Doxygen on all public members

### Task 15 — Tests: GyroscopeReading

**File:** `tests/Microsoft/Devices/Sensors/GyroscopeReadingTests.cpp` (new)

Required coverage:
- Default constructor: `RotationRate` is `Vector3::Zero`, `Timestamp` is default
- `getRotationRateProperty()` / `setRotationRateProperty()`
- `getTimestampProperty()` / `setTimestampProperty()`
- `operator==` and `operator!=`
- `ToString()` format spot-check
- `GetHashCode()` consistency

---

## Phase 8: Gyroscope

### Task 16 — Implement Gyroscope (.hpp)

**File:** `include/Microsoft/Devices/Sensors/Gyroscope.hpp`

Specification (mirrors Accelerometer structure):
- `class Gyroscope final : public SensorBase<GyroscopeReading>`
- Namespace `Microsoft::Devices::Sensors`
- `// SPDX-License-Identifier: MS-PL`, `#include "CNA/CNAHelper.hpp"`
- Public members:
  - `static bool getIsSupportedProperty()`
  - `SensorState getStateProperty() const`
  - `Gyroscope()` constructor
  - `void Start() override`
  - `void Stop() override`
  - `void Dispose(bool disposing) override`
  - `NOXNA GetTypeName()`
- Private: same pattern as Accelerometer (`g_sensor_`, `g_sensorId_`,
  `instanceCount_`, `eventWatchRegistered_`, `startedInstances_`, `MaxSensorCount`)
- Full Doxygen on all public members

### Task 17 — Implement Gyroscope (.cpp)

**File:** `src/Microsoft/Devices/Sensors/Gyroscope.cpp`

Implementation mirrors `Accelerometer.cpp` with these differences:
- Use `SDL_SENSOR_GYRO` instead of `SDL_SENSOR_ACCEL`
- Raw data from SDL is already in radians/second — no gravity normalization needed
- `getIsSupportedProperty()`: same platform check, looks for `SDL_SENSOR_GYRO`
- `ProcessSensorUpdateEvent()` stores `RotationRate` directly; on Android apply
  the same orientation-aware axis remap as Accelerometer

### Task 18 — Tests: Gyroscope

**File:** `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp` (new)

Required coverage:
- `getIsSupportedProperty()` returns a `bool` without crashing
- Constructor succeeds (count < 10)
- `getStateProperty()` reflects support status correctly
- `Start()` on unsupported platform throws `SensorFailedException`
- `Stop()` does not crash
- `Dispose()` succeeds; second `Dispose()` throws `ObjectDisposedException`
- 11th simultaneous instance throws `SensorFailedException`

---

## Phase 9: AttitudeReading

### Task 19 — Implement AttitudeReading

**Files to create:**
- `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
- `src/Microsoft/Devices/Sensors/AttitudeReading.cpp`

Specification (XNA WP7 API — all properties have internal setter, public getter):
- Namespace `Microsoft::Devices::Sensors`
- Inherits `ISensorReading`
- `// SPDX-License-Identifier: MS-PL` in both files
- Properties:
  - `Pitch : float` — rotation around X-axis in radians
  - `Roll : float` — rotation around Z-axis in radians
  - `Yaw : float` — rotation around Y-axis in radians
  - `Quaternion : Microsoft::Xna::Framework::Quaternion`
  - `RotationMatrix : Microsoft::Xna::Framework::Matrix`
  - `Timestamp : System::DateTimeOffset` (override from ISensorReading)
- `operator==` / `operator!=`
- `ToString()` and `GetHashCode()`
- `NOXNA GetTypeName()` returning `"Microsoft.Devices.Sensors.AttitudeReading"`
- Full Doxygen on all public members

### Task 20 — Tests: AttitudeReading

**File:** `tests/Microsoft/Devices/Sensors/AttitudeReadingTests.cpp` (new)

Required coverage:
- Default constructor: `Pitch`, `Roll`, `Yaw` are `0.0f`; `Quaternion` identity;
  `RotationMatrix` identity; `Timestamp` default
- All property getter/setter pairs
- `operator==` and `operator!=`
- `ToString()` format spot-check
- `GetHashCode()` consistency

---

## Phase 10: MotionReading

### Task 21 — Implement MotionReading

**Files to create:**
- `include/Microsoft/Devices/Sensors/MotionReading.hpp`
- `src/Microsoft/Devices/Sensors/MotionReading.cpp`

Specification (XNA WP7 API — all properties have internal setter, public getter):
- Namespace `Microsoft::Devices::Sensors`
- Inherits `ISensorReading`
- `// SPDX-License-Identifier: MS-PL` in both files
- Properties:
  - `Attitude : AttitudeReading` — fused orientation
  - `DeviceAcceleration : Microsoft::Xna::Framework::Vector3` — linear accel without gravity, in g
  - `DeviceRotationRate : Microsoft::Xna::Framework::Vector3` — angular velocity in radians/second
  - `Gravity : Microsoft::Xna::Framework::Vector3` — gravity vector in g
  - `Timestamp : System::DateTimeOffset` (override from ISensorReading)
- `operator==` / `operator!=`
- `ToString()` and `GetHashCode()`
- `NOXNA GetTypeName()` returning `"Microsoft.Devices.Sensors.MotionReading"`
- Full Doxygen on all public members

### Task 22 — Tests: MotionReading

**File:** `tests/Microsoft/Devices/Sensors/MotionReadingTests.cpp` (new)

Required coverage:
- Default constructor: all vector fields `Vector3::Zero`, `Attitude` default
- All property getter/setter pairs
- `operator==` and `operator!=`
- `ToString()` format spot-check
- `GetHashCode()` consistency

---

## Phase 11: Motion (sensor fusion)

### Task 23 — Implement Motion (.hpp)

**File:** `include/Microsoft/Devices/Sensors/Motion.hpp`

Specification:
- `class Motion final : public SensorBase<MotionReading>`
- Namespace `Microsoft::Devices::Sensors`
- `// SPDX-License-Identifier: MS-PL`, `#include "CNA/CNAHelper.hpp"`
- Public members:
  - `static bool getIsSupportedProperty()`
  - `SensorState getStateProperty() const`
  - `Motion()` constructor
  - `void Start() override`
  - `void Stop() override`
  - `void Dispose(bool disposing) override`
  - `NOXNA GetTypeName()`
  - `System::EventHandler<CalibrationEventArgs> Calibrate` (public event)
- Private: `SensorState state_`, `bool started_`, `static int instanceCount_`,
  `static constexpr bytecs MaxSensorCount = 10`
- Full Doxygen on all public members

### Task 24 — Implement Motion (.cpp)

**File:** `src/Microsoft/Devices/Sensors/Motion.cpp`

Platform strategy:
- Motion requires Accelerometer + Compass + Gyroscope all present.
- SDL3 on desktop and Android has no magnetometer — compass is unavailable.
- Therefore: `getIsSupportedProperty()` returns `false` on all current platforms;
  `Start()` throws `SensorFailedException("Motion is not supported on this platform.")`.
- Mark a `// TODO` where sensor fusion should be wired once compass is available.
- Constructor throws `SensorFailedException` if `instanceCount_ >= MaxSensorCount`.
- `Stop()`, `Dispose(bool)` follow the same pattern as `Compass`.

### Task 25 — Tests: Motion

**File:** `tests/Microsoft/Devices/Sensors/MotionTests.cpp` (new)

Required coverage:
- `getIsSupportedProperty()` returns `false` on desktop
- Constructor succeeds (count < 10)
- `getStateProperty()` is `SensorState::NotSupported`
- `Start()` throws `SensorFailedException`
- `Stop()` does not crash
- `Dispose()` succeeds; second `Dispose()` throws `ObjectDisposedException`
- 11th simultaneous instance throws `SensorFailedException`

---

## Phase 12: VibrateController

### Task 26 — Implement VibrateController (.hpp)

**File:** `include/Microsoft/Devices/VibrateController.hpp`

Specification (XNA WP7 API):
- Namespace `Microsoft::Devices`
- `// SPDX-License-Identifier: MS-PL`
- Static-only class: `VibrateController() = delete`
- Public static methods:
  - `static void Start(const System::TimeSpan& duration)`
  - `static void Stop()`
- `duration` is clamped silently to `[TimeSpan::Zero, TimeSpan::FromSeconds(5)]`
  (XNA documented maximum on Windows Phone 7)
- Full Doxygen on both methods

### Task 27 — Implement VibrateController (.cpp)

**File:** `src/Microsoft/Devices/VibrateController.cpp`

Platform strategy using SDL3:
- **Desktop:** `SDL_GetHaptics()` to enumerate haptic devices. If any found, open the
  first, call `SDL_HapticRumbleInit()` + `SDL_HapticRumblePlay(haptic, 1.0f, durationMs)`.
  Silent no-op if no haptic device found.
- **Android (`#ifdef __ANDROID__`):** use SDL3 haptic API if a controller with haptics
  is connected; otherwise use JNI via `SDL_AndroidSendMessage()` to trigger
  `Vibrator.vibrate(milliseconds)`.
- **`Stop()`:** call `SDL_HapticStopAll()` on the open haptic device, or no-op if none open.
- Duration clamping: enforce `[TimeSpan::Zero, TimeSpan::FromSeconds(5)]` before
  converting to milliseconds.

### Task 28 — Tests: VibrateController

**File:** `tests/Microsoft/Devices/VibrateControllerTests.cpp` (new)

Note: runs on desktop where haptic hardware may be absent; all calls must be no-throw.

Required coverage:
- `Stop()` before any `Start()` does not throw
- `Start(TimeSpan::Zero)` does not throw
- `Start(TimeSpan::FromMilliseconds(100))` does not throw
- `Start(TimeSpan::FromSeconds(10))` does not throw (clamped silently)
- `Stop()` after `Start()` does not throw
- Two consecutive `Start()` calls do not crash

---

## Phase 13: CMake integration

### Task 29 — Update CMakeLists.txt

Add new `.cpp` files to the `CNA` library target:
```
src/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.cpp
src/Microsoft/Devices/Sensors/CalibrationEventArgs.cpp
src/Microsoft/Devices/Sensors/CompassReading.cpp
src/Microsoft/Devices/Sensors/Compass.cpp
src/Microsoft/Devices/Sensors/GyroscopeReading.cpp
src/Microsoft/Devices/Sensors/Gyroscope.cpp
src/Microsoft/Devices/Sensors/AttitudeReading.cpp
src/Microsoft/Devices/Sensors/MotionReading.cpp
src/Microsoft/Devices/Sensors/Motion.cpp
src/Microsoft/Devices/VibrateController.cpp
```

Add new test files to the test target:
```
tests/Microsoft/Devices/Sensors/SensorFailedExceptionTests.cpp
tests/Microsoft/Devices/Sensors/AccelerometerFailedExceptionTests.cpp
tests/Microsoft/Devices/Sensors/AccelerometerReadingTests.cpp         (already registered)
tests/Microsoft/Devices/Sensors/AccelerometerReadingEventArgsTests.cpp
tests/Microsoft/Devices/Sensors/CalibrationEventArgsTests.cpp
tests/Microsoft/Devices/Sensors/CompassReadingTests.cpp
tests/Microsoft/Devices/Sensors/CompassTests.cpp
tests/Microsoft/Devices/Sensors/GyroscopeReadingTests.cpp
tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp
tests/Microsoft/Devices/Sensors/AttitudeReadingTests.cpp
tests/Microsoft/Devices/Sensors/MotionReadingTests.cpp
tests/Microsoft/Devices/Sensors/MotionTests.cpp
tests/Microsoft/Devices/VibrateControllerTests.cpp
```

---

## Phase 14: Build and test verification

### Task 30 — Build (debug)

```bash
cmake --build cmake-build-debug --target CNA
```

Report: changed files, any new errors, any residual warnings.

### Task 31 — Run tests

```bash
cd cmake-build-debug && ctest --output-on-failure
```

Report: pass/fail per test suite, any failures with full output.

---

## Task Summary

| # | Task | Phase |
|---|---|---|
| 1 | AccelerometerReading: add `==`, `ToString`, `GetHashCode`, `GetTypeName` | 1 |
| 2 | Tests: AccelerometerReading | 1 |
| 3 | Tests: SensorFailedException | 2 |
| 4 | Tests: AccelerometerFailedException | 2 |
| 5 | Implement AccelerometerReadingEventArgs (.hpp + .cpp) | 3 |
| 6 | Tests: AccelerometerReadingEventArgs | 3 |
| 7 | Implement CalibrationEventArgs (.hpp + .cpp) | 4 |
| 8 | Tests: CalibrationEventArgs | 4 |
| 9 | Implement CompassReading (.hpp + .cpp) | 5 |
| 10 | Tests: CompassReading | 5 |
| 11 | Implement Compass (.hpp) | 6 |
| 12 | Implement Compass (.cpp) — stub, `NotSupported` | 6 |
| 13 | Tests: Compass | 6 |
| 14 | Implement GyroscopeReading (.hpp + .cpp) | 7 |
| 15 | Tests: GyroscopeReading | 7 |
| 16 | Implement Gyroscope (.hpp) | 8 |
| 17 | Implement Gyroscope (.cpp) — SDL3 `SDL_SENSOR_GYRO` | 8 |
| 18 | Tests: Gyroscope | 8 |
| 19 | Implement AttitudeReading (.hpp + .cpp) | 9 |
| 20 | Tests: AttitudeReading | 9 |
| 21 | Implement MotionReading (.hpp + .cpp) | 10 |
| 22 | Tests: MotionReading | 10 |
| 23 | Implement Motion (.hpp) | 11 |
| 24 | Implement Motion (.cpp) — stub, `NotSupported` | 11 |
| 25 | Tests: Motion | 11 |
| 26 | Implement VibrateController (.hpp) | 12 |
| 27 | Implement VibrateController (.cpp) — SDL3 haptic | 12 |
| 28 | Tests: VibrateController | 12 |
| 29 | Update CMakeLists.txt | 13 |
| 30 | Build (debug) | 14 |
| 31 | Run tests | 14 |
