# Microsoft::Devices / Microsoft::Devices::Sensors API coverage

This is the current native device surface and its compatibility boundary, checked against
`modules/devices/include/Microsoft/Devices/` and the corresponding sources/tests on 2026-11-01.
It replaces the original DEVICES-0002–0008 / DEV-API / READINGS campaign log, retained in Git history.
The deleted devices plan is not an active task list.

The normal FNA reference does not supply this Windows Phone sensor implementation. Historical
WP7 classifications below come from the archived Microsoft references recorded by that campaign;
they are not a fresh exhaustive WP7 metadata comparison or measured hardware parity.
For generated runtime-member presence see
[xna-4-runtime-member-coverage.md](xna-4-runtime-member-coverage.md); presence and host tests do
not establish physical sensor correctness.

Public include spellings stay `Microsoft/Devices/...`. Declarations live in `modules/devices/include`,
implementations in `modules/devices/src`, tests in `modules/devices/tests`. The unrelated
`CNA::Devices` layer is documented in [devices-ext.md](devices-ext.md).

## Classification and C++ mapping

- WP7 surface: preserve the reference names, properties, events and behavior.
- CNA extension: marked `CNAEXT` in the Microsoft namespace, including test seams.
- C++ mapping: constructors/destructors, value storage and reference overloads describe the
  native representation; do not treat a C++ destructor as a literal WP7 method.
- Internal: Detail helpers are implementation contracts, even when their headers are available.
  They are not additional game-facing WP7 API.

The historical visibility/operator findings are closed: AccelerometerReadingEventArgs has no
public setters; SensorReadingEventArgs<T> has public setters; reading-specific equality,
formatting and hashing are CNA extensions; TimeBetweenUpdatesChanged is protected and CNAEXT.
There are no open wrong-visibility findings from that historical audit.

## Device environment and vibration

| Type/member | Classification | Current behavior |
|---|---|---|
| DeviceType | WP7 enum | Device, Emulator. |
| Environment.getDeviceTypeProperty() | WP7 property mapped to CNA targets | Mobile target returns Device; desktop/browser returns Emulator. This does not detect whether an Android build is actually running in an emulator. |
| VibrateController.getDefaultProperty() | WP7 | Process singleton; caller does not own/delete it. |
| Start(TimeSpan), Stop() | WP7 | Duration must be in [0, 5 seconds]; invalid values throw ArgumentOutOfRangeException. Unsupported native hardware is a no-op. |
| Start(TimeSpan, float intensity) | CNAEXT | Clamps intensity to [0,1], maps NaN to zero; zero intensity is not an implicit Stop. |
| StartLeftRight(float, float, TimeSpan) | CNAEXT | Same duration/magnitude validation; native-provider motor policy, mutually exclusive with ordinary Start. |
| getIsSupportedProperty(), getDeviceNameProperty() | CNAEXT | Probe selected haptics provider; availability can change. |
| SetBackendForTesting(unique_ptr<Detail::IVibrateBackend>) | CNAEXT | Replaces the singleton's adapter; null restores the platform adapter. |

The platform owns native haptic resources and gamepad exclusion. Phone motor blending,
reconnection and real effect-failure cleanup need the hardware matrix; the extension does not
promise two independent phone motors.

## Shared sensor surface

Accelerometer, Gyroscope, Compass and Motion inherit SensorBase of their reading type.

| Member | Classification / visibility | Current behavior |
|---|---|---|
| getCurrentValueProperty() | WP7, public | Returns a value snapshot; unsupported throws InvalidOperationException. A supported sensor can still have no valid reading. |
| getIsDataValidProperty() | WP7, public | Initially false; published value/validity are updated together under the base mutex. |
| getTimeBetweenUpdatesProperty(), setTimeBetweenUpdatesProperty(TimeSpan) | WP7, public | Default 2 ms; interval changes are stored and forwarded to the implementation. This is a request, not guaranteed native cadence. |
| CurrentValueChanged | WP7, public | Event with SensorReadingEventArgs of the reading type. |
| Start(), Stop() | WP7, public virtual | Implemented by each concrete sensor. |
| Dispose() | WP7, public | Repeated explicit disposal throws ObjectDisposedException; destructors avoid repeating it. |
| Dispose(bool) | Protected virtual implementation hook | Overridden by concrete sensors; not a public overload. |
| TimeBetweenUpdatesChanged | CNAEXT, protected | Derived-class notification for a changed interval. |
| GetTypeName() | CNA type-identity mapping | Concrete sensors use Sharp Runtime's existing GetTypeNameHPP macro. |

