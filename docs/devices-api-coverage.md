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

**Real/CNAEXT legend:** `Real` = strict XNA 4.0/WP7 API, name and shape must match exactly.
`WP7-legacy` = real WP7 API but superseded/deprecated in-spec (e.g. `ReadingChanged`).
`CNAEXT` = CNA-only extension, must be tagged `CNAEXT` on the public declaration.
`Internal-only` = implementation detail, must never appear in a public header at all (see
"SDL/internal-only internals" below).

**Missing/Extra/Wrong-signature flags** (`DEV-API-001`'s required three-way distinction,
applied inline in the tables below where relevant): **Missing** = present in the real
XNA/WP7 API but absent here. **Extra-unmarked** = present here, not real XNA/WP7 API, and
*not* tagged `CNAEXT` — the actual bug pattern this distinction exists to catch. **Wrong
signature/visibility** = the member exists and is real, but its C++ shape (access level,
parameter types, overload set) may not match the real API — flagged when unverified, not
assumed correct by default. This pass found **zero Missing and zero Extra-unmarked**
members; two **Wrong-visibility (unverified)** findings are recorded under "Flagged
findings" below.

---

## `Microsoft::Devices::VibrateController`

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| `getDefaultProperty()` (static) | Real | High | Never-null singleton |
| `Start(TimeSpan)` | Real | High | Throws `ArgumentOutOfRangeException` outside `[Zero, FromSeconds(5)]` |
| `Stop()` | Real | High | Silent no-op if nothing active/no device |
| `Start(TimeSpan, float intensity)` | `CNAEXT` | — | Intensity `[0,1]`; `intensity=0` still uploads a zero-strength effect, not an implicit `Stop()` |
| `getIsSupportedProperty()` | `CNAEXT` | — | Probe-only, doesn't hold a device open |
| `getDeviceNameProperty()` | `CNAEXT` | — | Probe-only |
| `StartLeftRight(float, float, TimeSpan)` | `CNAEXT` | — | `SDL_HAPTIC_LEFTRIGHT`; mutually exclusive with `Start()` (each stops the other's effect) |
| Constructor | Real (private) | High | Only reachable via `getDefaultProperty()` |
| `~VibrateController()` | Real (public, untagged) | — | Real WP7 has no explicit destructor concept (fire-and-forget static-shaped design in C#); C++ requires one for the singleton's static-lifetime cleanup. Deliberately **not** tagged `CNAEXT` — documented in its own doc comment as "CNAEXT in spirit" but not a new callable public API surface a game would ever invoke directly. Not a Missing/Extra finding: every other class in this namespace has an equivalent untagged destructor (see "Cross-cutting members" below), this is a project-wide, not Devices-specific, C++/C# lifetime-model gap. |

## `Microsoft::Devices::Sensors::SensorBase<TSensorReading>`

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| `getCurrentValueProperty()` | Real | High | Throws `InvalidOperationException` if unsupported; does not throw if supported-but-no-reading-yet |
| `getIsDataValidProperty()` | Real | High | Defaults `false` |
| `getTimeBetweenUpdatesProperty()`/`set...` | Real | High | Default 2ms. **Fixed 2026-07-06 (`SENSORBASE-001`/`ACCEL-005`/`GYRO-004`):** now really throttles `Accelerometer`/`Gyroscope` dispatch (`SensorBase<T>::ShouldAcceptUpdateAt()`, called from each class's `ProcessSensorUpdateEvent()`). **Also fixed 2026-07-06 (`ANDROID-BRIDGE-002`, closed):** `Compass`/`Motion` forward a live change to `Detail::AndroidSensorBridge::SetSampleInterval()`, which re-applies `ASensorEventQueue_setEventRate()` on the already-running queue — no longer applied only once at `Start()` time. All four sensor classes now honor a running `TimeBetweenUpdates` change without `Stop()`/`Start()`. |
| `CurrentValueChanged` | Real | High | Public event |
| `TimeBetweenUpdatesChanged` | `CNAEXT` | High | **Fixed 2026-07-06 (`SENSORBASE-007`):** was wrongly marked `Real` here — the real `SensorBase(TSensorReading)`'s own archived MSDN page (`hh239315(v=vs.105)`) lists exactly one event, `CurrentValueChanged`; no such member exists in the real API (confirmed by a dedicated web search finding zero hits). Was a genuine, previously-unflagged **Extra-unmarked** finding: a CNA-only extension (protected, used to forward a live `TimeBetweenUpdates` change to `Compass`/`Motion`'s Android backend) declared without the `CNAEXT` marker. Now tagged `CNAEXT` in `SensorBase.hpp`. |
| `Start()`/`Stop()` | Real (abstract) | High | |
| `Dispose()` | Real | High | Second call throws `ObjectDisposedException` |

No `IsSupported`/`State` on the base class (those are per-subclass statics/properties).

## `Microsoft::Devices::Sensors::Accelerometer`

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| Constructor | Real | High | Throws `SensorFailedException` past 10 simultaneous instances |
| `getIsSupportedProperty()` (static) | Real | High | Real SDL3-backed (`SDL_SENSOR_ACCEL`) |
| `getStateProperty()` | Real | High | The one sensor class with a real `State` (MSDN `ff707531`, confirmed `ACCEL-001`) |
| `Start()` | Real | High | Throws `AccelerometerFailedException` on failure |
| `Stop()` | Real | High | |
| `CurrentValueChanged` | Real | High | Fires from the SDL sensor thread — treat as unknown-thread |
| `ReadingChanged` (legacy) | Real | High | WP7 7.0, `[Obsolete]` since 7.1/8.0/8.1 but still present and raised (MSDN `ff707930`, confirmed `ACCEL-001`); raised alongside `CurrentValueChanged` |
| Unit conversion | Real | High | m/s² → g (`÷ 9.80665f`), confirmed correct and tested |
| Android axis remap | `CNAEXT`-adjacent internal | Medium | `Detail::ConvertAndroidPortraitToXnaLandscape()`; unit-tested, **never hardware-verified** |
| 8 `*ForTesting()`/`InjectSynthetic*` hooks | `CNAEXT` | — | Test-only |
| Desktop support policy (`ACCEL-007`) | — | High | Deliberately "fully supported wherever SDL exposes real hardware" — `Desktop` is listed alongside `Android`/`iOS` in `getIsSupportedProperty()`'s allowed-platform check, not a permanent no-op. `Platform::Web` (Emscripten) is excluded, pre-existing and not re-examined by `ACCEL-007` despite SDL itself having a real `SDL_SENSOR_EMSCRIPTEN` backend. |

## `Microsoft::Devices::Sensors::Gyroscope`

Identical shape to `Accelerometer` minus `ReadingChanged` (correctly absent — no legacy
event in the real API).

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| `getStateProperty()` | `CNAEXT` | High | Real `Gyroscope` has no `State` (MSDN `hh239201`, re-confirmed `GYRO-001`: Properties table lists only `CurrentValue`/`IsDataValid`/`IsSupported`/`TimeBetweenUpdates`, all inherited from `SensorBase<T>` except `IsSupported`) |
| Unit | Real | High | rad/s, no conversion (matches SDL3's own doc and WP7's documented unit, MSDN `hh239090`, re-confirmed `GYRO-002`) |
| Self-destroy-from-own-callback safety | Real | — | Fully safe (unlike `Accelerometer`, which has the extra `ReadingChanged` check) |

## `Microsoft::Devices::Sensors::Compass`

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| Constructor | Real | High | 10-instance cap |
| `getIsSupportedProperty()` (static) | Real | High | **Real on Android** (`Detail::AndroidCompassBackend`); `false` stub everywhere else |
| `getStateProperty()` | `CNAEXT` | High | Real `Compass` has no `State` |
| `Start()` | Real | High | Real on Android; throws `SensorFailedException` elsewhere |
| `Calibrate` | Real | High | Raised on Android for `UNRELIABLE`/`NO_CONTACT` magnetic-field accuracy |
| `SetBackendForTesting()` | `CNAEXT` | — | Test-only hook |

## `Microsoft::Devices::Sensors::CompassReading`

| Member | Confidence | Notes |
|---|---|---|
| `HeadingAccuracy` | High (member exists) / N/A (CNA's degree scale is a project choice) | Android: mapped from magnetic-field accuracy status |
| `MagneticHeading` | High | Android: from rotation-vector azimuth, **never hardware-verified** |
| `MagnetometerReading` | High | Android: raw µT vector, no axis remap applied — **now believed correct, not an open question** (`COMPASS-001`, 2026-07-06): an archived MSDN Magazine article ("Touch and Go - Getting Oriented with the Windows Phone Compass", Petzold, 2012) states `MagnetometerReading` shares the same device-fixed coordinate system as `Accelerometer.Acceleration`, which the same article says "is the same whether... running in portrait or landscape mode" — i.e. no landscape remap should apply to either. This same finding raises a much larger question about whether `Accelerometer`/`Gyroscope`'s own existing landscape remap should exist at all — tracked separately in `ACCEL-008` (open, needs a decision), not resolved here. |
| `TrueHeading` | High (member) | Android: **always equals `MagneticHeading`** — honest limitation, no `System.Device.Location` |

## Cross-cutting members — reading structs (`DEV-API-001`, added 2026-07-06)

`AccelerometerReading`/`GyroscopeReading`/`CompassReading`/`MotionReading`/
`AttitudeReading` and `AccelerometerReadingEventArgs` each share this identical member
shape — listed once here rather than repeated in every per-struct table above/below —
**except the setter row below, where `AccelerometerReadingEventArgs` now genuinely
diverges** (Task `READINGS-002`, 2026-07-06): it has no setter at all. (`CalibrationEventArgs`
does **not** share this shape — it is a genuinely empty marker class per the real WP7
API, with only a constructor and `GetTypeName()`; already fully covered by its own row
in "Exceptions / Enums" below.)

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| Parameterless + parameterized constructors | Real | High | Two ctors each: default (zero/identity values) and one taking every data member |
| `get<Field>Property()` (per field) | Real | High | Public getters, `const` reference or by-value depending on field type |
| `set<Field>Property()` (per field) | Real, `AccelerometerReadingEventArgs` has none | High | **`AccelerometerReading`/`GyroscopeReading`/`CompassReading`/`MotionReading`/`AttitudeReading`:** `private` + `friend class <owning sensor>` (Task P3-2), matching the real API's `internal set`. **`AccelerometerReadingEventArgs` (fixed `READINGS-002`, 2026-07-06): removed entirely** — the real API has no setter for `X`/`Y`/`Z` (public get-only) and only a `private set` for `Timestamp`, which needs no dedicated method since the constructor already assigns the private field directly. See "Flagged findings" below for the full citation. |
| `operator==`/`operator!=` | `CNAEXT` | High | **Fixed (`DEV-API-004`/`DEV-API-002`, 2026-07-06):** was wrongly marked `Real` here — each real reading structure's own archived MSDN page (`AccelerometerReading` `ff403534`, `GyroscopeReading` via `hh239315`'s sibling pattern, `CompassReading` `hh203072`, `MotionReading` `hh220685`, `AttitudeReading` `hh220667`) shows no equality operator at all; `Equals(Object)` is inherited unmodified from `System.ValueType` (field-reflection based). `AccelerometerReadingEventArgs` (a `class`, not `struct`) is the same story via `System.Object` instead (own page `ff707998`, reference-identity `Equals`). C++ has no equivalent automatic equality, so this is a genuine, useful CNA extension, now tagged `CNAEXT` on all six headers. Was a real, previously-unflagged **Extra-unmarked** finding, same bug pattern as `SENSORBASE-007`'s `TimeBetweenUpdatesChanged` finding. |
| `ToString()` | `CNAEXT` | High | **Fixed (`DEV-API-004`/`DEV-API-002`, 2026-07-06):** was wrongly marked `Real` with an incorrect claim ("Matches XNA's conventional format") — every real reading structure's `ToString()` is inherited unmodified from `System.ValueType.ToString()` (`AccelerometerReadingEventArgs`: `System.Object.ToString()`), which returns only the fully qualified type name (e.g. `"Microsoft.Devices.Sensors.AccelerometerReading"`), never field values. CNA's field-value format is a more useful convention, now tagged `CNAEXT` on all six headers. |
| `GetHashCode()` | `CNAEXT` | High | **Fixed (`DEV-API-004`/`DEV-API-002`, 2026-07-06):** was wrongly marked `Real` — every real reading structure's `GetHashCode()` is inherited unmodified from `System.ValueType.GetHashCode()` (`AccelerometerReadingEventArgs`: `System.Object.GetHashCode()`). Now tagged `CNAEXT` on all six headers. |
| `GetTypeName()` | `CNAEXT` | High | Hand-declared per struct as `CNAEXT [[nodiscard]] std::string GetTypeName() const;` (not the `GetTypeNameHPP()` macro — these don't inherit `System::Object` polymorphically the way the four sensor classes do) |

## `Microsoft::Devices::Sensors::Motion`

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| Constructor | Real | High | 10-instance cap; does **not** require a live `Accelerometer`/`Compass`/`Gyroscope` instance |
| `getIsSupportedProperty()` (static) | Real | High | **Real on Android** (`Detail::AndroidMotionBackend`); `false` stub everywhere else |
| `getStateProperty()` | `CNAEXT` | High | Real `Motion` has no `State` (MSDN `hh239189`, re-confirmed `MOTION-001`) |
| `Start()` | Real | High | Real on Android; throws `SensorFailedException` elsewhere |
| `Calibrate` | Real | High | Confirmed real (MSDN `hh239189`'s Events table: "Occurs when the operating system detects that the compass needs calibration", `MOTION-001`) — **now actually fires** on Android (`Task MOTION-011`, 2026-07-16): `AndroidMotionBackend` independently monitors `TYPE_MAGNETIC_FIELD` accuracy and raises it under the same condition `Compass::Calibrate` already uses |
| `SetBackendForTesting()` | `CNAEXT` | — | Test-only hook |

## Cross-cutting members (`DEV-API-001`, added 2026-07-06)

The four sensor classes (`Accelerometer`/`Gyroscope`/`Compass`/`Motion`) share this
identical set of members via `SensorBase<T>` — listed once here instead of repeated per
class above, so this file stays a genuine one-row-per-member matrix without
4x-duplicating identical notes. **`VibrateController` does not derive `SensorBase<T>` or
`System::Object`** — it has only its own destructor (already its own row in that
class's table above), no `Dispose()`/`Dispose(bool)`/`GetTypeName()` at all, matching
the real WP7 `VibrateController`, which implements neither `IDisposable` nor exposes a
type-name API a game would call.

| Member | Real/CNAEXT | Confidence | Notes |
|---|---|---|---|
| Destructor (`~Accelerometer()` etc.) | Real (public, untagged) | High | Matches C#'s implicit object lifetime; `SensorBase<T>`'s own virtual destructor calls `Dispose(false)` if not already disposed |
| `Dispose()` (public, no-arg) | Real | High | Inherited from `SensorBase<T>` via `using SensorBase<T>::Dispose;` (needed so declaring `Dispose(bool)` doesn't hide it) |
| `Dispose(bool)` (public override) | Real | High | Matches the standard C# `IDisposable` dispose pattern CNA uses project-wide, not WP7-specific |
| `GetTypeName()` | `CNAEXT` | High | Via the project-wide `GetTypeNameHPP()`/`GetTypeNameCPP()` macro pair (`sharp-runtime/include/System/Object.hpp`), which does **not** literally prefix the `CNAEXT` marker at each of its ~12 use sites project-wide — confirmed this is the established, consistent, project-wide convention (identical everywhere `GetTypeNameHPP()` is used, not a Devices-specific gap), so not flagged as an Extra-unmarked finding here. |

## Timestamp policy — all four sensor classes (`READINGS-003`, added 2026-07-06)

**One rule, applied identically everywhere a reading struct's `Timestamp` is set:
always `System::DateTimeOffset::getUtcNowProperty()` (wall-clock time of dispatch/
publish), never a raw platform/monotonic sensor timestamp.** Confirmed applied
consistently across every current call site, not just where it happened to already be
documented (`Detail::AndroidSensorSample::Timestamp`'s own doc comment):

| Sensor class | Where `Timestamp` is actually set | Wall-clock? |
|---|---|---|
| `Accelerometer` | `Accelerometer.cpp::DispatchSensorReading()`, direct `getUtcNowProperty()` call | Yes |
| `Gyroscope` | `Gyroscope.cpp::DispatchSensorReading()`, direct `getUtcNowProperty()` call | Yes |
| `Compass` | `Detail::AndroidCompassBackend::PublishReading()`, direct `getUtcNowProperty()` call passed into the `CompassReading` constructor | Yes |
| `Motion` | `Detail::AndroidSensorBridge.cpp`'s dispatch loop sets `AndroidSensorSample::Timestamp = getUtcNowProperty()` once per raw NDK sample; `Detail::AndroidMotionBackend`'s four `Handle*Sample()` methods copy that same value into `attitude_`/`gravityTimestamp_`/`linearAccelerationTimestamp_`/`gyroscopeTimestamp_`, and `PublishReading()` sets both `MotionReading.Timestamp` and (nested) `MotionReading.Attitude.Timestamp` from `attitude_.getTimestampProperty()` specifically (Task `MOTION-006`'s fix — previously `MotionReading.Timestamp` used a *second*, independently-fresh `getUtcNowProperty()` call, which could disagree with the nested `Attitude.Timestamp`) | Yes |

**Why wall-clock, not the platform's own raw sensor timestamp** (e.g. Android NDK's
`ASensorEvent::timestamp`, a monotonic value counted from an arbitrary boot-time epoch):
`System::DateTimeOffset` represents a specific calendar point in time (like C#'s real
`DateTimeOffset`), which a monotonic boot-time nanosecond counter cannot be converted to
without an unreliable, platform-specific boot-time-to-wall-clock offset calculation —
using it directly would silently produce a nonsensical `DateTimeOffset` (e.g. an
implausible near-1970/near-1601 date, depending on epoch), not a merely-imprecise one.
Wall-clock-at-dispatch is a small, bounded approximation of "when the physical sample was
taken" (dispatch happens promptly after the OS delivers a sample, not deferred), and is
the only option that produces a genuinely valid `DateTimeOffset` on every platform.

**Testability:** `Accelerometer`/`Gyroscope` construct their own reading with a fresh
`getUtcNowProperty()` call inline in `DispatchSensorReading()`, so their tests
(`AccelerometerTests`/`GyroscopeTests`' `CurrentValueChangedReceivesWallClockTimestamp`)
use a before/after real-time bracket (`EXPECT_GE`/`EXPECT_LE` against two real
`getUtcNowProperty()` calls taken immediately around the dispatch) rather than an
injected fixed clock — appropriate since the timestamp genuinely is generated fresh
inside the method under test, not a value the test controls. `Compass`/`Motion`, by
contrast, only ever forward whatever `CompassReading`/`MotionReading` their
(fake-backend-injectable, via `SetBackendForTesting()`) backend hands them —
`Compass`/`Motion`'s own C++ code never re-touches `Timestamp` after receiving it — so
`CompassTests`/`MotionTests`' new `CurrentValueChangedPropagatesBackendTimestampExactly`
tests (added this task; no equivalent existed before) inject one fixed,
deliberately-distinguishable `DateTimeOffset` via the fake backend and assert exact
(not bracketed) equality on the value that reaches `CurrentValueChanged`/`CurrentValue`
— proving the propagation path itself is a pure passthrough with no truncation,
re-timestamping, or clamping of its own. This is deliberately the strongest test each
class's own architecture allows: the real wall-clock-generating code
(`AndroidCompassBackend`/`AndroidSensorBridge`) is Android-only and unreachable on this
host, so the fixed-clock injection tests what *is* testable here (propagation), while
the wall-clock-generation itself is confirmed by direct source reading, per the table
above.

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
| `AccelerometerFailedException` | High | `Accelerometer`-specific; mirrors `SensorFailedException`'s 3 ctors. `Gyroscope`/`Compass`/`Motion` correctly have no dedicated subclass, use plain `SensorFailedException`. **Verified with direct citations (`DEV-API-005`, 2026-07-06):** `Gyroscope`/`Compass`/`Motion`'s own class pages (`hh239201(v=vs.110)`/`hh220912(v=vs.105)`/`hh239189(v=vs.105)`) list `Start`/`Stop` as inherited from `SensorBase<TSensorReading>`, never overridden — and that base `Start()` page (`hh220889(v=vs.105)`) documents `SensorFailedException` as its own real failure type. `Accelerometer.Stop()`'s own dedicated page (`ff707301(v=vs.105)`, confirming it *is* overridden) documents `AccelerometerFailedException` specifically. The split is exactly correct, not a CNA invention needing a `CNAEXT` tag. |
| `SensorState` (enum) | Medium | 6 values (`NotSupported`/`Ready`/`Initializing`/`NoData`/`NoPermissions`/`Disabled`) — MonoGame cross-check only, no direct MSDN enum page found |
| `ISensorReading` | High | Single `Timestamp` member |
| `CalibrationEventArgs` | High | Empty marker class, confirmed against its exact member-list page |
| `AccelerometerReadingEventArgs` | High | WP7 7.0 legacy, paired with `Accelerometer.ReadingChanged`. **Fixed (`READINGS-002`, 2026-07-06):** `X`/`Y`/`Z`/`Timestamp` setters removed — confirmed via MSDN `ff707568`/`ff707712`/`ff708055`/`ff707430` that the real API has no public/internal setter for `X`/`Y`/`Z` and only `private set` for `Timestamp`. |
| `SensorReadingEventArgs<T>` | High | Generic wrapper. `setSensorReadingProperty()` (2 overloads: copy and move) is fully `public` — confirmed correct (`READINGS-002`, MSDN `hh203225`: real `SensorReading` property is `public T SensorReading { get; set; }`), not a bug. |

---

## Flagged findings — resolved (`READINGS-002`, 2026-07-06)

Both `DEV-API-001` wrong-visibility findings below are now resolved against the
archived MSDN "previous-versions" pages (fetched directly, not assumed) — one was a
real, confirmed bug (now fixed); the other was CNA's existing code already matching the
real API exactly (no change needed).

- **`AccelerometerReadingEventArgs`'s `setXProperty()`/`setYProperty()`/
  `setZProperty()`/`setTimestampProperty()` — confirmed a real bug, fixed.** The real
  `Microsoft.Devices.Sensors.AccelerometerReadingEventArgs` has:
  - `public double X { get; }` (MSDN `ff707568`) — **no setter at all**, public or
    otherwise.
  - `public double Y { get; }` (MSDN `ff707712`) — same, no setter.
  - `public double Z { get; }` (MSDN `ff708055`) — same, no setter.
  - `public DateTimeOffset Timestamp { get; private set; }` (MSDN `ff707430`) —
    `private set`, not the `internal set` the other four reading structs use.

  CNA's public `setXProperty()`/`setYProperty()`/`setZProperty()`/
  `setTimestampProperty()` were therefore genuine **Extra-unmarked** API — CNA-only
  additions with no real counterpart, never tagged `CNAEXT`. All four were unused dead
  code (confirmed by grep: `Accelerometer::DispatchSensorReading()` only ever
  constructs this type via its 4-argument constructor, never calls a setter) — removed
  entirely rather than tagged `CNAEXT`, since the real API has no public/internal
  setter to preserve access to. `Timestamp`'s real `private set` needs no dedicated
  method in the C++ port: the constructor already assigns the private field directly,
  which is the literal C++ equivalent of "settable only from within this class's own
  code."
- **`SensorReadingEventArgs<T>::setSensorReadingProperty()` — confirmed correct as-is,
  no change needed.** The real `Microsoft.Devices.Sensors.SensorReadingEventArgs<T>`
  has `public T SensorReading { get; set; }` (MSDN `hh203225`) — a fully public setter,
  exactly matching CNA's existing implementation (both the copy and move overloads).
  This class is the outlier in the *other* direction from `AccelerometerReadingEventArgs`
  — genuinely publicly mutable in the real API, unlike the reading structs' `internal
  set` convention.

## Platform/internal-only internals (`Detail::` namespace, not XNA-facing)

**Re-confirmed 2026-07-06 (`DEV-API-001`): none of this namespace's members appear in
any public (non-`Detail::`) header** — `grep`-verified no `Detail::` type or free
function is referenced from `Accelerometer.hpp`/`Gyroscope.hpp`/`Compass.hpp`/
`Motion.hpp`/`SensorBase.hpp`'s public sections, only forward-declared as an opaque
pointer/reference member (e.g. `Accelerometer.hpp`'s `friend class
Detail::PlatformSensorSubsystem<Accelerometer>;` forward declaration) or used entirely inside
`.cpp` files. This table was extended this pass — the Android-only rows below predate
2026-07-06; PLAT-108 subsequently replaced the native backend rows with platform adapters.

| Type | Purpose |
|---|---|
| `AndroidSensorBridge` | Shared NDK `ASensorManager`/`ASensorEventQueue`/`ALooper` wrapper, one instance per Android sensor type |
| `ICompassBackend` / `AndroidCompassBackend` | Compass's Android implementation |
| `IMotionBackend` / `AndroidMotionBackend` | Motion's Android implementation |
| `ConvertRotationVectorToMagneticHeadingDegrees()` | Pure azimuth-from-quaternion function (Compass) |
| `ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees()` / `ShouldRaiseCalibrateForAccuracyStatus()` | Accuracy-status mapping (Compass) |
| `ConvertRotationVectorToXnaQuaternion()` / `ExtractYawPitchRollFromQuaternion()` | Quaternion passthrough + Euler extraction (Motion) |
| `AndroidSensorLandscapeOrientation` (enum) / `ConvertAndroidPortraitToXnaLandscape()` | Accelerometer/Gyroscope's shared landscape axis remap |
| `PlatformSensorSubsystem<TSensor>` | Shared platform session/registration/callback-dispatch machinery for `Accelerometer`/`Gyroscope`, one instantiation per concrete sensor type (migrated by PLAT-108) |
| `PlatformVibrateBackend` | Private adapter from `VibrateController` to `IPlatformHaptics` (added by PLAT-108) |
| `ScopeExit<F>` / `MakeScopeExit()` | General-purpose RAII scope-exit guard used by `PlatformSensorSubsystem<TSensor>::DispatchToInstances()`'s cleanup path (added `DEV-API-001`) |

---

## DEV-API-001 verification result (2026-07-06)

Read every public header in scope end-to-end (`VibrateController.hpp`; `SensorBase.hpp`;
`Accelerometer.hpp`/`Gyroscope.hpp`/`Compass.hpp`/`Motion.hpp`; the five reading structs;
`CalibrationEventArgs.hpp`/`AccelerometerReadingEventArgs.hpp`/
`SensorReadingEventArgs.hpp`; `SensorState.hpp`/`ISensorReading.hpp`;
`SensorFailedException.hpp`/`AccelerometerFailedException.hpp`) directly against this
file's prior content, rather than assuming the file was still current.

- **Missing (real XNA/WP7 API absent here): none found.**
- **Extra-unmarked (CNA extension not tagged `CNAEXT`): none found** in this original
  pass. This includes re-checking the exact drift this task's acceptance criteria names
  as the example case to catch — `Accelerometer::getStateProperty()`'s missing `CNAEXT`
  marker vs. `Gyroscope`/`Compass`/`Motion`'s marked ones — which `DEV-API-003`
  (2026-07-06, see `plan_devices.md`) had already independently re-investigated and
  closed as **not** a bug: `Accelerometer.State` is real WP7 API (MSDN `ff707531` —
  corrected `ACCEL-001`, 2026-07-06: every prior citation of `ff707930` for this
  property was a mix-up with `Accelerometer.ReadingChanged`'s own, differently-numbered
  page; both pages were independently re-fetched to confirm which ID belongs to which
  member, see `ACCEL-001`'s closing note in `plan_devices.md`), the other three
  correctly have no such property (MSDN `hh239201`/`hh220912`/`hh239189`), so the
  asymmetric marking is the *correct* state, not drift this matrix needed to newly
  catch — it had already been caught and resolved.
  **Update 2026-07-06 (`SENSORBASE-007`): one genuine Extra-unmarked finding was missed
  by this original pass and caught later** — `SensorBase<T>::TimeBetweenUpdatesChanged`
  was marked `Real` in this file's own `SensorBase<TSensorReading>` table above, with no
  citation, and declared with no `CNAEXT` tag in `SensorBase.hpp`. The real class's own
  archived MSDN page (`hh239315(v=vs.105)`) lists exactly one event
  (`CurrentValueChanged`) — no such member exists in the real API. Fixed: tagged `CNAEXT`
  in `SensorBase.hpp`, corrected in this file's table above.
  **Update 2026-07-06 (`DEV-API-004`): three more genuine Extra-unmarked findings found
  the same way, this time systemic across all five reading structs** —
  `operator==`/`operator!=`/`ToString()`/`GetHashCode()` were all marked `Real` in the
  "Cross-cutting members — reading structs" table above, with `ToString()`'s note even
  actively claiming (incorrectly) that it "Matches XNA's conventional format." Fetched
  each reading structure's own archived MSDN page directly (`AccelerometerReading`
  `ff403534`, `CompassReading` `hh203072`, `MotionReading` `hh220685`, `AttitudeReading`
  `hh220667`, plus `GyroscopeReading` by the identical established pattern — **since
  confirmed directly, not just by pattern, via its own archived page `hh220755(v=vs.105)`,
  Task READINGS-001, 2026-07-06**): all show `Equals`/`GetHashCode`/`ToString` inherited
  unmodified from `System.ValueType`, and no equality operator at all. Fixed: tagged
  `CNAEXT` on all four members across all five headers, corrected this file's table
  above.
  **Update 2026-07-06 (`READINGS-001`):** re-verified all five reading structs'
  *fields* (not just their cross-cutting members, already covered above) field-by-field
  against each struct's own archived MSDN page — `AccelerometerReading` (`ff403534`):
  `Acceleration`/`Timestamp`; `GyroscopeReading` (`hh220755`, fetched directly this
  pass, closing the "by pattern" gap noted above): `RotationRate`/`Timestamp`;
  `CompassReading` (`hh203072`): `HeadingAccuracy`/`MagneticHeading`/
  `MagnetometerReading`/`Timestamp`/`TrueHeading`; `MotionReading` (`hh220685`):
  `Attitude`/`DeviceAcceleration`/`DeviceRotationRate`/`Gravity`/`Timestamp`;
  `AttitudeReading` (`hh220667`): `Pitch`/`Roll`/`Yaw`/`Quaternion`/`RotationMatrix`/
  `Timestamp`. Every field on every struct in `include/Microsoft/Devices/Sensors/`
  matches exactly — no Missing, no Extra-unmarked fields found. Units/mutability were
  already independently confirmed by `ACCEL-003`/`GYRO-002`/`MOTION-003`/`MOTION-004`
  (units) and `P3-2` (the `private`+`friend`-restricted setter convention, matching
  every real property's `internal set`). Test coverage: every getter on every struct
  is exercised by that struct's own `ParameterizedConstructorStoresValues` test
  (`tests/Microsoft/Devices/Sensors/*ReadingTests.cpp`); every private setter is
  exercised indirectly through its owning sensor class's own dispatch tests
  (`Accelerometer`/`Gyroscope`/`Compass`/`Motion`'s synthetic-update-injection tests),
  since a reading struct's setters are only ever called from within its owning
  sensor's `DispatchSensorReading()` — no gap found, no new tests were needed.
- **Wrong signature/visibility (unverified): 2 found**, both newly recorded in "Flagged
  findings" above (`AccelerometerReadingEventArgs`'s and `SensorReadingEventArgs<T>`'s
  public setters, vs. every reading struct's `private`+`friend` convention) —
  cross-referenced to `READINGS-002`, which already exists in `plan_devices.md` to
  resolve them; not fixed by this task.
  **Update, same day:** `READINGS-002` resolved both — see "Flagged findings" above,
  now retitled "resolved" — against the archived MSDN pages (`ff707568`/`ff707712`/
  `ff708055`/`ff707430` for `AccelerometerReadingEventArgs`; `hh203225` for
  `SensorReadingEventArgs<T>`). One was a real bug (fixed); the other was already
  correct.
- **Coverage gaps in this file itself (not API bugs, just matrix incompleteness) that
  this pass fixed:** added the "Cross-cutting members" tables (destructor/`Dispose()`/
  `Dispose(bool)`/`GetTypeName()` for the four sensor classes; constructors/getters/
  setters/equality/`ToString()`/`GetHashCode()`/`GetTypeName()` for the five reading
  structs) — previously implicit/assumed rather than explicitly tabulated; extended the
  `Detail::` internals table with the sensor manager and `ScopeExit<F>`, which existed in the
  codebase but were missing from this table. PLAT-108 later updated those rows in place.
