# `Microsoft::Devices` / `Microsoft::Devices::Sensors` — API Coverage

A standalone, per-member reference for the XNA 4.0 / Windows Phone 7 API surface in this
namespace, extracted from `plan_devices.md`'s Phase 0 audit matrices (Tasks
DEVICES-0002–0008) and updated through Phase 8. This is a flat lookup table — for the
prose history of *how* each row got here, see `AUDIT.md`'s `Microsoft::Devices::Sensors`
section; for architecture, see `docs/devices-native-backend-design.md`.

**Re-verified 2026-07-06 (`DEV-API-001`)** by reading every public header in scope
(`include/Microsoft/Devices/**/*.hpp`, excluding `Detail/`) directly, rather than trusting
this file's own prior content — see "DEV-API-001 verification result" at the bottom for
what changed and what did not.

**Confidence legend:** High = confirmed against an archived MSDN "previous-versions" page.
Medium = cross-checked only against MonoGame or inferred from consistent usage, no direct
MSDN page found.

**Real/NOXNA legend:** `Real` = strict XNA 4.0/WP7 API, name and shape must match exactly.
`WP7-legacy` = real WP7 API but superseded/deprecated in-spec (e.g. `ReadingChanged`).
`NOXNA` = CNA-only extension, must be tagged `NOXNA` on the public declaration.
`Internal-only` = implementation detail, must never appear in a public header at all (see
"SDL/internal-only internals" below).

