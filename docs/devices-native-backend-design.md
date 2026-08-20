# Microsoft::Devices — Native Backend Design (Compass/Motion)

## Purpose and status

**PLAT-108 update (2026-08-13):** `Accelerometer`/`Gyroscope` no longer own a native backend.
They consume `IPlatformSensors` through `Detail::PlatformSensorSubsystem<TSensor>`; native sensor
sessions are implemented by the selected platform. The historical interface sketches below remain
useful context for Compass/Motion, but their statements that the old sensor subsystem must remain
unchanged are superseded by this update.

**Updated 2026-07-05 (`plans/plan_devices.md` Phases 6-8): the Android backend described
below is now implemented, not just sketched.** `Detail::AndroidSensorBridge`
(`include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`),
`Detail::ICompassBackend`/`Detail::AndroidCompassBackend`, and
`Detail::IMotionBackend`/`Detail::AndroidMotionBackend` are real, compiling,
cross-compile-verified (Android NDK r30, arm64-v8a, API 24, `llvm-nm`-confirmed) code,
wired into `Compass`/`Motion` via a compile-time `#if defined(__ANDROID__)` switch
exactly as this document's original migration plan prescribed. This document is kept
as the architectural reference (why these specific sensor choices were made, what
remains iOS-only-sketch, what limitations are accepted) — sections below are updated
in place to say what is actually implemented vs. still just designed, rather than
being rewritten from scratch.

Original history: `plans/plan_devices_phase9.md` Task P9-8 created this file to consolidate
the native-backend architecture that had been scattered across two prior plan files
(`plans/plan_devices_phase5.md` Tasks P5-8/P5-9's prose field mappings, and
`plans/plan_devices_phase6.md` Task P6-8's `ICompassBackend`/`IMotionBackend` interface
sketch) into one place, extended to cover `IDeviceSensorBackend`,
`IAccelerometerBackend`, and `IGyroscopeBackend` for architectural completeness, plus a
migration plan — that migration plan has now been executed for Android (see
"Migration plan" section below, updated with actual results). **The iOS sections
remain unimplemented sketches** — no Apple toolchain exists in this environment
(`plans/plan_devices.md` Task DEVICES-0131), so no `.mm`/Swift code was written; a future
phase should update those sections as its own design evolves, following the same
"update in place, don't treat as frozen" convention this document has followed since
Phase 9.

## Why this exists at all

Today, `Accelerometer` and `Gyroscope` are backed by a real, working, cross-platform
implementation (`Detail::PlatformSensorSubsystem<TSensor>` plus `IPlatformSensors`) — see
`docs/devices-build.md` for what has actually been built and tested. At the time this document was
originally written, **`Compass` and `Motion` were not.** Both were permanent,
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

**Update (2026-07-05): `ICompassBackend`/`IMotionBackend` below are now real, compiled
headers** — `include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp` and
`include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp`. `IDeviceSensorBackend`/
`IAccelerometerBackend`/`IGyroscopeBackend` below remain sketch-only, unimplemented,
exactly as originally intended (see "SDL backend scope, unchanged" — still true).

**Update (2026-07-16, Task MOTION-011): the two interfaces' shapes are now identical.**
`IMotionBackend::Start()` originally had no `CalibrationCallback` parameter at all,
since `AndroidMotionBackend` never detected a calibration-needed condition itself — an
independent audit (`../audit_devices.md`, `DEV-AUD-002`) confirmed this left
`Motion::Calibrate` a permanent no-op with no possible producer, despite being a real,
documented WP7 event (archived MSDN `hh239189(v=vs.105)`). `IMotionBackend::Start()`
now takes a `CalibrationCallback onCalibrationNeeded` in addition to `ReadingCallback`,
matching `ICompassBackend::Start()` exactly — see the Android backend section below for
how `AndroidMotionBackend` now drives it.

The shape below extends `plans/plan_devices_phase6.md` Task P6-8's `ICompassBackend`/
`IMotionBackend` sketch with a common base and the two sensors that already have a real
backend, purely so the architecture reads as one consistent design rather than two
unrelated one-off interfaces. **`IDeviceSensorBackend`/`IAccelerometerBackend`/
`IGyroscopeBackend` are still not compiled or wired in — only `ICompassBackend`/
`IMotionBackend` (below) were promoted to real code.**