Each concrete class caps live instances at ten, with bookkeeping released by disposal.
Accelerometer/Gyroscope apply software dispatch throttling; Compass/Motion forward running rate
changes to their Android source bridges. Native rate rejection and observed cadence remain distinct.

See [devices-event-contract.md](devices-event-contract.md) for snapshot order, thread identity,
reentrancy, lifetime and exception containment, and
[devices-thread-safety.md](devices-thread-safety.md) for concurrent calls/subscriptions.

## Concrete sensors and extensions

| Type | WP7-facing members beyond the shared base | CNA extensions and implementation boundary |
|---|---|---|
| Accelerometer | Constructor, static IsSupported, State, Start/Stop, legacy ReadingChanged | Keyboard-emulation enable/query; synthetic/registration/disposal/diagnostic test hooks. Uses IPlatformSensors; Android/iOS/desktop native target policy, with keyboard emulation as a separate support route. |
| Gyroscope | Constructor, static IsSupported, Start/Stop | State and synthetic/registration/disposal/diagnostic hooks are CNAEXT. Uses IPlatformSensors on Android/iOS/desktop; native browser route excluded. |
| Compass | Constructor, static IsSupported, Start/Stop, Calibrate | State and SetBackendForTesting are CNAEXT. AndroidCompassBackend supplies native readings; other targets have an unsupported implementation. |
| Motion | Constructor, static IsSupported, Start/Stop, Calibrate | State, IsAttitudeNorthReferenced and SetBackendForTesting are CNAEXT. AndroidMotionBackend supplies native readings; other targets have an unsupported implementation. |

Accelerometer.State is historical WP7 API (reference ID ff707531); the other three State properties
are CNAEXT. Accelerometer.ReadingChanged is the retained legacy event (ff707930).
The older table's fixed count of eight Accelerometer test hooks is obsolete; declarations are the
source of truth. Keyboard emulation is covered separately in
[keyboard-device-emulation.md](keyboard-device-emulation.md).

