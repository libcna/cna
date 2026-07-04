# Microsoft::Devices — Future Native Backend Design (Compass/Motion)

## Purpose and status

**This document is a design sketch only. Nothing in it is implemented, wired into the
build, or scheduled.** `plan_devices_phase9.md` Task P9-8 created this file to
consolidate the native-backend architecture that has been scattered across two prior
plan files (`plan_devices_phase5.md` Tasks P5-8/P5-9's prose field mappings, and
`plan_devices_phase6.md` Task P6-8's `ICompassBackend`/`IMotionBackend` interface
sketch) into one place, and to extend that sketch to cover `IDeviceSensorBackend`,
`IAccelerometerBackend`, and `IGyroscopeBackend` for architectural completeness, plus a
concrete migration plan.

Per the Phase 9 brief that created this file: **do not implement any of this unless
explicitly requested in a future task.** If a future phase decides to build a real
native backend, that phase should update this document as its design evolves rather
than treat this sketch as frozen.

## Why this exists at all

Today, `Accelerometer` and `Gyroscope` are backed by a real, working, cross-platform
implementation (`Detail::SdlSensorSubsystem<TSensor>`, built on SDL3's
`SDL_Sensor`/`SDL_AddEventWatch` API) — see `docs/devices-build.md` for what has
actually been built and tested. **`Compass` and `Motion` are not.** Both are permanent,
honest `SensorState::NotSupported` stubs: `getIsSupportedProperty()` hardcodes
`return false;`, and `Start()` unconditionally throws `SensorFailedException`. This is
deliberate — SDL3 has no compass/magnetometer or fused-orientation ("motion") API, only
raw accelerometer/gyroscope sensors — and has been re-confirmed unchanged across
Phases 6, 7, 8, and 9's audits. **Do not fake `Compass` or `Motion` from
accelerometer/gyroscope data**; this document exists precisely so that a future,
correctly-scoped task can give them a real backend instead.

`System.Device.Location` (GPS) is explicitly **out of scope** for this document and for
`Microsoft::Devices::Sensors` entirely — it belongs to a future, separate
`System.Device.Location` plan, referenced here only where a native Compass backend's
true-heading calculation needs geomagnetic-declination data that a location layer would
normally supply (see the Android Compass section below).

---

## Interface sketch

The shape below extends `plan_devices_phase6.md` Task P6-8's `ICompassBackend`/
`IMotionBackend` sketch with a common base and the two sensors that already have a real
backend, purely so the architecture reads as one consistent design rather than two
unrelated one-off interfaces. **None of this is compiled or wired in today.**

```cpp
// Sketch only -- not compiled, not wired into any .hpp/.cpp today.
namespace Microsoft::Devices::Sensors::Detail
{
    // Common shape shared by every per-sensor backend interface below. Pull-model
    // (a reading is fetched on demand) rather than push, matching this project's
    // existing Detail::SdlSensorSubsystem<TSensor> dispatch shape -- see "Why this
    // shape and not more" at the end of this section.
    class IDeviceSensorBackend
    {
    public:
        virtual ~IDeviceSensorBackend() = default;
        [[nodiscard]] virtual bool IsSupported() = 0;
        virtual void Start() = 0;
        virtual void Stop() = 0;
    };

    // Documentation only, for architectural symmetry: Accelerometer/Gyroscope
    // already have a real, working SDL3 backend (Detail::SdlSensorSubsystem<TSensor>)
    // and do NOT need one of these. No CNA code should ever instantiate this type --
    // see "SDL backend scope, unchanged" below.
    class IAccelerometerBackend : public IDeviceSensorBackend
    {
    public:
        [[nodiscard]] virtual AccelerometerReading GetLatestReading() = 0;
    };

    class IGyroscopeBackend : public IDeviceSensorBackend
    {
    public:
        [[nodiscard]] virtual GyroscopeReading GetLatestReading() = 0;
    };

    // One instance per platform (Android JNI bridge / iOS CLLocationManager heading
    // wrapper), constructed only once a real native backend is scoped and
    // implemented. Until then, Compass has no backend_ member at all and
    // getIsSupportedProperty() stays a hardcoded `return false;`.
    class ICompassBackend : public IDeviceSensorBackend
    {
    public:
        [[nodiscard]] virtual CompassReading GetLatestReading() = 0;
    };

    // Same shape, mirroring MotionReading's Attitude/Gravity/DeviceAcceleration/
    // DeviceRotationRate fields (see the Android/iOS sections below for the
    // field-for-field native source mapping).
    class IMotionBackend : public IDeviceSensorBackend
    {
    public:
        [[nodiscard]] virtual MotionReading GetLatestReading() = 0;
    };
}
```