```cpp
// Sketch only -- not compiled, not wired into any .hpp/.cpp today.
namespace Microsoft::Devices::Sensors::Detail
{
    // Common shape shared by every per-sensor backend interface below. Pull-model
    // (a reading is fetched on demand) rather than push, matching this project's
    // existing Detail::PlatformSensorSubsystem<TSensor> dispatch shape -- see "Why this
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
    // already use the platform sensor contract and do NOT need one of these.
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
`Detail::PlatformSensorSubsystem<TSensor>` session/dispatch model so a future implementation can
reuse `SensorBase<T>`'s current-value dispatch path rather than inventing a parallel event system.
Deliberately not sketching constructor/DI wiring, error handling, or thread-safety
details — those depend on decisions (polling vs. push, which thread delivers updates)
that belong to the actual implementation task, not this sketch.

### Platform sensor scope (updated by PLAT-108)

`IAccelerometerBackend`/`IGyroscopeBackend` above remain documentation-only. PLAT-108 retired the
old concrete native subsystem and moved accelerometer, gyroscope and vibration behind
`IPlatformSensors`/`IPlatformHaptics`; Compass/Motion keep their dedicated Android backends because
their fused/magnetometer data is outside the current platform sensor vocabulary.

---

## Android backend path — IMPLEMENTED (2026-07-05, `plans/plan_devices.md` Phases 6-8)

Builds directly on `plans/plan_devices_phase5.md`'s existing Compass-Android/Motion-Android
subsections (Tasks P5-8/P5-9); reproduced and consolidated here rather than duplicated
with drift. **As-implemented, this deviated from the sketch in one important way**:
no JNI/Kotlin bridge was needed at all — the NDK's own `<android/sensor.h>`/
`<android/looper.h>` (`ASensorManager`/`ASensorEventQueue`/`ALooper`) expose everything
needed directly from C++, confirmed by reading the NDK sysroot headers before writing
any code (`plans/plan_devices.md` Task DEVICES-0073). `Detail::AndroidSensorBridge`
(Phase 6) is the shared bridge both `Compass` and `Motion` build on: one instance per
Android sensor type, owning a dedicated background thread that prepares and polls its
own `ALooper` (thread-affine — can't be pumped from an arbitrary caller thread) and
delivers samples via callback, stamped with real wall-clock time (never
`ASensorEvent::timestamp`, which is monotonic boot-time).

**Compass (`Detail::AndroidCompassBackend`):** registers **both** `TYPE_ROTATION_VECTOR`
(chosen over the sketch's `TYPE_ACCELEROMETER`+`TYPE_MAGNETIC_FIELD` cross-product
approach — OS-fused, avoids reimplementing `SensorManager.getOrientation()`'s sensor-fusion
math in this bridge) for `CompassReading.MagneticHeading`, **and** `TYPE_MAGNETIC_FIELD`
separately for the raw `MagnetometerReading` vector and accuracy status (the rotation
vector alone doesn't expose either in the needed form). Azimuth is computed by
`Detail::ConvertRotationVectorToMagneticHeadingDegrees()` — a pure function building the
standard quaternion-to-rotation-matrix relationship, then `atan2(R01, R11)` — unit-tested
for self-consistency but **never checked against real hardware**. `CompassReading.TrueHeading`
is left exactly equal to `MagneticHeading` (the sketch's original plan — real declination
needs `System.Device.Location`, still not implemented, still out of scope). The magnetic
field sensor's `ASensorVector::status` (numerically identical to `SENSOR_STATUS_*`) maps
to `HeadingAccuracy` (`UNRELIABLE`/`NO_CONTACT`→180°, `LOW`→45°, `MEDIUM`→15°, `HIGH`→5°,
a CNA-chosen scale, not an XNA-documented one) and drives `Compass::Calibrate`
(`UNRELIABLE`/`NO_CONTACT` only — `LOW` deliberately excluded, would fire too eagerly
during normal use).

**Motion (`Detail::AndroidMotionBackend`):** `TYPE_ROTATION_VECTOR`, falling back to
`TYPE_GAME_ROTATION_VECTOR` only if the plain rotation vector is unavailable on the
device, for `MotionReading.Attitude` — `Detail::ExtractYawPitchRollFromQuaternion()`
derives `Pitch`/`Roll`/`Yaw` from the exact same `Quaternion` used for
`Attitude.Quaternion`/`RotationMatrix` (via CNA's own `Matrix::CreateFromQuaternion()`),
guaranteeing internal consistency; the formula itself was derived and numerically
verified via round-trip through CNA's own already-tested
`Quaternion::CreateFromYawPitchRoll()` before being written into C++. `TYPE_GRAVITY`/
`TYPE_LINEAR_ACCELERATION` for `Gravity`/`DeviceAcceleration` (Android's own already-split
virtual sensors, no manual filtering needed, exactly as sketched) — **one real bug found
and fixed while implementing**: both report m/s², but `MotionReading.hpp` documents both
fields as "in g," so both are now divided by the same `StandardGravity` constant
`Accelerometer.cpp` already uses. `TYPE_GYROSCOPE` for `DeviceRotationRate`, registered
independently of any live `Gyroscope` C++ instance (confirmed no conflict — Android
supports multiple listeners per sensor, and `Gyroscope::MaxSensorCount`/`instanceCount_`
tracking is per-class). **Task MOTION-011 (2026-07-16):** also independently registers
`TYPE_MAGNETIC_FIELD` — purely to monitor its accuracy status and drive
`Motion::Calibrate` via `IMotionBackend::CalibrationCallback`, reusing
`Detail::ShouldRaiseCalibrateForAccuracyStatus()`, the exact same policy
`AndroidCompassBackend` already uses. Best-effort and optional: unlike the four bridges
above, `IsSupported()`/`Start()`'s overall success never depends on this sensor being
available, and its data is never stored or exposed through `MotionReading` (which has no
magnetometer field) — this bridge exists solely to make the previously-permanent
`Motion::Calibrate` no-op (`../audit_devices.md`, `DEV-AUD-002`) fire under a real
condition.

**Coordinate-system remap status (Task DEVICES-0111 → `MOTION-012`, resolved 2026-07-16):**
`Motion`'s `Gravity`/`DeviceAcceleration`/`DeviceRotationRate` are now run through
`Detail::ConvertAndroidPortraitToXnaLandscape()` (respecting
`Detail::IsAndroidLandscapeRemapEnabled()`), the same remap `Accelerometer`/`Gyroscope`
already apply — confirmed correct via Android's own public developer documentation
(`developer.android.com/guide/topics/sensors/sensors_motion`/`sensors_overview`: the
gravity, linear-acceleration, and gyroscope sensors all explicitly share the
accelerometer's own coordinate system, which is fixed to the device's natural
orientation and never adjusted by the OS for display rotation), not by guessing or by
analogy alone — an independent audit (`../audit_devices.md`, `DEV-AUD-003`) prompted
closing this out rather than leaving it open. **What remains unverified is the remap's
sign/axis correctness on real hardware** (the same still-open gap `Accelerometer`/
`Gyroscope` themselves carry, `docs/devices-hardware-checklist.md` §1/§2/§8), not
whether a remap should exist at all. The `Attitude` quaternion mapping
(`ConvertRotationVectorToXnaQuaternion()`) remains, for a different reason (a
quaternion is not a plain vector — the same sign-flip logic does not apply), a direct,
unremapped passthrough — still `MOTION-002`'s own open question, explicitly out of
scope for `MOTION-012`.

---

## iOS backend path

Builds directly on `plans/plan_devices_phase5.md`'s existing Compass-iOS/Motion-iOS
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

**Re-confirmed 2026-07-06 (Task COMPASS-007):** this plan still holds; no Apple
toolchain exists in this environment to implement or compile it (`docs/devices-build.md`
Section 5). One addition, cross-referencing findings from this session's fresh MSDN
research (`COMPASS-001`/`COMPASS-009`): the real WP7 `Compass` documents switching
which raw axis it reads based on the device's physical tilt (upright vs. flat) —
`CLLocationManager`'s `CLHeading` API has no equivalent concept to replicate at all;
`magneticHeading`/`trueHeading` are simply delivered as whichever value CoreLocation
itself has already computed, with Apple's own framework handling any device-orientation
dependence internally. A future iOS `Compass` backend would **not** need its own
version of `COMPASS-009`'s tilt-mode switch — this is specific to the Android NDK
rotation-vector-based implementation CNA built by hand, not something an iOS backend
built on `CLLocationManager` would ever need to reimplement.

**Motion:** `CMMotionManager.deviceMotion` (a `CMDeviceMotion` struct, delivered via
`startDeviceMotionUpdates(to:withHandler:)`) is close to a direct field-for-field match:
`.attitude` (`CMAttitude`) → `MotionReading.Attitude`, `.gravity` → `.Gravity`,
`.userAcceleration` → `.DeviceAcceleration`, `.rotationRate` → `.DeviceRotationRate`.
This is the cleanest of the four native-backend mappings in this document — CoreMotion
already computes and separates every field XNA's `MotionReading` expects, with no fusion
math needed in the bridge at all.

**Re-confirmed 2026-07-06 (Task MOTION-009):** this plan still holds; no Apple
toolchain exists in this environment to implement or compile it
(`docs/devices-build.md` Section 5). Two cross-references from this session's Android
work, both concluding a future iOS backend needs **neither** of the corresponding
Android-only fixes:
- **`MOTION-006`'s timestamp-consistency fix** (`MotionReading.Timestamp` anchored to
  its own nested `Attitude.Timestamp`, since Android fuses four independently-arriving
  sensor streams with their own timestamps) has no iOS equivalent problem to fix —
  `CMDeviceMotion` is a single, already-fused struct with its own one `timestamp`
  property (`CFTimeInterval`), so a future iOS backend would simply use that one value
  for both `MotionReading.Timestamp` and `Attitude.Timestamp` — they could never
  diverge in the first place, by construction.
- **`MOTION-007`'s stale-sample-fusion guard** (a 500ms max-age window across four
  independently-timestamped Android sensor streams) is entirely an artifact of
  Android's architecture (five separate `AndroidSensorBridge` instances feeding one
  fused reading) — `CMDeviceMotion` has nothing to be stale relative to, since it is
  never assembled from separately-arriving pieces in the bridge at all. A future iOS
  `IMotionBackend` would need no equivalent staleness check.

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
  iOS-specific quirk already flagged in `plans/plan_devices_phase5.md`'s original prose plan
  and repeated here because it is easy to miss: a future Compass-iOS implementation
  cannot skip the location-permission flow just because it never calls
  `startUpdatingLocation()`.
- **iOS, Motion:** `CMMotionManager` does not require a separate permission prompt on
  iOS today, only `NSMotionUsageDescription` in the app's `Info.plist`.
- **Lifecycle, both platforms:** listeners/updates must stop when the owning
  `Compass`/`Motion` instance is `Dispose()`d or destroyed, mirroring the discipline
  already established for `Accelerometer`/`Gyroscope` in
  `include/Microsoft/Devices/Sensors/Detail/PlatformSensorSubsystem.hpp` (instance-count
  tracking, `ClaimDisposalOnce()`/`WaitForDisposalToComplete()`). A native backend
  implementation should reuse `SensorBase<T>`'s existing disposal machinery rather than
  invent a second one — this is a strong architectural constraint, not a suggestion,
  given how much Phase 7/Phase 8 hardening went into that machinery's correctness.

---

## Migration plan (must not break the existing SDL implementation) — EXECUTED for Android

1. **No change to any existing public API. — Confirmed true, verified by re-running
   tests, not just asserted.** `Compass`/`Motion`'s constructors, `getIsSupportedProperty()`,
   `Start()`/`Stop()`, `getCurrentValueProperty()`, and their `CurrentValueChanged` events
   kept their exact current XNA-facing signatures. `Accelerometer`/`Gyroscope`'s existing
   SDL-backed implementation was untouched — see "SDL backend scope, unchanged" above.
2. **Backend-selection seam only inside `Compass`/`Motion` — done exactly as planned.**
   Each class gained a private `std::unique_ptr<Detail::ICompassBackend>` (respectively
   `IMotionBackend`) member, constructed only inside `#if defined(__ANDROID__)`
   (the `#if defined(__APPLE__) && TARGET_OS_IOS` half remains unwritten — no Apple
   toolchain in this environment).
