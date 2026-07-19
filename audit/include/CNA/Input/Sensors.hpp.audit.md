# Audit: include/CNA/Input/Sensors.hpp

## Metadata

- Source file: `include/CNA/Input/Sensors.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares SensorTypeEXT (Accelerometer/Gyroscope + Left/Right dual-sensor variants) and Sensors: static-only host motion-sensor enumeration and accelerometer/gyroscope reading, mirroring SDL3's SDL_SensorType/SDL_sensor API.

## Executive Verdict

Needs attention — 1 cross-file verification note (informational, not a defect in this file itself).

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**`SensorTypeEXT`'s ordinals happen to numerically align with real `SDL_SensorType`'s non-negative values** (SDL: `Invalid=-1, Unknown=0, Accel=1, Gyro=2, AccelL=3, GyroL=4, AccelR=5, GyroR=6`; `SensorTypeEXT`: `Unknown=0, Accelerometer=1, Gyroscope=2, AccelerometerLeft=3, GyroscopeLeft=4, AccelerometerRight=5, GyroscopeRight=6` — a 1:1 match for every value SDL considers valid), **but `SensorTypeEXT` has no corresponding entry for `SDL_SENSOR_INVALID` (-1) at all** — a hypothetical raw cast of `SDL_SENSOR_INVALID` would produce an out-of-range `SensorTypeEXT` value with no name. This shard's own `Sensors.cpp` does not perform the SDL-to-EXT mapping itself (delegates entirely to `SystemSensorBackend::GetSensors()`, not yet audited) — worth checking that consumer explicitly handles the invalid/error case rather than assuming every SDL sensor type is one of the 7 valid ones.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

**`SensorTypeEXT`'s ordinals happen to numerically align with real `SDL_SensorType`'s non-negative values** (SDL: `Invalid=-1, Unknown=0, Accel=1, Gyro=2, AccelL=3, GyroL=4, AccelR=5, GyroR=6`; `SensorTypeEXT`: `Unknown=0, Accelerometer=1, Gyroscope=2, AccelerometerLeft=3, GyroscopeLeft=4, AccelerometerRight=5, GyroscopeRight=6` — a 1:1 match for every value SDL considers valid), **but `SensorTypeEXT` has no corresponding entry for `SDL_SENSOR_INVALID` (-1) at all** — a hypothetical raw cast of `SDL_SENSOR_INVALID` would produce an out-of-range `SensorTypeEXT` value with no name. This shard's own `Sensors.cpp` does not perform the SDL-to-EXT mapping itself (delegates entirely to `SystemSensorBackend::GetSensors()`, not yet audited) — worth checking that consumer explicitly handles the invalid/error case rather than assuming every SDL sensor type is one of the 7 valid ones.

## Cross-File Observations

See `Sensors.cpp`'s own report; the actual SDL_SensorType-to-SensorTypeEXT mapping logic lives in `SystemSensorBackend` (cna-internal-core or cna-devices, not yet audited).

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