**Why this shape and not more:** mirrors the existing
`Detail::SdlSensorSubsystem<TSensor>` pull-model (a reading is fetched, not pushed via a
second callback mechanism) so a future implementation could plausibly reuse
`SensorBase<T>`'s existing `setCurrentValueProperty()` dispatch path from inside
`Start()`'s own polling/callback glue, rather than inventing a parallel event system.
Deliberately not sketching constructor/DI wiring, error handling, or thread-safety
details — those depend on decisions (polling vs. push, which thread delivers updates)
that belong to the actual implementation task, not this sketch.

### SDL backend scope, unchanged

To be explicit, since this document could otherwise be misread as proposing a broader
rewrite: **the SDL backend remains accelerometer, gyroscope, and vibration only.**
`IAccelerometerBackend`/`IGyroscopeBackend` above exist purely for interface symmetry
with `ICompassBackend`/`IMotionBackend` in this document — no task should actually
create an SDL-backed implementation of them, retire `Detail::SdlSensorSubsystem`, or
otherwise touch `Accelerometer`/`Gyroscope`'s working implementation as part of adding a
Compass/Motion native backend.

---

## Android backend path

Builds directly on `plan_devices_phase5.md`'s existing Compass-Android/Motion-Android
subsections (Tasks P5-8/P5-9); reproduced and consolidated here rather than duplicated
with drift.

**Compass:** a JNI/Kotlin bridge into `android.hardware.SensorManager`, registering
listeners for `TYPE_MAGNETIC_FIELD` plus `TYPE_ACCELEROMETER` (optionally
`TYPE_ROTATION_VECTOR` instead, to avoid doing the sensor-fusion math in the bridge
itself). `SensorManager.getOrientation()` (or the rotation-vector output directly) maps
to `CompassReading.MagneticHeading`. `CompassReading.TrueHeading` additionally needs the
geomagnetic declination at the device's current location via `GeomagneticField` — this
ties the Compass backend loosely to a location source, which is exactly why true heading
depends on the *separate, not-yet-implemented* `System.Device.Location` plan rather than
being computed here in isolation. `SensorManager`'s `SENSOR_STATUS_*` accuracy constants
(`SENSOR_STATUS_UNRELIABLE`, `_LOW`, `_MEDIUM`, `_HIGH`) map to
`CompassReading.HeadingAccuracy` and drive when to raise `Compass::Calibrate`.

**Motion:** `TYPE_ROTATION_VECTOR` or `TYPE_GAME_ROTATION_VECTOR` (the latter avoids
magnetometer coupling entirely, at the cost of no true-north reference) for
`MotionReading.Attitude`. `TYPE_GRAVITY` for `MotionReading.Gravity`,
`TYPE_LINEAR_ACCELERATION` for `MotionReading.DeviceAcceleration` — Android already
exposes these as separate virtual sensors that split gravity from user acceleration,
matching XNA's own split of the two fields, so no manual high-pass/low-pass filtering is
needed in the bridge. `TYPE_GYROSCOPE` for `MotionReading.DeviceRotationRate`.

---

## iOS backend path

Builds directly on `plan_devices_phase5.md`'s existing Compass-iOS/Motion-iOS
subsections (Tasks P5-8/P5-9).

**Compass:** `CLLocationManager`'s heading APIs (`startUpdatingHeading()`, with updates
delivered through `CLLocationManagerDelegate.locationManager(_:didUpdateHeading:)`)
deliver a `CLHeading` object that maps almost directly onto `CompassReading`:
`magneticHeading` → `MagneticHeading`, `trueHeading` → `TrueHeading`,
`headingAccuracy` → `HeadingAccuracy`, and the raw `x`/`y`/`z` magnetometer fields are
available if a lower-level reading is ever needed. `shouldDisplayHeadingCalibration()`
on the same delegate maps to raising `Compass::Calibrate`. Unlike Android, iOS supplies
true heading directly (no separate location/declination step needed in the bridge) —
but see the permission note below, which is the opposite trade-off.

**Motion:** `CMMotionManager.deviceMotion` (a `CMDeviceMotion` struct, delivered via
`startDeviceMotionUpdates(to:withHandler:)`) is close to a direct field-for-field match:
`.attitude` (`CMAttitude`) → `MotionReading.Attitude`, `.gravity` → `.Gravity`,
`.userAcceleration` → `.DeviceAcceleration`, `.rotationRate` → `.DeviceRotationRate`.
This is the cleanest of the four native-backend mappings in this document — CoreMotion
already computes and separates every field XNA's `MotionReading` expects, with no fusion
math needed in the bridge at all.

