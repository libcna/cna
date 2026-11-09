# Microsoft::Devices — Native Backend Design (Compass/Motion)

## Purpose and status

`Accelerometer` and `Gyroscope` own no native backend: they consume `IPlatformSensors` through
`Detail::PlatformSensorSubsystem<TSensor>`, and the selected platform implements the native sensor
sessions (PLAT-108, which also moved vibration behind `IPlatformHaptics`). `Compass` and `Motion`
keep dedicated native backends, because their fused and magnetometer data is outside the platform
sensor vocabulary. This page is the architectural reference for those backends: why each sensor
was chosen, which limitations are accepted, and the designed but unimplemented iOS path.

| Platform | Compass/Motion backend |
|---|---|
| Android | Implemented: `Detail::AndroidSensorBridge`, `Detail::AndroidCompassBackend`, `Detail::AndroidMotionBackend` (headers under `modules/devices/include/Microsoft/Devices/Sensors/Detail/`), cross-compiled for arm64-v8a, API 24 ([devices-build.md](devices-build.md) §4). |
| iOS | Designed below, not implemented: building for iOS needs macOS/Xcode. |
| Everything else | None. `Compass` and `Motion` are permanent, honest `SensorState::NotSupported` stubs: `getIsSupportedProperty()` returns `false` and `Start()` throws `SensorFailedException`. |

SDL3 has no compass/magnetometer or fused-orientation ("motion") API, only raw accelerometer and
gyroscope sensors. **Do not fake `Compass` or `Motion` from accelerometer/gyroscope data.**
`System.Device.Location` (GPS) is out of scope for `Microsoft::Devices::Sensors`; it matters here
only because a true-heading calculation needs geomagnetic declination (see
[location-future-plan.md](location-future-plan.md)).

## Backend interfaces

`Detail::ICompassBackend` and `Detail::IMotionBackend` (`ICompassBackend.hpp`,
`IMotionBackend.hpp`) are compiled interfaces with no platform-specific `#include`, so they compile
and are mockable everywhere. Their shapes are identical: `Start()` takes a `ReadingCallback` and a
`CalibrationCallback`, because `Motion::Calibrate` is a real Windows Phone 7 event (archived MSDN
`hh239189(v=vs.105)`) that needs a producer, as `Compass::Calibrate` does (MOTION-011,
`DEV-AUD-002`). `Compass` and `Motion` each hold a private `std::unique_ptr` to their backend,
selected at construction by a compile-time platform switch and reused through `SensorBase<T>`'s
current-value dispatch path rather than a parallel event system.

## Android backend path

No JNI or Kotlin bridge is needed: the NDK's `<android/sensor.h>`/`<android/looper.h>`
(`ASensorManager`/`ASensorEventQueue`/`ALooper`) expose everything directly from C++ (Task
DEVICES-0073). `Detail::AndroidSensorBridge` is the shared building block of both backends: one
instance per Android sensor type, owning a dedicated background thread that prepares and polls its
own `ALooper` (thread-affine — it cannot be pumped from an arbitrary caller thread) and delivers
samples by callback, stamped with wall-clock time (never `ASensorEvent::timestamp`, which is
monotonic boot time). Unlike the bridge, which is inert everywhere, the two backends are entirely
`#ifdef __ANDROID__`-gated and referenced only from `Compass.cpp`/`Motion.cpp`'s own Android block.

### Android Compass

`Detail::AndroidCompassBackend` registers **both** `TYPE_ROTATION_VECTOR` for
`CompassReading.MagneticHeading` — chosen over an accelerometer+magnetometer cross product because
it is OS-fused and avoids reimplementing `SensorManager.getOrientation()`'s fusion math — **and**
`TYPE_MAGNETIC_FIELD` for the raw `MagnetometerReading` vector and its accuracy status, which the
rotation vector does not expose. Azimuth is computed by
`Detail::ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode()`: like the WP7 `Compass`
(MSDN `hh220912`/`hh202974`), it uses the flat-device convention
(`ConvertRotationVectorToMagneticHeadingDegrees()`, the standard quaternion-to-rotation-matrix
relationship and `atan2(R01, R11)`) or, when `IsDeviceInUprightCompassMode()` reports the device
held upright, the upright convention (COMPASS-009). These pure functions are unit-tested for
self-consistency but **not checked against real hardware**.

