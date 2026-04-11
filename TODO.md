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

## Sound️ Current State (Temporary)

The current SDL3_mixer integration **can work**, but it is not a good long-term design.

Problems:
- `SoundEffect` depends on an already created mixer
- Mixer lifetime is not clearly managed
- SDL3_mixer types (`MIX_*`) leak into engine code
- Harder to extend (music, categories, global volume, device reset, etc.)

👉 This is a **good transition state**, but not a final architecture.

---

# 🎯 Introduce AudioManager (SDL3_mixer Integration)

After the SDL3_mixer migration is stable and builds successfully, the next step is to design a proper audio layer abstraction.

### 🔧 Goal

Create an `AudioManager` for CNA that:

- Owns and manages the `MIX_Mixer` instance
- Handles audio system initialization and shutdown
- Encapsulates all SDL3_mixer-specific logic
- Prevents SDL3_mixer types (`MIX_*`) from leaking into public engine APIs

---

### 🧱 Responsibilities

#### 1. Mixer Lifetime Management
- Create and store a single global/shared `MIX_Mixer`
- Ensure it is initialized exactly once

#### 2. Initialization / Shutdown
- Initialize SDL3_mixer (`MIX_Init`)
- Create mixer (`MIX_CreateMixerDevice`)
- Shutdown cleanly (`MIX_DestroyMixer`, `MIX_Quit`)

#### 3. Engine Isolation
- Hide SDL3_mixer behind CNA abstractions:
  - `SoundEffect`
  - `SoundEffectInstance`
- No SDL headers in public `.hpp`

---

### 🧠 Why This Is Needed

SDL3_mixer changed the model:

- No global audio system
- Everything is tied to `MIX_Mixer`
- Track-based playback (`MIX_Track`)

➡️ Central manager is **required**, not optional.

---

### 🧩 Suggested Structure

```cpp
class AudioManager {
public:
    static void Initialize();
    static void Shutdown();

    static MIX_Mixer* GetMixer();

private:
    static MIX_Mixer* mixer_;
};
````

---

### 🚀 Result

Move CNA from:

> “it works”

to:

> “proper engine architecture”