---

## Permission and lifecycle notes

- **Android:** runtime sensor permissions are not required for `SensorManager` listeners
  in the general case (unlike location, camera, or microphone) — `SensorManager`
  registration itself does not gate on a runtime permission prompt on current Android
  versions. This needs to be re-verified against the target API level at actual
  implementation time, not assumed from this document.
- **iOS, Compass specifically:** `CLLocationManager`'s heading APIs require **location
  permission authorization** (`CLLocationManager.requestWhenInUseAuthorization()` or
  equivalent) even though only heading — not a coordinate — is being read. This is an
  iOS-specific quirk already flagged in `plan_devices_phase5.md`'s original prose plan
  and repeated here because it is easy to miss: a future Compass-iOS implementation
  cannot skip the location-permission flow just because it never calls
  `startUpdatingLocation()`.
- **iOS, Motion:** `CMMotionManager` does not require a separate permission prompt on
  iOS today, only `NSMotionUsageDescription` in the app's `Info.plist`.
- **Lifecycle, both platforms:** listeners/updates must stop when the owning
  `Compass`/`Motion` instance is `Dispose()`d or destroyed, mirroring the discipline
  already established for `Accelerometer`/`Gyroscope` in
  `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` (instance-count
  tracking, `ClaimDisposalOnce()`/`WaitForDisposalToComplete()`). A native backend
  implementation should reuse `SensorBase<T>`'s existing disposal machinery rather than
  invent a second one — this is a strong architectural constraint, not a suggestion,
  given how much Phase 7/Phase 8 hardening went into that machinery's correctness.

---

## Migration plan (must not break the existing SDL implementation)

1. **No change to any existing public API.** `Compass`/`Motion`'s constructors,
   `getIsSupportedProperty()`, `Start()`/`Stop()`, `getCurrentValueProperty()`, and
   their `CurrentValueChanged` events keep their exact current XNA-facing signatures.
   `Accelerometer`/`Gyroscope`'s existing SDL-backed implementation is untouched by this
   migration entirely — see "SDL backend scope, unchanged" above.
2. **Introduce a backend-selection seam only inside `Compass`/`Motion`.** A future
   implementation task would add a private `std::unique_ptr<Detail::ICompassBackend>`
   (respectively `IMotionBackend`) member, selected at construction time by a
   compile-time platform switch (`#if defined(__ANDROID__)` / `#if defined(__APPLE__)
   && TARGET_OS_IOS`) — analogous to this project's existing `CNA_GRAPHICS_BACKEND`
   compile-time selection pattern, but scoped to these two classes only, not a
   project-wide macro.
3. **Default (desktop/SDL, and any platform without a native backend implemented yet)
   keeps today's exact behavior**: no backend instance is constructed,
   `getIsSupportedProperty()` stays a hardcoded `return false;`, `Start()` stays an
   unconditional `throw SensorFailedException`. This is the honest fallback for every
   platform this document does not (yet) give a real backend to — including desktop
   Linux, where this document's own author has no compass/motion hardware to test
   against in the first place.
4. **Only once a platform backend is constructed and `IsSupported()` returns true**
   would `getIsSupportedProperty()`/`Start()` change behavior for that platform's build.
   This keeps every currently-passing `CompassTests`/`MotionTests` case
   (`GetIsSupportedPropertyDoesNotCrash`/`IsFalse`, `StartThrowsSensorFailedException`)
   correct and unchanged for every platform that has not yet received an implemented
   backend.
5. **New tests, not modified tests.** Existing Compass/Motion tests assert the
   `NotSupported` stub behavior for the platforms this document doesn't implement
   anything for; they should keep passing unmodified. A native-backend implementation
   task would add new, platform-gated tests (or hardware-only manual checklist items in
   `docs/devices-hardware-checklist.md`) rather than rewrite the existing ones.
6. **Order of implementation, if ever scoped:** iOS Motion first (cleanest mapping, no
   fusion math, single permission caveat already known), then iOS Compass (needs the
   location-permission flow), then Android Motion, then Android Compass (needs the
   `System.Device.Location` declination dependency, so realistically blocked on that
   separate plan existing first). This ordering is a suggestion for whichever future
   task picks this up, not a commitment.

---

## Explicitly not part of this document

- No `.hpp`/`.cpp` files were added or modified for this design. `Compass.hpp`/`.cpp`
  and `Motion.hpp`/`.cpp` are unchanged.
- No GPS/location code. `System.Device.Location` remains a separate, future,
  unstarted plan.
- No timeline or task numbers are committed here — this document only becomes
  actionable when a future phase explicitly scopes an implementation task against it.
