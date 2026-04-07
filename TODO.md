# CNA Sensors – TODO Roadmap

## Overview
This document outlines the planned implementation roadmap for sensor support in CNA.

The goal is to extend beyond the original XNA 4.0 capabilities while maintaining a clean, consistent, and XNA-like API design.

---

## Current Status

### Implemented (Partial)
- [x] `SensorState` enum
- [x] `ISensorReading`
- [x] `SensorReadingEventArgs<T>`
- [x] `SensorBase<TSensorReading>`
- [x] `AccelerometerReading`
- [x] `Accelerometer` (SDL3-based, runtime validation pending)

### Notes
- Implementation is **partial**
- Runtime behavior must be validated on **Android**
- `DateTime` / `DateTimeOffset` are also partial
- Sensor timestamps are not yet fully .NET-compatible

---

## Phase 1 – Core Sensors (High Priority)

These provide the most practical value for games.

### Gyroscope
- [ ] `GyroscopeReading`
- [ ] `Gyroscope : SensorBase<GyroscopeReading>`
- [ ] Angular velocity (X, Y, Z)
- [ ] SDL3 integration

### Compass (Magnetometer)
- [ ] `CompassReading`
- [ ] `Compass : SensorBase<CompassReading>`
- [ ] Heading (degrees)
- [ ] Optional raw magnetic field vector

### Motion (Combined Sensor)
- [ ] `MotionReading`
- [ ] `Motion : SensorBase<MotionReading>`
- [ ] Combines:
    - Accelerometer
    - Gyroscope
    - Compass (optional)
- [ ] Provides:
    - Orientation (Quaternion or Euler)
    - Gravity vector
    - Linear acceleration

---

## Phase 2 – Secondary Sensors

Useful but not essential for most games.

### Proximity Sensor
- [ ] `ProximityReading`
- [ ] `Proximity : SensorBase<ProximityReading>`
- [ ] Distance / near-far detection

### Light Sensor
- [ ] `LightReading`
- [ ] `Light : SensorBase<LightReading>`
- [ ] Ambient light level (lux)

### Location (GPS)
- [ ] `LocationReading`
- [ ] `Location : SensorBase<LocationReading>`
- [ ] Latitude / Longitude
- [ ] Optional:
    - Altitude
    - Speed
    - Heading

---

## Phase 3 – Advanced / Optional Sensors

Only implement if needed.

### Pressure / Barometer
- [ ] `PressureReading`
- [ ] `Pressure : SensorBase<PressureReading>`

### Step Counter
- [ ] `StepCounterReading`
- [ ] `StepCounter : SensorBase<StepCounterReading>`

### Gravity Sensor
- [ ] `GravityReading`
- [ ] `Gravity : SensorBase<GravityReading>`

### Linear Acceleration
- [ ] `LinearAccelerationReading`
- [ ] `LinearAcceleration : SensorBase<LinearAccelerationReading>`

### Rotation Vector
- [ ] `RotationVectorReading`
- [ ] Quaternion-based orientation

---

## Platform Integration

### SDL3
- [x] Accelerometer (partial)
- [ ] Gyroscope
- [ ] Additional sensor mappings

### Android (Future)
- [ ] Native Android sensor backend
- [ ] Replace / extend SDL where necessary
- [ ] Validate all sensors on real device

### Desktop
- [ ] Graceful fallback (no sensors available)
- [ ] Ensure `IsSupported == false`

---

## API Consistency Goals

- [ ] Match XNA-style naming conventions
- [ ] Consistent property pattern (`getXProperty`, `setXProperty`)
- [ ] Event-based updates (`CurrentValueChanged`)
- [ ] Strong typing for readings
- [ ] Minimal platform-specific leakage into public API

---

## Testing

- [ ] Unit tests for:
    - `SensorBase`
    - Event dispatching
    - State transitions

- [ ] Runtime tests:
    - Android device validation
    - Sensor availability detection

---

## Known Limitations

- No GC / finalizer parity with .NET
- Timestamp precision differs from .NET
- SDL3 sensor API differs from Android listener model
- Some sensors may not be available on all platforms

---

## Future Ideas

- [ ] Sensor fusion utilities (shared math layer)
- [ ] Filtering / smoothing (low-pass, high-pass)
- [ ] Calibration support
- [ ] Debug visualization tools

---

## Status Summary

**Sensors module:**  
➡️ Functional (partial)  
➡️ Ready for extension  
➡️ Awaiting Android runtime validation