Start errors use AccelerometerFailedException for the legacy accelerometer path and
SensorFailedException for the other concrete implementations, alongside ordinary disposed-state
errors. The archived [SensorBase.Start reference](https://learn.microsoft.com/en-us/previous-versions/windows/apps/hh220889(v=vs.105))
lists a broader WP7 exception surface; this table does not claim CNA reproduces every native
WP7 permission/resource failure.

## Readings and event arguments

The five value readings implement ISensorReading.Timestamp. Each has default and parameterized
native constructors, public const getters, and private setters accessible to its owning sensor.

| Reading | Data properties |
|---|---|
| AccelerometerReading | Acceleration (Vector3), Timestamp |
| GyroscopeReading | RotationRate (Vector3), Timestamp |
| CompassReading | HeadingAccuracy, MagneticHeading, MagnetometerReading (Vector3), Timestamp, TrueHeading |
| MotionReading | Attitude, DeviceAcceleration (Vector3), DeviceRotationRate (Vector3), Gravity (Vector3), Timestamp |
| AttitudeReading | Pitch, Roll, Yaw, Quaternion, RotationMatrix, Timestamp |

All five add CNAEXT operator==/!=, ToString, GetHashCode and GetTypeName. Their field-based
formatting/hashing/equality must not be described as a custom WP7 ValueType implementation.

| Supporting type | Surface |
|---|---|
| ISensorReading | Virtual const Timestamp getter. |
| AccelerometerReadingEventArgs | Default/parameterized native constructors; public X/Y/Z/Timestamp getters, no setters; CNAEXT equality/formatting/hashing/type-name helpers. |
| SensorReadingEventArgs<T> | Default/copy/move reading construction; public const SensorReading getter and copy/move setters. The public setter agrees with the [archived property reference](https://learn.microsoft.com/en-us/previous-versions/hh203225(v=vs.110)). |
| CalibrationEventArgs | Empty event marker plus CNA type-name helper. |
| SensorFailedException | Default/message/message+errorId constructors, ErrorId getter (default zero). |
| AccelerometerFailedException | SensorFailedException subclass with the same three constructor forms. |
| SensorState | NotSupported, Ready, Initializing, NoData, NoPermissions, Disabled. The historical audit's direct WP7 enum-source gap is not resolved by merely inspecting CNA. |

Historical reference IDs retained for the value shapes: AccelerometerReading ff403534,
GyroscopeReading hh220755, CompassReading hh203072, MotionReading hh220685, AttitudeReading hh220667;
legacy event-argument getters ff707568/ff707712/ff708055/ff707430. These identify earlier evidence,
not a claim that every archive page was fetched successfully in this audit.

## Units, coordinate and fusion policy

| Output | Implemented conversion / limitation |
|---|---|
| Accelerometer.Acceleration | Platform m/s² divided by 9.80665 into g; optional Android landscape remap, or keyboard-emulated values. |
| Gyroscope.RotationRate | rad/s; optional Android landscape remap. |
| Compass.MagnetometerReading | Android raw magnetic-field vector in microtesla; no display remap. |
| Compass.MagneticHeading | Normalized rotation quaternion, with flat/upright heading mode selection. The old claim that no tilt handling exists is obsolete. |
| Compass.HeadingAccuracy / Calibrate | Android status maps to 5/15/20/180 degree estimates; unreliable/no-contact can raise calibration. These estimates are CNA policy. |
| Compass.TrueHeading | Equals MagneticHeading; no geographic declination integration. |
| Motion.Attitude | Component-mapped quaternion normalized, invalid/near-zero input replaced by Identity; RotationMatrix and Euler radians derived from it. No display remap of attitude. |
| Motion.Gravity / DeviceAcceleration | Android acceleration divided by standard gravity into g; optional landscape remap. |
| Motion.DeviceRotationRate | Android rad/s; optional landscape remap. |
| Motion source/fusion | Rotation-vector source with game-rotation-vector fallback; the CNAEXT north-reference property reports the choice. Latest-value fusion rejects source timestamp spread over 500 ms, rather than interpolating a measured tight sample queue. |

Since CNA-FIX-D22 the optional landscape remap is a proper rotation (Rotation90 `(-y, x, z)`,
Rotation270 `(y, -x, z)`) rather than a single-axis reflection; physical qualification remains in
the hardware checklist. Host math tests prove the implemented formula, not the physical WP7
coordinate convention.

## Timestamp policy

Public timestamps use wall-clock DateTimeOffset at delivery/publication, not raw platform
monotonic timestamps. This is CNA's approximation of capture time, with no bounded latency guarantee
and no promise of monotonicity if the wall clock changes.

- Accelerometer/Gyroscope stamp a reading during DispatchSensorReading.
- AndroidSensorBridge stamps each Android sample at delivery.
- Compass stamps its fused publication with UtcNow after source freshness checks.
- Motion copies the attitude source timestamp into both MotionReading.Timestamp and nested
  Attitude.Timestamp.

Compass requires each source to be fresh within max(500 ms, five requested intervals).
Motion instead bounds the spread among its latest source timestamps. These are different tests;
the Motion rule alone does not bound absolute age relative to now.

## Verification boundary

Host tests cover value members, exceptions, forwarding, dispatch, fake providers, ownership and
pure Android math. Android-only worker/service paths require separate target execution.
Use [devices-build.md](devices-build.md) for build commands and
[devices-hardware-checklist.md](devices-hardware-checklist.md) with its
[report template](devices_sensor_hardware_qa_template.md) to record physical results.
No new hardware qualification or exhaustive API-completeness claim follows from this cleanup.

Audit verification (2026-11-01): CnaDevicesTests builds in the existing UBSan tree and runs on a
private display: 484 pass, four supported-hardware cases skip, no failures. These results cover
host execution; they do not rerun Android sensor workers on a device.