3. **Default (desktop/SDL) keeps today's exact behavior — confirmed, not assumed.**
   Every pre-existing `CompassTests`/`MotionTests` case still passes unmodified on the
   non-Android desktop build (re-run, not just read, after every change).
4. **Behavior only changes once a platform backend is constructed and `IsSupported()`
   returns true — confirmed on Android** (cross-compiled, `llvm-nm`-verified, and
   actually run on an emulator — `plans/plan_devices.md` Phase 9 — where the real backend's
   `IsSupported()`/`Start()` genuinely execute, not just the stub path).
5. **New tests, not modified tests — done.** All existing Compass/Motion stub-behavior
   tests are unmodified; new tests added: 11 `AndroidCompassMathTests` + 6 fake-backend
   `CompassTests` (Phase 7), 9 `AndroidMotionMathTests` + 5 fake-backend `MotionTests`
   (Phase 8), plus new manual hardware-checklist sections (§7 Compass, §8 Motion).
6. **Order of implementation — Android went first, not iOS, deviating from this
   plan's own suggested order.** The original suggestion (iOS Motion first) assumed iOS
   would be reachable before Android; in practice no Apple toolchain ever existed in this
   environment (confirmed at every phase, including `plans/plan_devices.md` Task DEVICES-0131),
   while Android's NDK was already available and, this session, so was a working emulator
   (`/dev/kvm`) — so Android Compass and Motion were both implemented and Android
   Compass's `System.Device.Location`-blocked true-heading limitation was accepted as a
   documented gap (not a blocker) rather than waited on. iOS remains entirely unstarted.

---

## Explicitly not part of this document

- Android `.hpp`/`.cpp` files **were** added — see "Android backend path" above for the
  full list. `Compass.hpp`/`.cpp` and `Motion.hpp`/`.cpp` **were** modified (the
  backend-selection seam, step 2 above) — no longer unchanged, by design, per this
  document's own migration plan.
- No GPS/location code. `System.Device.Location` remains a separate, future,
  unstarted plan.
- No timeline or task numbers are committed here — this document only becomes
  actionable when a future phase explicitly scopes an implementation task against it.
