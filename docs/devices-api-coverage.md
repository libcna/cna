# `Microsoft::Devices` / `Microsoft::Devices::Sensors` — API Coverage

A standalone, per-member reference for the XNA 4.0 / Windows Phone 7 API surface in this
namespace, extracted from `plan_devices.md`'s Phase 0 audit matrices (Tasks
DEVICES-0002–0008) and updated through Phase 8. This is a flat lookup table — for the
prose history of *how* each row got here, see `AUDIT.md`'s `Microsoft::Devices::Sensors`
section; for architecture, see `docs/devices-native-backend-design.md`.

**Confidence legend:** High = confirmed against an archived MSDN "previous-versions" page.
Medium = cross-checked only against MonoGame or inferred from consistent usage, no direct
MSDN page found.

---

## `Microsoft::Devices::VibrateController`

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| `getDefaultProperty()` (static) | Real | High | Never-null singleton |
| `Start(TimeSpan)` | Real | High | Throws `ArgumentOutOfRangeException` outside `[Zero, FromSeconds(5)]` |
| `Stop()` | Real | High | Silent no-op if nothing active/no device |
| `Start(TimeSpan, float intensity)` | `NOXNA` | — | Intensity `[0,1]`; `intensity=0` still uploads a zero-strength effect, not an implicit `Stop()` |
| `getIsSupportedProperty()` | `NOXNA` | — | Probe-only, doesn't hold a device open |
| `getDeviceNameProperty()` | `NOXNA` | — | Probe-only |
| `StartLeftRight(float, float, TimeSpan)` | `NOXNA` | — | `SDL_HAPTIC_LEFTRIGHT`; mutually exclusive with `Start()` (each stops the other's effect) |
| Constructor | Real (private) | High | Only reachable via `getDefaultProperty()` |

## `Microsoft::Devices::Sensors::SensorBase<TSensorReading>`

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| `getCurrentValueProperty()` | Real | High | Throws `InvalidOperationException` if unsupported; does not throw if supported-but-no-reading-yet |
| `getIsDataValidProperty()` | Real | High | Defaults `false` |
| `getTimeBetweenUpdatesProperty()`/`set...` | Real | High | Default 2ms; **stored/observable only, no dispatch-rate enforcement** in `Accelerometer`/`Gyroscope` |
| `CurrentValueChanged` | Real | High | Public event |
| `TimeBetweenUpdatesChanged` | Real | High | Protected event |
| `Start()`/`Stop()` | Real (abstract) | High | |
| `Dispose()` | Real | High | Second call throws `ObjectDisposedException` |

No `IsSupported`/`State` on the base class (those are per-subclass statics/properties).

## `Microsoft::Devices::Sensors::Accelerometer`

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| Constructor | Real | High | Throws `SensorFailedException` past 10 simultaneous instances |
| `getIsSupportedProperty()` (static) | Real | High | Real SDL3-backed (`SDL_SENSOR_ACCEL`) |
| `getStateProperty()` | Real | High | The one sensor class with a real `State` |
| `Start()` | Real | High | Throws `AccelerometerFailedException` on failure |
| `Stop()` | Real | High | |
| `CurrentValueChanged` | Real | High | Fires from the SDL sensor thread — treat as unknown-thread |
| `ReadingChanged` (legacy) | Real | High | WP7 7.0; raised alongside `CurrentValueChanged` |
| Unit conversion | Real | High | m/s² → g (`÷ 9.80665f`), confirmed correct and tested |
| Android axis remap | `NOXNA`-adjacent internal | Medium | `Detail::ConvertAndroidPortraitToXnaLandscape()`; unit-tested, **never hardware-verified** |
| 8 `*ForTesting()`/`InjectSynthetic*` hooks | `NOXNA` | — | Test-only |

## `Microsoft::Devices::Sensors::Gyroscope`

Identical shape to `Accelerometer` minus `ReadingChanged` (correctly absent — no legacy
event in the real API).

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| `getStateProperty()` | `NOXNA` | High | Real `Gyroscope` has no `State` |
| Unit | Real | High | rad/s, no conversion (matches SDL3's own doc and WP7's documented unit) |
| Self-destroy-from-own-callback safety | Real | — | Fully safe (unlike `Accelerometer`, which has the extra `ReadingChanged` check) |

## `Microsoft::Devices::Sensors::Compass`

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| Constructor | Real | High | 10-instance cap |
| `getIsSupportedProperty()` (static) | Real | High | **Real on Android** (`Detail::AndroidCompassBackend`); `false` stub everywhere else |
| `getStateProperty()` | `NOXNA` | High | Real `Compass` has no `State` |
| `Start()` | Real | High | Real on Android; throws `SensorFailedException` elsewhere |
| `Calibrate` | Real | High | Raised on Android for `UNRELIABLE`/`NO_CONTACT` magnetic-field accuracy |
| `SetBackendForTesting()` | `NOXNA` | — | Test-only hook |

## `Microsoft::Devices::Sensors::CompassReading`

| Member | Confidence | Notes |
|---|---|---|
| `HeadingAccuracy` | High (member exists) / N/A (CNA's degree scale is a project choice) | Android: mapped from magnetic-field accuracy status |
| `MagneticHeading` | High | Android: from rotation-vector azimuth, **never hardware-verified** |
| `MagnetometerReading` | High | Android: raw µT vector, no axis remap applied (open question) |
| `TrueHeading` | High (member) | Android: **always equals `MagneticHeading`** — honest limitation, no `System.Device.Location` |

## `Microsoft::Devices::Sensors::Motion`

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| Constructor | Real | High | 10-instance cap; does **not** require a live `Accelerometer`/`Compass`/`Gyroscope` instance |
| `getIsSupportedProperty()` (static) | Real | High | **Real on Android** (`Detail::AndroidMotionBackend`); `false` stub everywhere else |
| `getStateProperty()` | `NOXNA` | High | Real `Motion` has no `State` |
| `Start()` | Real | High | Real on Android; throws `SensorFailedException` elsewhere |
| `Calibrate` | Real | High | **Never raised by any backend** — `IMotionBackend` has no calibration callback at all |
| `SetBackendForTesting()` | `NOXNA` | — | Test-only hook |

## `Microsoft::Devices::Sensors::MotionReading` / `AttitudeReading`

| Member | Confidence | Notes |
|---|---|---|
| `Attitude.Pitch`/`Roll`/`Yaw` | High (members) | Android: derived from the rotation-vector quaternion, internally consistent with `Quaternion`/`RotationMatrix` by construction; **never hardware-verified** |
| `Attitude.Quaternion` | High | Android: direct, unremapped passthrough of the raw rotation-vector quaternion |
| `Attitude.RotationMatrix` | High | Android: `Matrix::CreateFromQuaternion()` on the same `Quaternion` |
| `Gravity` | High (member) | Android: `TYPE_GRAVITY`, m/s² → g conversion applied |
| `DeviceAcceleration` | High (member) | Android: `TYPE_LINEAR_ACCELERATION`, m/s² → g conversion applied |
| `DeviceRotationRate` | High (member) | Android: `TYPE_GYROSCOPE`, rad/s, no conversion |

## Exceptions / Enums

| Type | Confidence | Notes |
|---|---|---|
| `SensorFailedException` | High | Message + `(message, errorId)` ctors; `ErrorId` defaults `0` (no documented real WP7 error codes exist) |
| `AccelerometerFailedException` | High | `Accelerometer`-specific; mirrors `SensorFailedException`'s 3 ctors. `Gyroscope`/`Compass`/`Motion` correctly have no dedicated subclass, use plain `SensorFailedException` |
| `SensorState` (enum) | Medium | 6 values (`NotSupported`/`Ready`/`Initializing`/`NoData`/`NoPermissions`/`Disabled`) — MonoGame cross-check only, no direct MSDN enum page found |
| `ISensorReading` | High | Single `Timestamp` member |
| `CalibrationEventArgs` | High | Empty marker class, confirmed against its exact member-list page |
| `AccelerometerReadingEventArgs` | High | WP7 7.0 legacy, paired with `Accelerometer.ReadingChanged` |
| `SensorReadingEventArgs<T>` | High | Generic wrapper |

---

## Android-native-only internals (`Detail::` namespace, not XNA-facing)

| Type | Purpose |
|---|---|
| `AndroidSensorBridge` | Shared NDK `ASensorManager`/`ASensorEventQueue`/`ALooper` wrapper, one instance per Android sensor type |
| `ICompassBackend` / `AndroidCompassBackend` | Compass's Android implementation |
| `IMotionBackend` / `AndroidMotionBackend` | Motion's Android implementation |
| `ConvertRotationVectorToMagneticHeadingDegrees()` | Pure azimuth-from-quaternion function (Compass) |
| `ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees()` / `ShouldRaiseCalibrateForAccuracyStatus()` | Accuracy-status mapping (Compass) |
| `ConvertRotationVectorToXnaQuaternion()` / `ExtractYawPitchRollFromQuaternion()` | Quaternion passthrough + Euler extraction (Motion) |
| `ConvertAndroidPortraitToXnaLandscape()` | Accelerometer/Gyroscope's existing landscape axis remap (unchanged by this plan) |