`CompassReading.TrueHeading` is left exactly equal to `MagneticHeading`: real declination needs a
location source, which does not exist in this codebase. The magnetic-field sensor's
`ASensorVector::status` (numerically identical to `SENSOR_STATUS_*`) maps to `HeadingAccuracy`
(`UNRELIABLE`/`NO_CONTACT`→180°, `LOW`→45°, `MEDIUM`→15°, `HIGH`→5°, a CNA-chosen scale, not an
XNA-documented one) and drives `Compass::Calibrate` for `UNRELIABLE`/`NO_CONTACT` only. Excluding
`LOW` is a choice, not a requirement: a momentarily low reading is common during normal use and
would make `Calibrate` fire too eagerly.

### Android Motion

`Detail::AndroidMotionBackend` uses `TYPE_ROTATION_VECTOR` for `MotionReading.Attitude`, falling
back to `TYPE_GAME_ROTATION_VECTOR` only when the plain rotation vector is unavailable. **Drift
note:** the game rotation vector fuses gyroscope and accelerometer only, with no magnetometer
coupling, so its yaw drifts over time with no correction; the plain rotation vector has a
true-north reference. `Detail::ExtractYawPitchRollFromQuaternion()` derives `Pitch`/`Roll`/`Yaw`
from the same `Quaternion` used for `Attitude.Quaternion`/`RotationMatrix` (via
`Matrix::CreateFromQuaternion()`), which keeps them internally consistent; the formula is verified
by round trip through `Quaternion::CreateFromYawPitchRoll()`.

`TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION` supply `Gravity`/`DeviceAcceleration` — Android's own
already-split virtual sensors, so the bridge performs no manual high-pass/low-pass filtering. Both
report m/s², while `MotionReading` documents both fields in g, so both are divided by the same
`StandardGravity` constant `Accelerometer.cpp` uses. `TYPE_GYROSCOPE` for `DeviceRotationRate` is
the backend's own registration, independent of any live `Gyroscope` instance: Android allows
several listeners per sensor, and `Gyroscope::MaxSensorCount` is per-class (DEVICES-0110). The
backend never performs manual sensor fusion.

The backend also registers `TYPE_MAGNETIC_FIELD` solely to monitor its accuracy status and drive
`Motion::Calibrate` through `IMotionBackend::CalibrationCallback`, with the same
`Detail::ShouldRaiseCalibrateForAccuracyStatus()` policy as the compass (MOTION-011). It is
best-effort: `IsSupported()`/`Start()` never depend on it, and its data is never stored or exposed
(`MotionReading` has no magnetometer field).

### Coordinate-system remap

Motion's Gravity, DeviceAcceleration and DeviceRotationRate use the same optional landscape
conversion as Accelerometer/Gyroscope. CNA-FIX-D22 makes that conversion a proper 90/270-degree
rotation, with the explicit basis and hardware checks in
[the hardware checklist](devices-hardware-checklist.md#1-accelerometer-axis-signorientation).
Android supplies all these vectors in the same natural-device frame; unit conversion precedes
remapping. The shared switch remains enabled by default, and disabling it preserves device-fixed
components. Attitude remains a normalized device/world-frame quaternion, independent of this
CNA convenience. Host tests verify the vector basis, handedness, inverse rotations and units;
physical sensor delivery, calibration and orientation mapping still need hardware validation.

## iOS backend path (designed, not implemented)

### iOS Compass

`CLLocationManager`'s heading APIs (`startUpdatingHeading()`, updates through
`CLLocationManagerDelegate.locationManager(_:didUpdateHeading:)`) deliver a `CLHeading` that maps
almost directly onto `CompassReading`: `magneticHeading` → `MagneticHeading`, `trueHeading` →
`TrueHeading`, `headingAccuracy` → `HeadingAccuracy`, with raw `x`/`y`/`z` magnetometer fields
available. `shouldDisplayHeadingCalibration()` maps to raising `Compass::Calibrate`. Unlike Android,
iOS supplies true heading directly — but needs location permission (below). CoreLocation handles
device-orientation dependence internally, so an iOS backend needs no equivalent of the Android
backend's tilt-mode switch (COMPASS-009).

### iOS Motion

`CMMotionManager.deviceMotion` (`CMDeviceMotion`, delivered through
`startDeviceMotionUpdates(to:withHandler:)`) is a direct field-for-field match:
`.attitude` → `MotionReading.Attitude`, `.gravity` → `.Gravity`, `.userAcceleration` →
`.DeviceAcceleration`, `.rotationRate` → `.DeviceRotationRate`, with no fusion math in the bridge.
Because `CMDeviceMotion` is one already-fused struct with one `timestamp`, an iOS backend uses that
value for both `MotionReading.Timestamp` and `Attitude.Timestamp` and needs neither the Android
timestamp anchoring (MOTION-006) nor the 500 ms stale-sample guard across independently arriving
streams (MOTION-007).