**Missing/Extra/Wrong-signature flags** (`DEV-API-001`'s required three-way distinction,
applied inline in the tables below where relevant): **Missing** = present in the real
XNA/WP7 API but absent here. **Extra-unmarked** = present here, not real XNA/WP7 API, and
*not* tagged `NOXNA` — the actual bug pattern this distinction exists to catch. **Wrong
signature/visibility** = the member exists and is real, but its C++ shape (access level,
parameter types, overload set) may not match the real API — flagged when unverified, not
assumed correct by default. This pass found **zero Missing and zero Extra-unmarked**
members; two **Wrong-visibility (unverified)** findings are recorded under "Flagged
findings" below.

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
| `~VibrateController()` | Real (public, untagged) | — | Real WP7 has no explicit destructor concept (fire-and-forget static-shaped design in C#); C++ requires one for the singleton's static-lifetime cleanup. Deliberately **not** tagged `NOXNA` — documented in its own doc comment as "NOXNA in spirit" but not a new callable public API surface a game would ever invoke directly. Not a Missing/Extra finding: every other class in this namespace has an equivalent untagged destructor (see "Cross-cutting members" below), this is a project-wide, not Devices-specific, C++/C# lifetime-model gap. |

## `Microsoft::Devices::Sensors::SensorBase<TSensorReading>`

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| `getCurrentValueProperty()` | Real | High | Throws `InvalidOperationException` if unsupported; does not throw if supported-but-no-reading-yet |
| `getIsDataValidProperty()` | Real | High | Defaults `false` |
| `getTimeBetweenUpdatesProperty()`/`set...` | Real | High | Default 2ms. **Fixed 2026-07-06 (`SENSORBASE-001`/`ACCEL-005`/`GYRO-004`):** now really throttles `Accelerometer`/`Gyroscope` dispatch (`SensorBase<T>::ShouldAcceptUpdateAt()`, called from each class's `ProcessSensorUpdateEvent()`). **Still stored/observable only for `Compass`/`Motion`'s Android backend** — applied once at `Start()` time, not while running (`ANDROID-BRIDGE-002`, open). |
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

## Cross-cutting members — reading structs (`DEV-API-001`, added 2026-07-06)

`AccelerometerReading`/`GyroscopeReading`/`CompassReading`/`MotionReading`/
`AttitudeReading` and `AccelerometerReadingEventArgs` each share this identical member
shape — listed once here rather than repeated in every per-struct table above/below.
(`CalibrationEventArgs` does **not** — it is a genuinely empty marker class per the real
WP7 API, with only a constructor and `GetTypeName()`; already fully covered by its own
row in "Exceptions / Enums" below.)

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| Parameterless + parameterized constructors | Real | High | Two ctors each: default (zero/identity values) and one taking every data member |
| `get<Field>Property()` (per field) | Real | High | Public getters, `const` reference or by-value depending on field type |
| `set<Field>Property()` (per field) | Real (visibility unverified) | High (member is real) / unverified (visibility) | **`AccelerometerReading`/`GyroscopeReading`/`CompassReading`/`MotionReading`/`AttitudeReading`:** `private` + `friend class <owning sensor>` (Task P3-2), matching the real API's `internal set`. **`AccelerometerReadingEventArgs`: fully `public`** — see "Flagged findings" below, this is the one confirmed shape difference this pass found. |
| `operator==`/`operator!=` | Real | High | Field-by-field equality |
| `ToString()` | Real | High | Matches XNA's conventional `"{Field:value ...}"` format |
| `GetHashCode()` | Real | High | Hash derived from every data member; consistent for equal instances |
| `GetTypeName()` | `NOXNA` | High | Hand-declared per struct as `NOXNA [[nodiscard]] std::string GetTypeName() const;` (not the `GetTypeNameHPP()` macro — these don't inherit `System::Object` polymorphically the way the four sensor classes do) |

## `Microsoft::Devices::Sensors::Motion`

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| Constructor | Real | High | 10-instance cap; does **not** require a live `Accelerometer`/`Compass`/`Gyroscope` instance |
| `getIsSupportedProperty()` (static) | Real | High | **Real on Android** (`Detail::AndroidMotionBackend`); `false` stub everywhere else |
| `getStateProperty()` | `NOXNA` | High | Real `Motion` has no `State` |
| `Start()` | Real | High | Real on Android; throws `SensorFailedException` elsewhere |
| `Calibrate` | Real | High | **Never raised by any backend** — `IMotionBackend` has no calibration callback at all |
| `SetBackendForTesting()` | `NOXNA` | — | Test-only hook |

## Cross-cutting members (`DEV-API-001`, added 2026-07-06)

The four sensor classes (`Accelerometer`/`Gyroscope`/`Compass`/`Motion`) share this
identical set of members via `SensorBase<T>` — listed once here instead of repeated per
class above, so this file stays a genuine one-row-per-member matrix without
4x-duplicating identical notes. **`VibrateController` does not derive `SensorBase<T>` or
`System::Object`** — it has only its own destructor (already its own row in that
class's table above), no `Dispose()`/`Dispose(bool)`/`GetTypeName()` at all, matching
the real WP7 `VibrateController`, which implements neither `IDisposable` nor exposes a
type-name API a game would call.

| Member | Real/NOXNA | Confidence | Notes |
|---|---|---|---|
| Destructor (`~Accelerometer()` etc.) | Real (public, untagged) | High | Matches C#'s implicit object lifetime; `SensorBase<T>`'s own virtual destructor calls `Dispose(false)` if not already disposed |
| `Dispose()` (public, no-arg) | Real | High | Inherited from `SensorBase<T>` via `using SensorBase<T>::Dispose;` (needed so declaring `Dispose(bool)` doesn't hide it) |
| `Dispose(bool)` (public override) | Real | High | Matches the standard C# `IDisposable` dispose pattern CNA uses project-wide, not WP7-specific |
| `GetTypeName()` | `NOXNA` | High | Via the project-wide `GetTypeNameHPP()`/`GetTypeNameCPP()` macro pair (`sharp-runtime/include/System/Object.hpp`), which does **not** literally prefix the `NOXNA` marker at each of its ~12 use sites project-wide — confirmed this is the established, consistent, project-wide convention (identical everywhere `GetTypeNameHPP()` is used, not a Devices-specific gap), so not flagged as an Extra-unmarked finding here. |

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
| `SensorReadingEventArgs<T>` | High | Generic wrapper. `setSensorReadingProperty()` is fully `public` (2 overloads: copy and move) — see "Flagged findings" below, same open question as `AccelerometerReadingEventArgs`. |

---

## Flagged findings — needs follow-up (`DEV-API-001`, 2026-07-06)

Two **wrong-visibility (unverified)** findings — the member itself is real, but this
pass could not confirm its C++ access level matches the real API's C# accessibility.
Neither is a Missing or Extra-unmarked finding (no member is absent, and nothing here is
CNA-only API masquerading as strict XNA), so neither blocks this task's acceptance
criteria — recorded here for `READINGS-002` (which already exists in this plan and
already covers exactly these two types) to resolve, not fixed by this task.

- **`AccelerometerReadingEventArgs`'s `setXProperty()`/`setYProperty()`/
  `setZProperty()`/`setTimestampProperty()` are fully `public`.** Every reading struct
  in this namespace (`AccelerometerReading`, `GyroscopeReading`, `CompassReading`,
  `MotionReading`, `AttitudeReading`) instead makes its setters `private` + `friend
  class <owning sensor>` (Task P3-2), explicitly matching the real API's `internal set`
  convention. `AccelerometerReadingEventArgs` is the one type in this namespace that
  does not follow that pattern — unclear whether that's because the real WP7
  `AccelerometerReadingEventArgs.X`/`Y`/`Z`/`Timestamp` properties genuinely have a
  public setter (plausible: WP7 7.0-era event-args classes predate the `SensorBase<T>`
  pattern the other four structs follow, and may have been designed differently), or
  whether this is simply an unfixed drift from before Task P3-2's convention was
  established. Needs an authoritative WP7 7.0 reference check, not an assumption either
  way — `READINGS-002`'s job.
- **`SensorReadingEventArgs<T>::setSensorReadingProperty()` is fully `public`** (both the
  copy and move overloads). Same open question: this class only ever legitimately holds
  a reading the producing sensor already validated and dispatched, so a real `internal
  set` (mirroring the reading structs) would be the more defensively-shaped choice, but
  this pass found no direct evidence either way for the *generic* real WP7
  `SensorBase<T>`-equivalent event-args type specifically.

## SDL/internal-only internals (`Detail::` namespace, not XNA-facing)

**Re-confirmed 2026-07-06 (`DEV-API-001`): none of this namespace's members appear in
any public (non-`Detail::`) header** — `grep`-verified no `Detail::` type or free
function is referenced from `Accelerometer.hpp`/`Gyroscope.hpp`/`Compass.hpp`/
`Motion.hpp`/`SensorBase.hpp`'s public sections, only forward-declared as an opaque
pointer/reference member (e.g. `Accelerometer.hpp`'s `friend class
Detail::SdlSensorSubsystem<Accelerometer>;` forward declaration) or used entirely inside
`.cpp` files. This table was extended this pass — the Android-only rows below predate
2026-07-06; the SDL-backend and shared-utility rows are new.

| Type | Purpose |
|---|---|
| `AndroidSensorBridge` | Shared NDK `ASensorManager`/`ASensorEventQueue`/`ALooper` wrapper, one instance per Android sensor type |
| `ICompassBackend` / `AndroidCompassBackend` | Compass's Android implementation |
| `IMotionBackend` / `AndroidMotionBackend` | Motion's Android implementation |
| `ConvertRotationVectorToMagneticHeadingDegrees()` | Pure azimuth-from-quaternion function (Compass) |
| `ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees()` / `ShouldRaiseCalibrateForAccuracyStatus()` | Accuracy-status mapping (Compass) |
| `ConvertRotationVectorToXnaQuaternion()` / `ExtractYawPitchRollFromQuaternion()` | Quaternion passthrough + Euler extraction (Motion) |
| `AndroidSensorLandscapeOrientation` (enum) / `ConvertAndroidPortraitToXnaLandscape()` | Accelerometer/Gyroscope's shared landscape axis remap |
| `SdlSensorSubsystem<TSensor>` | Shared SDL sensor-subsystem/event-watch/dispatch machinery for `Accelerometer`/`Gyroscope`, one instantiation per concrete sensor type (added `DEV-API-001`) |
| `GetGlobalSdlSensorMutex()` | Process-wide mutex serializing real SDL sensor-subsystem calls across `Accelerometer` and `Gyroscope` (added `DEV-API-001`) |
| `ScopeExit<F>` / `MakeScopeExit()` | General-purpose RAII scope-exit guard used by `SdlSensorSubsystem<TSensor>::DispatchToInstances()`'s cleanup path (added `DEV-API-001`) |

---

## DEV-API-001 verification result (2026-07-06)

Read every public header in scope end-to-end (`VibrateController.hpp`; `SensorBase.hpp`;
`Accelerometer.hpp`/`Gyroscope.hpp`/`Compass.hpp`/`Motion.hpp`; the five reading structs;
`CalibrationEventArgs.hpp`/`AccelerometerReadingEventArgs.hpp`/
`SensorReadingEventArgs.hpp`; `SensorState.hpp`/`ISensorReading.hpp`;
`SensorFailedException.hpp`/`AccelerometerFailedException.hpp`) directly against this
file's prior content, rather than assuming the file was still current.

- **Missing (real XNA/WP7 API absent here): none found.**
- **Extra-unmarked (CNA extension not tagged `NOXNA`): none found.** This includes
  re-checking the exact drift this task's acceptance criteria names as the example case
  to catch — `Accelerometer::getStateProperty()`'s missing `NOXNA` marker vs.
  `Gyroscope`/`Compass`/`Motion`'s marked ones — which `DEV-API-003` (2026-07-06, see
  `plan_devices.md`) had already independently re-investigated and closed as **not** a
  bug: `Accelerometer.State` is real WP7 API (MSDN `ff707930`), the other three
  correctly have no such property (MSDN `hh239201`/`hh220912`/`hh239189`), so the
  asymmetric marking is the *correct* state, not drift this matrix needed to newly
  catch — it had already been caught and resolved.
- **Wrong signature/visibility (unverified): 2 found**, both newly recorded in "Flagged
  findings" above (`AccelerometerReadingEventArgs`'s and `SensorReadingEventArgs<T>`'s
  public setters, vs. every reading struct's `private`+`friend` convention) —
  cross-referenced to `READINGS-002`, which already exists in `plan_devices.md` to
  resolve them; not fixed by this task.
- **Coverage gaps in this file itself (not API bugs, just matrix incompleteness) that
  this pass fixed:** added the "Cross-cutting members" tables (destructor/`Dispose()`/
  `Dispose(bool)`/`GetTypeName()` for the four sensor classes; constructors/getters/
  setters/equality/`ToString()`/`GetHashCode()`/`GetTypeName()` for the five reading
  structs) — previously implicit/assumed rather than explicitly tabulated; extended the
  `Detail::` internals table with `SdlSensorSubsystem<TSensor>`/
  `GetGlobalSdlSensorMutex()`/`ScopeExit<F>`, which existed in the codebase but were
  missing from this table.