### iOS vibration (`VibrateController`, VIB-004)

iOS vibration should be supported behind `Detail::IVibrateBackend` (`VIB-002`) with a
`CHHapticEngine` (Core Haptics, iOS 13+) implementation:

- **`CHHapticEngine`, not `UIImpactFeedbackGenerator`.** The latter is UIKit's canned tap feedback
  (`.light`/`.medium`/`.heavy`/`.soft`/`.rigid` via `impactOccurred()`) and cannot run for a
  caller-chosen duration. A `.hapticContinuous` `CHHapticEvent` takes a `duration` and a
  `CHHapticEventParameterID.hapticIntensity`, which maps onto `Start(TimeSpan, float)` almost
  one-to-one.
- **Availability:** `CHHapticEngine.capabilitiesForHardware().supportsHaptics` (false on iPad and
  on iPhones without a Taptic Engine) is `IVibrateBackend::IsSupported()`; check it before
  constructing an engine.
- **`StartLeftRight()`:** the Taptic Engine is a single actuator. Fold `largeMotor`/`smallMotor`
  into one intensity with Android's `large*0.6 + small*0.4` weighting
  ([devices-android.md](devices-android.md#vibration-no-native-bridge-exists-and-none-is-needed)),
  so both phone platforms behave alike.
- **Permissions:** none — Core Haptics needs no `Info.plist` entry or runtime prompt.
- **Lifecycle:** an engine can stop on audio-session interruption or backgrounding and must be
  restarted (`engine.start()`) before the next `Start()`; handle `resetHandler`/`stoppedHandler`.
- **No pre-iOS-13 fallback** (`AudioServicesPlaySystemSound`): the minimum iOS version is
  undecided; decide it first.

Until then `VibrateController` has no iOS backend: like any platform without a registered
`IVibrateBackend`, `getIsSupportedProperty()` is `false` and `Start()`/`Stop()`/`StartLeftRight()`
are silent no-ops, covered by `VibrateControllerTests.cpp`'s no-hardware tests.

## Permission and lifecycle notes

- **Android:** `SensorManager`/NDK sensor registration needs no runtime permission prompt on
  current Android; re-verify against the target API level. Manifest declarations are in
  [devices-android.md](devices-android.md).
- **iOS, Compass:** `CLLocationManager`'s heading APIs require **location permission**
  (`requestWhenInUseAuthorization()` or equivalent) even though no coordinate is read; an iOS
  compass cannot skip the location-permission flow because it never calls
  `startUpdatingLocation()`.
- **iOS, Motion:** `CMMotionManager` needs no permission prompt, only `NSMotionUsageDescription`
  in `Info.plist`.
- **Lifecycle, both platforms:** listeners/updates stop when the owning `Compass`/`Motion` is
  disposed or destroyed. A backend reuses `SensorBase<T>`'s disposal machinery (instance-count
  tracking, `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` in
  `PlatformSensorSubsystem.hpp`) rather than inventing a second one.

## Migration plan

These rules governed adding the Android backends and govern any further platform:

1. **No change to any public API.** `Compass`/`Motion`'s constructors,
   `getIsSupportedProperty()`, `Start()`/`Stop()`, `getCurrentValueProperty()` and their
   `CurrentValueChanged` events keep their XNA-facing signatures.
2. **The backend-selection seam lives only inside `Compass`/`Motion`:** a private backend pointer,
   constructed only inside a platform block such as `#if defined(__ANDROID__)` (the
   `#if defined(__APPLE__) && TARGET_OS_IOS` half is unwritten).
3. **A platform without a backend keeps today's exact behavior:** no backend object at all, and
   the permanent `NotSupported`/throws-`SensorFailedException` stub. Every `CompassTests`/
   `MotionTests` stub case passes unmodified on desktop.
4. **Behavior changes only once a platform backend is constructed and its `IsSupported()` returns
   true.**
5. **New tests, not modified tests:** stub-behavior tests stay unchanged; a backend adds its own
   math tests (`AndroidCompassMathTests`, `AndroidMotionMathTests`), fake-backend
   `CompassTests`/`MotionTests` cases and manual checklist sections (§7 Compass, §8 Motion in
   [devices-hardware-checklist.md](devices-hardware-checklist.md)).
