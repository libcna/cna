# CNA Devices and Sensors XNA 4.0 Compatibility Plan

## Purpose

Drive `Microsoft::Devices` / `Microsoft::Devices::Sensors` from its current state —
**accepted as an SDL-backed baseline** (`plan_devices_phase9.md` Task P9-9, see `NEXT.md`
Section 2) — toward real native backends for the two permanently-stubbed classes
(`Compass`, `Motion`), a hardened/optional native Android vibration path, physical
hardware verification, and Android APK packaging. This plan does not re-litigate
Phases 1–9's closed work; it starts from their audited end state (see "Current
Repository Findings" below) and only re-verifies a prior claim where a task explicitly
says to.

## Scope

- `Microsoft::Devices::VibrateController`
- `Microsoft::Devices::Sensors::SensorBase<TSensorReading>` and all four concrete
  sensors (`Accelerometer`, `Gyroscope`, `Compass`, `Motion`)
- All reading classes, event-args classes, exceptions, enums (`AccelerometerReading`,
  `GyroscopeReading`, `CompassReading`, `MotionReading`, `AttitudeReading`,
  `AccelerometerReadingEventArgs`, `SensorReadingEventArgs<T>`, `CalibrationEventArgs`,
  `SensorFailedException`, `AccelerometerFailedException`, `SensorState`,
  `ISensorReading`)
- `Detail::SdlSensorSubsystem<TSensor>`, `Detail::AndroidSensorOrientation.hpp`
- `examples/demo_devices` (`cna_demo_devices`)
- Devices-specific docs (`docs/devices-*.md`), Android build/packaging as it relates to
  Devices only

## Non-Goals

- `Microsoft::Devices.Environment`, `PhotoCamera`/`CameraButtons`/`CameraCaptureTask`,
  `PhotoChooserTask`, radio, phone-call APIs — explicitly out of scope
  (`AUDIT.md` "Explicitly out of scope" note).
- `System.Device.Location` (GPS) — a separate, not-yet-started plan
  (`docs/location-future-plan.md`). **Never** add a location member to any
  `Microsoft::Devices::Sensors` class, including as `NOXNA`.
- A `NOXNA` main-thread dispatch queue/pump for sensor events — considered and
  explicitly rejected in `AUDIT.md` ("Considered, not implemented") for lack of a
  concrete need. Do not resurrect this without a real, evidenced use case.
- Rewriting `SensorBase<T>`/`Detail::SdlSensorSubsystem<TSensor>`'s locking/dispatch
  machinery — five phases of hardening (Phases 5–8) already closed the concurrency/
  lifetime gaps that were actually there (see `NEXT.md` Section 9). Only touch this
  machinery for a concrete, newly-found bug, not speculative cleanup.
- Retiring or SDL-backing `IAccelerometerBackend`/`IGyroscopeBackend` from
  `docs/devices-native-backend-design.md` — those exist for interface symmetry only;
  `Accelerometer`/`Gyroscope` keep their existing `Detail::SdlSensorSubsystem<TSensor>`
  implementation untouched.

---

## Current Repository Findings

(Full detail lives in the tasks of Phase 0 below; this is the pre-audit summary that
motivated this plan's shape.)

- **Real, SDL3-backed, hardened:** `Accelerometer`, `Gyroscope` (`SDL_SENSOR_ACCEL`/
  `SDL_SENSOR_GYRO`), `VibrateController` (SDL3 haptic). All three are ASan/TSan/UBSan
  clean, Android-cross-compile clean (library only), never run on physical hardware.
- **Permanent, honest stubs:** `Compass`, `Motion` — `getIsSupportedProperty()` hardcoded
  `false`, `Start()` always throws `SensorFailedException`. No native backend exists.
  A design-only sketch exists at `docs/devices-native-backend-design.md`
  (`IDeviceSensorBackend`/`ICompassBackend`/`IMotionBackend`, Android/iOS field mappings,
  a 6-step migration plan) — **nothing in it is implemented.**
- **Zero Android-native code exists anywhere in this repo.** Grepped the full tree
  (excluding `third_party/`): no JNI, no `SensorManager`, no `Vibrator`/
  `VibrationEffect`, no `TYPE_ROTATION_VECTOR`/`TYPE_GAME_ROTATION_VECTOR`/
  `TYPE_MAGNETIC_FIELD`/`TYPE_LINEAR_ACCELERATION`/`TYPE_GRAVITY` symbol anywhere. The
  only Android-specific Devices code today is
  `include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`'s pure
  axis-remap function (`ConvertAndroidPortraitToXnaLandscape()`), which only handles
  coordinate-system conversion for the *existing* SDL accelerometer/gyroscope, not a new
  sensor type.
- **Important, easy-to-miss fact for Phase 2/3:** `third_party/SDL/src/haptic/android/`
  contains a real Android haptic backend (`SDL_syshaptic.c`), and
  `src/Microsoft/Devices/VibrateController.cpp`'s own comment (unverified against SDL
  source until Task DEVICES-0031) claims SDL3's Android haptic driver already registers
  the phone's own vibration motor via `Context.VIBRATOR_SERVICE`, with **no custom
  JNI/Java bridge needed** in this project today. If true, a large parallel
  JNI-vibration-bridge (as sketched in the originating brief for this plan) may be
  **redundant or actively conflicting** with the existing, working SDL path — Phase 2's
  first task must resolve this before any new Android vibration code is written.
- **No Android build/packaging integration exists.** `cna_demo_devices` is defined via
  plain `add_executable()` inside the umbrella `if(CNA_BUILD_EXAMPLES)` block
  (`CMakeLists.txt:429`), **not excluded for `ANDROID`** the way `cna_demo_xact` is
  (`CMakeLists.txt:391`, guarded `NOT EMSCRIPTEN AND NOT ANDROID`) — meaning it is
  currently attempted, unverified, and likely wrong for Android (SDL-on-Android apps
  need a shared library loaded by a Java `Activity`, not a plain executable). This has
  never been built or noticed because Android builds in this repo have only ever used
  `-DCNA_BUILD_TESTS=OFF` with target `CNA` (the library), never `CNA_BUILD_EXAMPLES=ON`
  (`docs/devices-build.md` Section 4).
- **Test inventory (Google Test, `tests/Microsoft/Devices/**`):** 18 files, ~229
  `TEST`/`TEST_F` cases total (`Accelerometer` 33, `Gyroscope` 31, `VibrateController`
  29, `AccelerometerReadingEventArgs` 15, `Compass` 15, `Motion` 15,
  `AccelerometerFailedException` 8, `AccelerometerReading` 10, `AndroidSensorOrientation`
  9, `AttitudeReading` 10, `CalibrationEventArgs` 3, `CompassReading` 10,
  `GyroscopeReading` 10, `MotionReading` 10, `ScopeExit` 4, `SensorBase` 7,
  `SensorFailedException` 7, `SensorSubsystemOwnership` 3).
- **Docs inventory:** `docs/devices-build.md` (reproducible commands, sanitizer/Android
  results), `docs/devices-hardware-checklist.md` (6-case manual matrix, 1/6 verified),
  `docs/devices-native-backend-design.md` (design sketch, nothing implemented),
  `docs/location-future-plan.md` (explicitly out of scope placeholder).
- **Demo inventory:** `examples/demo_devices/{Main.cpp (11 lines), DevicesDemo.hpp (119
  lines), DevicesDemo.cpp (436 lines)}` — draws all four sensors +
  `VibrateController` diagnostics on-screen; `A`/`G` keys toggle
  `VibrateController`/accelerometer-gyroscope start-stop for manual desktop testing.
- **`NOXNA` extension inventory:** `VibrateController::Start(TimeSpan, float intensity)`,
  `getIsSupportedProperty()`, `getDeviceNameProperty()`, `StartLeftRight()`;
  `Accelerometer`/`Compass`/`Gyroscope`/`Motion::getStateProperty()` (real API has no
  `State` on these — only `Accelerometer` documents one, confirmed against MSDN);
  numerous `*ForTesting()` synthetic-injection hooks on `Accelerometer`.

---

## XNA / Windows Phone API Compatibility Matrix

Full per-member matrices are produced as Phase 0 deliverables (Tasks DEVICES-0002
through DEVICES-0008 below), one per class, written into `plan_devices.md`'s companion
audit output and cross-linked into `AUDIT.md`. Summary (all previously confirmed against
archived MSDN `previous-versions` pages, `plan_devices_phase2.md` Task P2-2 — Phase 0
here re-confirms these are still true of the current code, not re-derives them from
scratch):

| Class | Real API members | CNA status |
|---|---|---|
| `VibrateController` | `Default` (static), `Start(TimeSpan)`, `Stop()` | Complete + 4 `NOXNA` extensions |
| `SensorBase<T>` | `CurrentValue`, `IsDataValid`, `TimeBetweenUpdates`, `CurrentValueChanged`, `Start()`, `Stop()`, `Dispose()` | Complete |
| `Accelerometer` | + `IsSupported` (static), `State`, legacy `ReadingChanged` | Complete, real SDL backend |
| `Gyroscope` | + `IsSupported` (static) only (no `State`, no legacy event) | Complete, real SDL backend; `State` is `NOXNA` |
| `Compass` | + `IsSupported` (static), `Calibrate` (no `State`) | Complete API shell, permanent stub; `State` is `NOXNA` |
| `Motion` | + `IsSupported` (static), `Calibrate` (no `State`) | Complete API shell, permanent stub; `State` is `NOXNA` |
| `AccelerometerReading` | `Acceleration` (Vector3), `Timestamp` | Complete |
| `GyroscopeReading` | `RotationRate` (Vector3), `Timestamp` | Complete |
| `CompassReading` | `HeadingAccuracy`, `MagneticHeading`, `MagnetometerReading`, `Timestamp`, `TrueHeading` | Complete |
| `MotionReading` | `Attitude`, `DeviceAcceleration`, `DeviceRotationRate`, `Gravity`, `Timestamp` | Complete |
| `AttitudeReading` | `Pitch`, `Roll`, `Yaw`, `Quaternion`, `RotationMatrix`, `Timestamp` | Complete |
| `SensorState` | `NotSupported`, `Ready`, `Initializing`, `NoData`, `NoPermissions`, `Disabled` | Complete (medium-confidence enum values, no direct MSDN enum page found) |
| `SensorFailedException` | message ctor, `ErrorId` | Complete |
| `AccelerometerFailedException` | inherits `SensorFailedException` | Complete |

---

## Backend Strategy

- **Vibration:** keep SDL3 haptic as the primary cross-platform path (desktop +
  Android, per Current Repository Findings above). Only add an
  `IDeviceVibrationBackend` seam and a dedicated `AndroidVibrationBackend` if Phase 2's
  investigation task (DEVICES-0031) proves SDL3's Android haptic driver does **not**
  reach the phone's real vibrator, or does not support amplitude control /
  `VibrationEffect.createWaveform()`-style patterns SDL3 cannot express. Do not build a
  parallel JNI bridge speculatively.
- **Sensors (Accelerometer/Gyroscope):** unchanged. `Detail::SdlSensorSubsystem<TSensor>`
  stays the only implementation; this plan does not touch it except via
  `Detail::AndroidSensorOrientation.hpp`'s hardware-verification loop (Phase 5).
- **Sensors (Compass/Motion):** new native backends, one interface per sensor
  (`ICompassBackend`, `IMotionBackend`, from `docs/devices-native-backend-design.md`),
  selected at construction time by a compile-time platform switch inside `Compass`/
  `Motion` only — never a project-wide macro. Desktop/unimplemented platforms keep
  today's exact `NotSupported`/throws-`SensorFailedException` behavior forever (the
  design doc's Migration Plan step 3). Android is the first and only platform target
  for this plan (Phase 6–8); iOS backend code is not written here (no Apple toolchain in
  this environment — Phase 8 only updates the design doc's iOS section, never adds
  `.mm`/`.swift` files) but the interface must not preclude it later.

## Android Native Strategy

- One shared bridge (`Detail::AndroidSensorBridge` or equivalent — exact name decided in
  Phase 6's design task) wraps `android.hardware.SensorManager` registration,
  listener callbacks, and thread-safe delivery into `SensorBase<T>`'s existing
  `setCurrentValueProperty()`/dispatch path — reusing `ClaimDisposalOnce()`/
  `WaitForDisposalToComplete()`, never inventing a second disposal scheme (mandatory
  per `docs/devices-native-backend-design.md` "Lifecycle, both platforms").
- Compass backend: `TYPE_MAGNETIC_FIELD` + `TYPE_ACCELEROMETER` (or
  `TYPE_ROTATION_VECTOR`) → `MagneticHeading`; `TrueHeading` stays honestly
  unavailable/equal-to-magnetic until `System.Device.Location` exists (never fudge this
  — see Non-Goals).
- Motion backend: `TYPE_ROTATION_VECTOR`/`TYPE_GAME_ROTATION_VECTOR` → `Attitude`;
  `TYPE_GRAVITY` → `Gravity`; `TYPE_LINEAR_ACCELERATION` → `DeviceAcceleration`;
  `TYPE_GYROSCOPE` → `DeviceRotationRate`.
- No permission prompt is required for `SensorManager` registration on current Android
  (re-verify at implementation time, per the design doc's own caveat) — vibration needs
  `android.permission.VIBRATE` only if Phase 2 concludes a native path is needed.
- APK packaging (Phase 9) is additive: adapt SDL's vendored
  `third_party/SDL/android-project` template, do not invent a new build system.

## SDL3 Strategy

- `Accelerometer`/`Gyroscope`/`VibrateController`'s existing SDL3 usage is the
  compatibility and hardening baseline — Phase 1/4/5 tasks below verify and close small
  gaps in it (false-positive `IsSupported`, error-path handling, shutdown ordering) but
  do not replace it.
- SDL3 has no compass/motion-fusion API on any platform — confirmed unchanged across
  Phases 6–9 audits and re-confirmed as a Phase 0 task here. Do not attempt to add one;
  Compass/Motion's real backend is native-only (Android Strategy above), never SDL.

---

## Task List

Every task below is independently reviewable and sized for one focused iteration.
Checkbox state reflects this plan's creation time (2026-07-05); update as work lands,
same convention as `plan_devices_phase2.md`–`plan_devices_phase9.md`.

### Phase 0: Repository Audit and Compatibility Matrix

- [x] DEVICES-0001 — Re-confirm namespace-wide file inventory is current (2026-07-05: re-ran the inventory; 56 files, identical to this plan's "Current Repository Findings" set; confirmed via `git diff --stat` between the commit that introduced this plan and HEAD touching none of the four Devices directories)
  - **Area:** Audit
  - **Files:** none changed; read `include/Microsoft/Devices/**`, `src/Microsoft/Devices/**`, `tests/Microsoft/Devices/**`, `examples/demo_devices/**`
  - **Required behavior:** Produce a plain file list (already captured in "Current Repository Findings" above) confirming no file was added/removed/renamed since `plan_devices_phase9.md` closed.
  - **Acceptance criteria:** List matches this plan's "Current Repository Findings" file set exactly, or documents the delta.
  - **Tests:** N/A (audit-only task)
  - **Dependencies:** none

- [x] DEVICES-0002 — Build a fresh `VibrateController` API matrix vs. WP7 `Microsoft.Devices.VibrateController` (2026-07-05: re-read `include/Microsoft/Devices/VibrateController.hpp` fresh; confirmed `getDefaultProperty()` (never-null singleton), `Start(const TimeSpan&)`, `Stop()` match the real WP7 shape exactly. `NOXNA` extensions confirmed correctly tagged: `Start(TimeSpan, float intensity)`, `getIsSupportedProperty()`, `getDeviceNameProperty()`, `StartLeftRight(float, float, TimeSpan)`. No drift from `AUDIT.md`'s existing `VibrateController` row)
  - **Area:** Audit / Compatibility matrix
  - **Files:** `include/Microsoft/Devices/VibrateController.hpp` (read-only); output into `AUDIT.md`'s existing Devices table row, no new file
  - **Required behavior:** Line-by-line confirm `getDefaultProperty()`, `Start(TimeSpan)`, `Stop()` match the documented WP7 shape; list all 4 `NOXNA` extensions explicitly as non-XNA.
  - **Acceptance criteria:** Matrix entry has one row per real member plus one row per `NOXNA` member, each marked "match"/"deviation, documented".
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0003 — Build a fresh `SensorBase<TSensorReading>` API matrix (2026-07-05: re-read `SensorBase.hpp` fresh; confirmed `CurrentValue`/`IsDataValid`/`TimeBetweenUpdates`/`CurrentValueChanged`/`Start()`/`Stop()`/`Dispose()` shape; confirmed the base class has no `IsSupported`/`State` member — both are per-subclass statics/properties only, matching MSDN `hh239261`)
  - **Area:** Audit / Compatibility matrix
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp` (read-only)
  - **Required behavior:** Confirm `CurrentValue`/`IsDataValid`/`State`(absence)/`TimeBetweenUpdates`/`CurrentValueChanged`/`Start()`/`Stop()`/`Dispose()` shape against MSDN `hh239261`-family pages; explicitly note the base class has no `IsSupported`/`State` (those are per-subclass).
  - **Acceptance criteria:** Written matrix distinguishes base-class members from subclass-only members.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0004 — Build a fresh `Accelerometer` API matrix (2026-07-05: re-read `Accelerometer.hpp` fresh; confirmed ctor, static `getIsSupportedProperty()`, `CurrentValue`/`CurrentValueChanged` (inherited), legacy `ReadingChanged` all present and real-shaped; `getStateProperty()` confirmed `NOXNA`. Unit-conversion question (SDL reports m/s², WP7 `Acceleration` is documented in g) is real and NOT resolved by this matrix task — explicitly deferred to Phase 5's DEVICES-0063, not silently assumed correct here)
  - **Area:** Audit / Compatibility matrix
  - **Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp` (read-only)
  - **Required behavior:** Confirm ctor, static `IsSupported`, `CurrentValue`, `CurrentValueChanged`, legacy `ReadingChanged`, units (m/s² SDL raw → g XNA), timestamp semantics, lifecycle against documented shape; list every `NOXNA` test-only hook as non-API.
  - **Acceptance criteria:** Matrix confirms real vs. `NOXNA` member split; unit-conversion note is explicit (SDL reports m/s², WP7 `Acceleration` is in g — confirm which conversion, if any, `DispatchSensorReading()` currently performs).
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0005 — Build a fresh `Gyroscope` API matrix (2026-07-05: re-read `Gyroscope.hpp` fresh; confirmed same shape as `Accelerometer` minus the legacy event (correctly absent — real `Gyroscope` never had one); `getStateProperty()` confirmed `NOXNA`. Unit question (rad/s) deferred to DEVICES-0064, same reasoning as DEVICES-0004)
  - **Area:** Audit / Compatibility matrix
  - **Files:** `include/Microsoft/Devices/Sensors/Gyroscope.hpp` (read-only)
  - **Required behavior:** Same as DEVICES-0004 but for `Gyroscope`; confirm no legacy event exists (correct, matches real API) and `State` is `NOXNA`.
  - **Acceptance criteria:** Matrix explicit that `Gyroscope` has one fewer real member than `Accelerometer` (no `ReadingChanged`), by design.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0006 — Build a fresh `Compass`/`CompassReading`/`CalibrationEventArgs` API matrix (2026-07-05: re-read all three headers fresh; confirmed ctor, static `getIsSupportedProperty()`, `Calibrate`, `CompassReading`'s `HeadingAccuracy`/`MagneticHeading`/`MagnetometerReading`/`TrueHeading`/`Timestamp` (all private-setter + `friend class Compass`, matching real `internal set`); confirmed `Start()` unconditionally throws `SensorFailedException` — an honest, documented stub, not a compatibility gap)
  - **Area:** Audit / Compatibility matrix
  - **Files:** `include/Microsoft/Devices/Sensors/{Compass,CompassReading,CalibrationEventArgs}.hpp` (read-only)
  - **Required behavior:** Confirm ctor, static `IsSupported`, `CurrentValue`, `CurrentValueChanged`, `Calibrate`, `MagneticHeading`/`TrueHeading`/`HeadingAccuracy`/`MagnetometerReading` shape; confirm stub semantics (`Start()` always throws) are the honest, documented state, not a compatibility gap.
  - **Acceptance criteria:** Matrix explicitly separates "API-complete" from "runtime-implemented" (they are not the same claim for this class).
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0007 — Build a fresh `Motion`/`MotionReading`/`AttitudeReading` API matrix (2026-07-05: re-read all three headers fresh; confirmed ctor, static `getIsSupportedProperty()`, `Calibrate` (shared naming/shape with `Compass`), `MotionReading`'s `Attitude`/`DeviceAcceleration`/`DeviceRotationRate`/`Gravity`/`Timestamp`, `AttitudeReading`'s `Pitch`/`Roll`/`Yaw`/`Quaternion`/`RotationMatrix`/`Timestamp` — all private-setter + `friend class Motion`. Same honest-stub note as DEVICES-0006 applies)
  - **Area:** Audit / Compatibility matrix
  - **Files:** `include/Microsoft/Devices/Sensors/{Motion,MotionReading,AttitudeReading}.hpp` (read-only)
  - **Required behavior:** Confirm ctor, static `IsSupported`, `CurrentValue`, `CurrentValueChanged`, `Calibrate` (shared with Compass by design), `Attitude`/`DeviceAcceleration`/`DeviceRotationRate`/`Gravity`, and `AttitudeReading`'s `Pitch`/`Roll`/`Yaw`/`Quaternion`/`RotationMatrix` shape against documented members.
  - **Acceptance criteria:** Matrix notes `Motion`'s real dependency chain (requires Compass conceptually) and that this is why it is also stubbed.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0008 — Build a fresh exceptions/enums API matrix (`SensorFailedException`, `AccelerometerFailedException`, `SensorState`, `ISensorReading`, `SensorReadingEventArgs<T>`, `AccelerometerReadingEventArgs`) (2026-07-05: re-read all six headers fresh; confirmed `SensorFailedException`'s 3 ctors + `getErrorIdProperty()`, `AccelerometerFailedException`'s matching 3-ctor mirror, `SensorState`'s 6 values (`NotSupported`/`Ready`/`Initializing`/`NoData`/`NoPermissions`/`Disabled`), `ISensorReading`'s single pure-virtual `getTimestampProperty()`. `SensorState`'s enum values remain medium-confidence — no direct MSDN enum page was ever found, only a MonoGame cross-check — flagged again here as a standing, accepted risk, not silently upgraded to high confidence)
  - **Area:** Audit / Compatibility matrix
  - **Files:** all six headers (read-only)
  - **Required behavior:** Confirm each type's members/ctors against documented shape; explicitly flag `SensorState`'s medium-confidence status (no direct MSDN enum page found, only MonoGame cross-check) as a standing, accepted risk, not a bug to silently "fix" by inventing values.
  - **Acceptance criteria:** Matrix entry for each of the 6 types with a confidence level (high/medium) noted per `AUDIT.md`'s existing convention.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0009 — Inventory every `NOXNA`-tagged member in the Devices namespace (2026-07-05: grepped `include/Microsoft/Devices` fresh. 30 explicitly-tagged members: `VibrateController`'s 4 extensions (`Start(TimeSpan,intensity)`, `getIsSupportedProperty()`, `getDeviceNameProperty()`, `StartLeftRight()`); `Accelerometer`'s 8 test-only hooks; `Gyroscope`'s identical 8 test-only hooks + its `NOXNA getStateProperty()` (9); `Compass`/`Motion`'s `getStateProperty()` (1 each); 7 explicit `NOXNA GetTypeName()` overrides on the reading/event-args/calibration classes. Confirmed `Accelerometer::getStateProperty()` is correctly **not** `NOXNA`-tagged — it is the one real WP7 `State` member. **New observation, not a bug to fix here:** `Accelerometer`/`Gyroscope`/`Compass`/`Motion`'s `GetTypeName()` overrides come from the shared `GetTypeNameHPP()` macro (`sharp-runtime`'s `System/Object.hpp`), which does not itself emit a textual `NOXNA` tag — meaning these 4 declarations satisfy `CHECKLIST.md`'s `GetTypeName()`-must-be-`NOXNA` rule only in spirit, not literally in source text. This is a project-wide macro-convention question well beyond `Microsoft::Devices`, not something to fix from this plan.)
  - **Area:** Audit
  - **Files:** all headers under `include/Microsoft/Devices/**` (read-only)
  - **Required behavior:** Grep for `NOXNA` across the namespace; produce a flat list of every tagged member with a one-line justification for each (already mostly documented in-source; this task just aggregates it).
  - **Acceptance criteria:** List is complete (verified via `grep -rn NOXNA include/Microsoft/Devices` matching the produced list count exactly).
  - **Tests:** N/A
  - **Dependencies:** none

- [x] DEVICES-0010 — Inventory every test-only (`*ForTesting`/`InjectSynthetic*`) hook (2026-07-05: counted call sites for all 8 hooks in both `AccelerometerTests.cpp` and `GyroscopeTests.cpp`. **Finding: `UnregisterStartedInstanceForTesting()` has zero call sites in either test file**, on both `Accelerometer` and `Gyroscope` — every other hook has ≥1. Flagged, not removed, per this task's own acceptance criteria ("may be intentionally reserved for a future test") — most likely reserved for a not-yet-written test of the specific "instance removed from `startedInstances_` before being disposed" interleaving that `RegisterStartedInstanceForTesting`'s own doc comment describes setting up for. No action taken; a future task could add the missing test or drop the hook, but that decision is out of scope for this audit-only task.)
  - **Area:** Audit
  - **Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp` (read-only)
  - **Required behavior:** List all 7 test-only hooks (`InjectSyntheticSensorUpdate`, `SetStartedForTesting`, `SetSupportedForTesting`, `GetSubsystemHeldForTesting`, `SetDisposalCleanupHookForTesting`, `RegisterStartedInstanceForTesting`, `UnregisterStartedInstanceForTesting`, `DispatchToInstancesForTesting` — 8 total), confirm each is exercised by at least one test in `AccelerometerTests.cpp`.
  - **Acceptance criteria:** Every listed hook has ≥1 call site in the test file; any orphaned hook is flagged (not removed without asking — may be intentionally reserved for a future test).
  - **Tests:** N/A (verifies existing tests)
  - **Dependencies:** DEVICES-0001

- [x] DEVICES-0011 — Inventory `Microsoft::Devices` build integration (2026-07-05: confirmed `CNA_SOURCES`/`CNA_TEST_SOURCES` are collected via `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` — Devices sources/tests need no manual per-file registration, low risk of silent exclusion. **Confirmed the suspected gap from this plan's Current Repository Findings is real:** `cna_demo_devices` (`CMakeLists.txt:430`) sits inside the umbrella `if(CNA_BUILD_EXAMPLES)` block (line 278) with no `NOT EMSCRIPTEN AND NOT ANDROID` guard of its own, unlike `cna_demo_xact` (guarded, `NOT EMSCRIPTEN AND NOT ANDROID`, closed at the matching `endif()` before `cna_demo_devices`'s block starts). Confirmed via exact line numbers this session, feeding directly into Phase 9's DEVICES-0120/0121.)
  - **Area:** Audit / Build
  - **Files:** `CMakeLists.txt` (read-only)
  - **Required behavior:** Confirm which targets compile Devices sources (`CNA`, `CnaTests`, `cna_demo_devices`) and under what platform guards; specifically confirm/deny whether `cna_demo_devices` is actually excluded for `ANDROID`/`EMSCRIPTEN` (Current Repository Findings above suspects it is not, unlike `cna_demo_xact`).
  - **Acceptance criteria:** Written confirmation (with exact `CMakeLists.txt` line numbers) of every guard affecting Devices targets.
  - **Tests:** N/A
  - **Dependencies:** none

- [x] DEVICES-0012 — Inventory Devices-related docs for staleness (2026-07-05: spot-checked one claim per doc. `docs/devices-build.md`'s NDK path (`~/Android/Sdk/ndk/30.0.14904198`) still exists. `docs/devices-hardware-checklist.md`'s `Medium_Phone` AVD still exists. `sharp-runtime/src/System/TimeSpan.cpp:55`'s `copy_count++` in the copy constructor still exists exactly as described (the one accepted, out-of-scope TSan finding). **Major environment-change finding, re-verify before trusting any prior "blocked" claim: `/dev/kvm` now EXISTS and is openable read-write in this container** (`ls -la /dev/kvm` → `crw-rw----+ 1 root kvm`, confirmed actually openable via a direct `os.open()` test, not just a stat check) — every prior phase since Phase 4 (P9-4 most recently) found this device node absent, which was the sole documented blocker for the x86_64 `Medium_Phone` AVD. **This directly changes Phase 9's starting assumption** — DEVICES-0126 must re-attempt the emulator launch for real rather than assuming the old failure still holds. Both docs otherwise still accurate as of this session.)
  - **Area:** Audit / Docs
  - **Files:** `docs/devices-build.md`, `docs/devices-hardware-checklist.md`, `docs/devices-native-backend-design.md`, `docs/location-future-plan.md` (read-only)
  - **Required behavior:** Confirm each doc's claims still match the current code (e.g. test counts, sanitizer results, Android NDK path) by spot-re-running one representative command from each doc.
  - **Acceptance criteria:** Each doc gets a one-line "still accurate as of 2026-07-05" or a list of stale claims to fix in Phase 10.
  - **Tests:** N/A
  - **Dependencies:** none

- [x] DEVICES-0013 — Confirm SDL3 has no compass/magnetometer API on any platform (re-verify, don't just trust prior phases) (2026-07-05: grepped `third_party/SDL/include/SDL3/SDL_sensor.h`'s `SDL_SensorType` enum fresh — 8 values total: `INVALID`/`UNKNOWN`/`ACCEL`/`GYRO`/`ACCEL_L`/`GYRO_L`/`ACCEL_R`/`GYRO_R` (the last 4 are Joy-Con-specific accel/gyro, not a new sensor kind). No magnetometer/compass value exists. Confirmed unchanged from every prior phase's finding.)
  - **Area:** Audit
  - **Files:** `third_party/SDL/include/SDL3/SDL_sensor.h` (read-only)
  - **Required behavior:** Grep `SDL_SensorType` enum for any magnetometer/compass-like value; confirm none exists (only accel/gyro types).
  - **Acceptance criteria:** Documented enum dump with an explicit "no magnetometer type present" conclusion, dated this session.
  - **Tests:** N/A
  - **Dependencies:** none

- [x] DEVICES-0014 — Confirm SDL3's Android haptic backend claim (gating fact for Phase 2) (2026-07-05: read `third_party/SDL/src/haptic/android/SDL_syshaptic.c` (JNI trampoline: `Android_JNI_PollHapticDevices()`/`Android_JNI_HapticRun()`/`Android_JNI_HapticStop()`) and the actual Java implementation, `third_party/SDL/android-project/app/src/main/java/org/libsdl/app/SDLControllerManager.java`. **Decisive, definitive confirmation — `VibrateController.cpp`'s own comment is correct and then some:** `SDLHapticHandler.pollHapticDevices()` (line ~703) explicitly queries `Context.VIBRATOR_SERVICE` (the phone's own built-in `Vibrator`, via `hasVibrator()`) as its own device, separate from any connected-controller `InputDevice.getVibratorManager()` device — both are real, both already wired. `SDLHapticHandler_API26.run()` (line ~634) already does **exactly** what this plan's Phase 3 (DEVICES-0038–0047) set out to build from scratch: `intensity == 0.0f` → calls `stop()` rather than sending zero amplitude (resolves DEVICES-0030/0043's open question); `Math.round(intensity * 255)` clamped to `[1,255]`; `VibrationEffect.createOneShot(length, vibeValue)` wrapped in try/catch, falling back to `vib.vibrate(length)` (`DEFAULT_AMPLITUDE`) if anything fails. `SDLHapticHandler_API31` layers `VibratorManager`/multi-vibrator support on top for API 31+. **This resolves Phase 2's DEVICES-0031 gate: a native Android vibration backend (Phase 3) would be redundant, duplicate work — SDL3 already implements the phone-vibrator path with amplitude control end to end.** Recorded here in full since DEVICES-0031 formally closes this in Phase 2, but the evidence is conclusive now, not deferred.)
  - **Area:** Audit
  - **Files:** `third_party/SDL/src/haptic/android/SDL_syshaptic.c`, `third_party/SDL/src/core/android/SDL_android.c` (read-only)
  - **Required behavior:** Read the actual Android haptic backend source to confirm/deny `VibrateController.cpp`'s own comment claim: that `SDL_INIT_HAPTIC` on Android registers the phone's real vibrator (via `Context.VIBRATOR_SERVICE`/`Vibrator`) as an `SDL_Haptic` device automatically, with amplitude control, with no CNA-side JNI code.
  - **Acceptance criteria:** Written yes/no answer with the exact source evidence (function/JNI call names), feeding directly into Phase 2's DEVICES-0031 decision task.
  - **Tests:** N/A
  - **Dependencies:** none

- [x] DEVICES-0015 — Write Phase 0 findings into `AUDIT.md` (2026-07-05: appended an additive-only "Phase 0 audit addendum" note after the existing `Microsoft::Devices::Sensors` section, covering the SDL-Android-haptic finding (DEVICES-0014), the orphaned test hook (DEVICES-0010), and the `/dev/kvm` environment change (DEVICES-0012). No existing history rewritten or deleted. **Phase 0 (DEVICES-0001–0015) is now fully closed.**)
  - **Area:** Docs
  - **Files:** `AUDIT.md`
  - **Required behavior:** Append a dated note under the existing `Microsoft::Devices::Sensors` section (do not rewrite the existing table) referencing this plan's Phase 0 tasks and any deltas found (e.g. if DEVICES-0011 found the Android demo-build guard gap).
  - **Acceptance criteria:** `AUDIT.md` diff is additive only; existing history is not deleted or rewritten.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0001 through DEVICES-0014

### Phase 1: `VibrateController` Correctness and Tests

- [x] DEVICES-0016 — Re-verify `getDefaultProperty()` singleton behavior under repeated calls (2026-07-05: existing `GetDefaultPropertyReturnsSameInstance` only compared 2 bare calls. Added `GetDefaultPropertyReturnsSameInstanceAcrossUsage`, asserting identity holds across a real `Start()`/`StartLeftRight()`/`Stop()` sequence. 31/31 `VibrateControllerTests` green.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`, `tests/Microsoft/Devices/VibrateControllerTests.cpp`
  - **Required behavior:** Confirm two calls to `getDefaultProperty()` return the identical pointer; never null.
  - **Acceptance criteria:** Existing/added test asserts pointer equality across ≥3 calls, including after a prior `Start()`/`Stop()`/`StartLeftRight()` sequence.
  - **Tests:** `VibrateControllerTests.DefaultReturnsSameInstance` (add if missing)
  - **Dependencies:** none

- [x] DEVICES-0017 — Re-verify duration validation boundaries (2026-07-05: confirmed all 4 boundary cases already exist and pass — `StartWithZeroDurationDoesNotThrow`, `StartWithExactlyMaxDurationDoesNotThrow`, `StartWithNegativeDurationThrows`, `StartWithOverlongDurationThrows`. No gap; no change needed.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** `Start(TimeSpan)` throws `ArgumentOutOfRangeException` for duration < 0 and > 5s; boundary values (`Zero`, `FromSeconds(5)`) do not throw.
  - **Acceptance criteria:** All 4 boundary cases (just-under-zero, zero, exactly-5s, just-over-5s) covered.
  - **Tests:** confirm `StartWithNegativeDurationThrows`/`StartWithOverlongDurationThrows` exist and add the two missing exact-boundary (non-throwing) cases if absent.
  - **Dependencies:** none

- [x] DEVICES-0018 — Re-verify zero-duration `Start()` behavior (2026-07-05: confirmed `StartWithZeroDurationDoesNotThrow` exists and passes. Code read confirms `TimeSpan::Zero` is **not** special-cased in `Start()` — it is passed straight through to `SDL_PlayHapticRumble(device, intensity, 0)`; SDL's own exact semantics for a zero-millisecond rumble (immediate no-op vs. some minimum duration) were not independently verified against SDL3 rumble-effect source, and are left as an honest open point for hardware verification, not assumed either way.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** `Start(TimeSpan::Zero)` does not throw and does not crash regardless of hardware presence; document whether it produces an audible/felt effect or is effectively a no-op (SDL rumble with 0 duration).
  - **Acceptance criteria:** Test asserts no throw/no crash; behavior documented in the method's doc comment if not already.
  - **Tests:** `VibrateControllerTests.StartWithZeroDurationDoesNotThrow`
  - **Dependencies:** none

- [x] DEVICES-0019 — Re-verify repeated `Start()` calls stop the prior effect first (2026-07-05: confirmed `Start()`/`Start(duration,intensity)` call `DestroyLeftRightEffectIfAny()` before playing their own rumble (`VibrateController.cpp` line 320). The exact `StartLeftRight()`-then-`Start()` direction is already covered by `StartLeftRightThenStartThenStopDoesNotThrow`. No gap; no change needed.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** Calling `Start()` while a previous `Start()`/`StartLeftRight()` effect is still running replaces it (confirmed behavior per `plan_devices_phase3.md` Task P3-5) rather than running both simultaneously.
  - **Acceptance criteria:** Existing sequence-safety tests still pass; add one if the exact `Start()`-replaces-`StartLeftRight()` direction is untested.
  - **Tests:** `VibrateControllerTests.StartLeftRightThenStartStopsLeftRightEffect` (add if missing)
  - **Dependencies:** none

- [x] DEVICES-0020 — Re-verify `Stop()` before any `Start()` is a safe no-op (2026-07-05: confirmed `StopBeforeAnyStartDoesNotThrow` exists and passes; `Stop()`'s own body correctly guards on `g_haptic != nullptr` before touching anything. No gap.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** Calling `Stop()` on a fresh process (no prior `Start()`) does not crash or throw.
  - **Acceptance criteria:** Test confirms no throw/crash.
  - **Tests:** confirm existing coverage in `VibrateControllerTests.cpp`; add if missing.
  - **Dependencies:** none

- [x] DEVICES-0021 — Re-verify `Stop()` after a failed/absent haptic device is a safe no-op (2026-07-05: re-ran, not just read, the full `VibrateControllerTests` suite in this hardware-less container — 31/31 green, including the new `UnsupportedEnvironmentFullContract` (DEVICES-0028) which asserts this exact sequence explicitly in one place.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** On a machine with no haptic hardware (this container), `Start()` then `Stop()` never crash.
  - **Acceptance criteria:** Confirmed by existing `GetIsSupportedPropertyDoesNotCrash`-style tests; explicitly re-run in this session per Section 7 of `NEXT.md`.
  - **Tests:** re-run, do not just read, `VibrateControllerTests.*`
  - **Dependencies:** none

- [x] DEVICES-0022 — Re-verify destructor/shutdown resets `g_leftRightEffectId` (2026-07-05: confirmed `~VibrateController()` (lines 256-282) still resets `g_leftRightEffectId = -1` per Task P8-6. No regression; no change needed.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** Per `plan_devices_phase8.md` Task P8-6, confirm `~VibrateController()` resets `g_leftRightEffectId` to `-1` after closing `g_haptic` (a one-line defensive fix already applied) — Phase 1 here only needs to re-confirm it is still present, not re-apply it.
  - **Acceptance criteria:** Source inspection confirms the reset line exists; no code change needed unless it regressed.
  - **Tests:** N/A unless a regression is found (then add a targeted test)
  - **Dependencies:** none

- [x] DEVICES-0023 — Re-verify gamepad-rumble exclusion still holds (2026-07-05: confirmed `IsConnectedGamepadHapticDevice()` still correlates by `SDL_OpenHapticFromJoystick()`/`SDL_GetHapticID()` (ID-based, not name-based), matching Task P4-10's description exactly. No hardware to exercise it against; unchanged.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** `IsConnectedGamepadHapticDevice()`'s ID-correlation (`SDL_OpenHapticFromJoystick()`) still excludes any haptic device that is also a connected joystick, so `VibrateController` never competes with `GamePad::SetVibration()`.
  - **Acceptance criteria:** Existing unit-level exclusion logic re-read and confirmed unchanged; flag if it drifted from `plan_devices_phase4.md` Task P4-10's description.
  - **Tests:** existing coverage; add a fake/mock joystick-ID test if the current suite only reasons about it indirectly.
  - **Dependencies:** none

- [x] DEVICES-0024 — Re-verify SDL haptic device enumeration handles `SDL_GetHaptics()` returning `NULL`/0 (2026-07-05: confirmed `OpenFirstHapticDevice()` (lines 175-207) guards `haptics == nullptr || hapticCount <= 0` and never calls `SDL_free(nullptr)`. No gap.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** `OpenFirstHapticDevice()` must not crash or leak when `SDL_GetHaptics()` returns `nullptr` or `hapticCount <= 0` (already handled per the code read in Phase 0 — confirm, don't re-implement).
  - **Acceptance criteria:** Confirmed via code inspection; no `SDL_free(nullptr)` call path.
  - **Tests:** N/A unless a gap is found
  - **Dependencies:** none

- [x] DEVICES-0025 — Re-verify haptic subsystem ownership/ref-counting (2026-07-05: confirmed exactly one `g_subsystemHeld`-gated `SDL_InitSubSystem(SDL_INIT_HAPTIC)` call (`EnsureHapticSubsystemInitialized()`) paired with exactly one `SDL_QuitSubSystem()` in the destructor; no double-init/double-quit path across `Start()`/`StartLeftRight()`/destructor. No gap.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** Exactly one `SDL_InitSubSystem(SDL_INIT_HAPTIC)`/`SDL_QuitSubSystem()` pair per process lifetime (singleton), matching `GraphicsDevice`'s established convention (Task P6-6).
  - **Acceptance criteria:** Confirmed no double-init/double-quit path exists across `Start()`/`Stop()`/`StartLeftRight()`/destructor.
  - **Tests:** N/A unless a gap is found
  - **Dependencies:** none

- [x] DEVICES-0026 — Add explicit mutex-contention test for concurrent `Start()`/`Stop()`/`StartLeftRight()` (2026-07-05: `ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock` already exists (8 threads × 20 iterations, all 5 public methods exercised). Looped the full `VibrateControllerTests.*` filter 40/40 times — all clean, no failures. `devices-tsan` re-run deferred to Phase 10's final gate (DEVICES-0140) rather than repeated per-task; no new concurrency code was added this task, only a new single-threaded contract test.)
  - **Area:** VibrateController
  - **Files:** `tests/Microsoft/Devices/VibrateControllerTests.cpp`
  - **Required behavior:** Confirm the single mutex (Task P4-9) serializes all public methods correctly under concurrent calls from multiple threads (no crash/UB), looped per `NEXT.md`'s "loop 20-60+ iterations" rule since this touches concurrency.
  - **Acceptance criteria:** New test passes 40/40 loop iterations under a plain build, and clean under `devices-tsan`.
  - **Tests:** `VibrateControllerTests.ConcurrentStartStopStartLeftRightDoesNotCrash`
  - **Dependencies:** none

- [x] DEVICES-0027 — `getIsSupportedProperty()` false-positive audit (2026-07-05: code inspection (no `VibrateController`-specific test hook exists, and none is added — per the task's own allowance) confirms `AcquireHapticDeviceForProbe()`'s `openedTemporary` tracking is correct: `getIsSupportedProperty()`/`getDeviceNameProperty()` both close any temporarily-opened device before returning (lines 345-348, 370-373). `getIsSupportedProperty()` can only return `true` when a real, non-gamepad haptic device was genuinely opened. No gap.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** Confirm `getIsSupportedProperty()` only returns `true` when a real, non-gamepad haptic device is genuinely probeable (opens then immediately closes, per `AcquireHapticDeviceForProbe()`) — not merely because `SDL_INIT_HAPTIC` succeeded.
  - **Acceptance criteria:** Code-read confirms probe-then-close discipline; add a test asserting `getIsSupportedProperty()` is `false` in this hardware-less container (already covered) and does not itself leave a device open (`GetSubsystemHeldForTesting()`-equivalent check, or code inspection if no such hook exists for `VibrateController`).
  - **Tests:** `VibrateControllerTests.GetIsSupportedPropertyDoesNotHoldDeviceOpen` (add if missing)
  - **Dependencies:** none

- [x] DEVICES-0028 — Add unsupported/no-device-environment regression test explicitly labeled as such (2026-07-05: added `VibrateControllerTests.UnsupportedEnvironmentFullContract`, asserting `IsSupported`/`DeviceName`/`Start`/`Start(duration,intensity)`/`StartLeftRight`/`Stop`'s full no-hardware contract together in one place, with a `GTEST_SKIP()` guard if this container ever does have real haptic hardware. Cross-referenced from `docs/devices-hardware-checklist.md` via this plan.)
  - **Area:** VibrateController
  - **Files:** `tests/Microsoft/Devices/VibrateControllerTests.cpp`
  - **Required behavior:** One test explicitly documents (in its name/body comment) that this container has zero haptic hardware and asserts the full unsupported-path contract (`IsSupported == false`, `DeviceName == ""`, `Start()`/`Stop()`/`StartLeftRight()` are silent no-ops) in one place, for a future hardware session to compare against.
  - **Acceptance criteria:** Test passes; doubles as living documentation cross-referenced from `docs/devices-hardware-checklist.md`.
  - **Tests:** `VibrateControllerTests.UnsupportedEnvironmentFullContract`
  - **Dependencies:** DEVICES-0027

- [x] DEVICES-0029 — Investigate a fake/mock haptic backend seam for deterministic amplitude/waveform tests (2026-07-05: recommendation is **proceed, but as Phase 2's own `IDeviceVibrationBackend` work (DEVICES-0032–0037), not a small ad-hoc seam added here** — building a one-off mockable function pointer now and then replacing it with the real backend-abstraction interface in Phase 2 would be double work / a throwaway abstraction, against this project's own "don't build abstractions you'll immediately replace" convention. No code added by this task.)
  - **Area:** VibrateController
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp` (read-only investigation)
  - **Required behavior:** Determine whether a mockable seam is feasible without a large refactor (e.g. an injectable function-pointer for `SDL_PlayHapticRumble`) — this is an investigation task, not an implementation mandate; only proceed to implement in a follow-up task if the seam is small and low-risk.
  - **Acceptance criteria:** Written recommendation: proceed / do not proceed, with reasoning tied to this project's "don't add abstractions beyond what's needed" convention.
  - **Tests:** N/A
  - **Dependencies:** none

- [x] DEVICES-0030 — Document `Start(TimeSpan, intensity)` intensity=0.0f behavior explicitly (2026-07-05: confirmed via code read that `intensity=0.0f` is **not** special-cased into an implicit `Stop()` — it still uploads/plays a zero-strength SDL rumble effect for the full requested duration. Documented this precisely in `VibrateController.hpp`'s `Start(TimeSpan, intensity)` Doxygen comment. `StartWithIntensityZeroDoesNotThrow` already covers no-crash; behavior is now also explicitly documented, closing the acceptance criteria.)
  - **Area:** VibrateController
  - **Files:** `include/Microsoft/Devices/VibrateController.hpp`, `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** Confirm and document whether `intensity = 0.0f` behaves as a true no-op (equivalent to not calling `Start()` at all) or actually uploads a zero-strength effect; this matters for the Android-vibration-mapping notes in Phase 2/3.
  - **Acceptance criteria:** Doxygen comment states the exact behavior; a test asserts it (no crash either way).
  - **Tests:** `VibrateControllerTests.StartWithZeroIntensityDoesNotCrash`
  - **Dependencies:** none

### Phase 2: Backend Abstraction for Vibration

- [ ] DEVICES-0031 — **Gating decision task:** resolve whether a native Android vibration backend is needed at all
  - **Area:** VibrateController / Android
  - **Files:** none changed; reads `third_party/SDL/src/haptic/android/SDL_syshaptic.c`, `third_party/SDL/src/core/android/SDL_android.c`, and DEVICES-0014's findings
  - **Required behavior:** Produce a definitive written answer: does SDL3's existing Android haptic backend already reach `Vibrator.vibrate()` with amplitude control? If **yes**, Phase 3 (native Android vibration backend, JNI bridge) is **not needed** — Phase 3's tasks below are downgraded to "verify SDL path on real hardware only" (folded into Phase 9). If **no** (e.g. SDL's Android haptic backend has no amplitude control, or doesn't exist for this NDK/API level, or conflicts with something), Phase 3 proceeds as scoped.
  - **Acceptance criteria:** A written decision (in this plan file, editing the "Backend Strategy" section above and each Phase 3 task's status) with source-code evidence, not a guess. Do not write any new Android vibration code before this task closes.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0014

- [ ] DEVICES-0032 — Design `IDeviceVibrationBackend` interface (only if DEVICES-0031 says Phase 3 is needed)
  - **Area:** VibrateController / Backend abstraction
  - **Files:** `include/Microsoft/Devices/Detail/IDeviceVibrationBackend.hpp` (new)
  - **Required behavior:** Minimal interface: `IsSupported()`, `Start(TimeSpan, float intensity)`, `StartLeftRight(float, float, TimeSpan)`, `Stop()`, `GetDeviceName()`. Mirrors `docs/devices-native-backend-design.md`'s `IDeviceSensorBackend` shape/spirit for consistency.
  - **Acceptance criteria:** Header compiles standalone; no implementation yet; doc comments explain this is `NOXNA`-only internal machinery, not public API.
  - **Tests:** N/A (interface-only, no behavior to test yet)
  - **Dependencies:** DEVICES-0031 (only if it concludes Phase 3 is needed)

- [ ] DEVICES-0033 — Extract existing SDL haptic logic into `SdlHapticVibrationBackend`
  - **Area:** VibrateController / Backend abstraction
  - **Files:** `include/Microsoft/Devices/Detail/SdlHapticVibrationBackend.hpp` (new), `src/Microsoft/Devices/Detail/SdlHapticVibrationBackend.cpp` (new), `src/Microsoft/Devices/VibrateController.cpp` (refactor to use it)
  - **Required behavior:** Move `OpenFirstHapticDevice()`/`IsConnectedGamepadHapticDevice()`/effect-upload logic behind `IDeviceVibrationBackend`, byte-for-byte behavior preserved — this is a pure refactor, not a behavior change.
  - **Acceptance criteria:** All 29 existing `VibrateControllerTests` pass unmodified after the refactor; no new test needed for this step itself (behavior-preservation is the test).
  - **Tests:** full existing `VibrateControllerTests.*` suite, re-run and confirmed green
  - **Dependencies:** DEVICES-0032

- [ ] DEVICES-0034 — Add backend-selection seam to `VibrateController`
  - **Area:** VibrateController / Backend abstraction
  - **Files:** `src/Microsoft/Devices/VibrateController.cpp`
  - **Required behavior:** `VibrateController` holds a `std::unique_ptr<Detail::IDeviceVibrationBackend>` selected at construction: `AndroidVibrationBackend` on `__ANDROID__` (once Phase 3 exists), else `SdlHapticVibrationBackend` everywhere else. No behavior change on any platform without a native backend.
  - **Acceptance criteria:** Desktop behavior byte-for-byte unchanged (same 29 tests green); compiles with a `#if defined(__ANDROID__)` seam even before `AndroidVibrationBackend` exists (guard it out or stub it, per DEVICES-0031's outcome).
  - **Tests:** full existing suite green; one new test confirming the non-Android branch is selected on this build
  - **Dependencies:** DEVICES-0033

- [ ] DEVICES-0035 — Add a no-op `UnsupportedVibrationBackend` fallback
  - **Area:** VibrateController / Backend abstraction
  - **Files:** `include/Microsoft/Devices/Detail/UnsupportedVibrationBackend.hpp` (new)
  - **Required behavior:** For any platform with neither SDL haptic support compiled in nor a native backend, `IsSupported()` returns `false`, `Start()`/`StartLeftRight()`/`Stop()` are silent no-ops — matches today's exact "no haptic hardware" contract.
  - **Acceptance criteria:** Used only as documented architectural completeness; not wired in anywhere unless a real platform gap is found (SDL haptic is expected to always be compiled in today).
  - **Tests:** unit test constructing it directly and asserting the no-op contract
  - **Dependencies:** DEVICES-0032

- [ ] DEVICES-0036 — Re-run full `VibrateControllerTests` suite after the Phase 2 refactor, looped
  - **Area:** Testing
  - **Files:** none
  - **Required behavior:** Per `NEXT.md`'s concurrency-change rule, loop the full `VibrateControllerTests.*` filter 40 times after the Phase 2 refactor lands, plus one `devices-tsan` pass.
  - **Acceptance criteria:** 40/40 clean, TSan clean (aside from the known unrelated `sharp-runtime` finding).
  - **Tests:** `VibrateControllerTests.*` (all)
  - **Dependencies:** DEVICES-0034, DEVICES-0035

- [ ] DEVICES-0037 — Document the backend-abstraction migration in `docs/devices-build.md`
  - **Area:** Docs
  - **Files:** `docs/devices-build.md`
  - **Required behavior:** Add a short section describing the new `IDeviceVibrationBackend` seam, referencing this plan's Phase 2 tasks.
  - **Acceptance criteria:** Doc accurately reflects the new file layout.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0034

### Phase 3: Android Native Vibration Backend (only if DEVICES-0031 concludes it is needed)

- [ ] DEVICES-0038 — Design the JNI bridge surface (`Vibrator`/`VibratorManager`)
  - **Area:** Android / VibrateController
  - **Files:** design note only, appended to `docs/devices-native-backend-design.md`
  - **Required behavior:** Sketch the exact JNI call sequence: `VibratorManager` (API 31+) vs. legacy `Context.VIBRATOR_SERVICE` `Vibrator` (older), `VibrationEffect.createOneShot(long, int)`, `cancel()`. No code yet.
  - **Acceptance criteria:** Design note reviewed against DEVICES-0031's findings — must explain specifically what SDL3's own Android haptic backend does NOT do that justifies this bridge.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0031 (must conclude "needed")

- [ ] DEVICES-0039 — Add `AndroidVibrationBackend` skeleton (compiles, not yet wired)
  - **Area:** Android / VibrateController
  - **Files:** `include/Microsoft/Devices/Detail/AndroidVibrationBackend.hpp` (new), `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp` (new, `#ifdef __ANDROID__`-guarded)
  - **Required behavior:** Class shape only; every method returns/no-ops without touching JNI yet.
  - **Acceptance criteria:** Compiles under the Android NDK cross-compile (library target); does not compile into non-Android builds (verify with `#ifdef` guard).
  - **Tests:** N/A (no behavior yet)
  - **Dependencies:** DEVICES-0038

- [ ] DEVICES-0040 — Implement JNI attach/detach lifecycle helper
  - **Area:** Android / JNI
  - **Files:** `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** Correctly attach the current native thread to the JVM (`AttachCurrentThread`/`DetachCurrentThread`) around any JNI call, avoiding leaks on repeated calls from different threads (SDL's sensor event-watch thread precedent from `Accelerometer`/`Gyroscope` — the same care is needed here).
  - **Acceptance criteria:** Code reviewed against SDL3's own `SDL_android.c` JNI-attach pattern for consistency; no global reference leak across repeated Start/Stop cycles.
  - **Tests:** compile-only test (Android cross-compile); no host-side unit test possible for real JNI calls
  - **Dependencies:** DEVICES-0039

- [ ] DEVICES-0041 — Implement `VibratorManager`/`Vibrator` lookup with API-level branching
  - **Area:** Android / JNI
  - **Files:** `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** `Build.VERSION.SDK_INT >= 31` → `VibratorManager.getDefaultVibrator()`; else `Context.VIBRATOR_SERVICE` → `Vibrator`.
  - **Acceptance criteria:** Both branches compile; correct JNI method-ID caching (looked up once, not per-call).
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0040

- [ ] DEVICES-0042 — Implement `Start(TimeSpan)` → `VibrationEffect.createOneShot(long, DEFAULT_AMPLITUDE)`
  - **Area:** Android / JNI
  - **Files:** `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** Map the WP7 `Start(TimeSpan)` (no intensity concept) to `VibrationEffect.DEFAULT_AMPLITUDE`, matching the doc's "map default amplitude" requirement.
  - **Acceptance criteria:** Correct milliseconds conversion (`TimeSpan::TotalMilliseconds()` → `long`), no truncation-to-zero for sub-millisecond durations except at the documented zero-duration no-op boundary (DEVICES-0018).
  - **Tests:** compile-only + a host-side pure-function unit test for the ms-conversion helper if it's split out as testable
  - **Dependencies:** DEVICES-0041

- [ ] DEVICES-0043 — Map `NOXNA Start(TimeSpan, intensity)` to amplitude 1..255
  - **Area:** Android / JNI
  - **Files:** `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** `intensity ∈ [0,1]` → `amplitude ∈ [1,255]`; explicitly decide and document `intensity == 0.0f`'s mapping (per DEVICES-0030's decision — likely "treat as `Stop()`/no-op", not amplitude 0 which `VibrationEffect` rejects).
  - **Acceptance criteria:** Amplitude formula documented in a Doxygen comment; boundary (intensity exactly 0.0 and exactly 1.0) both handled without throwing a Java-side `IllegalArgumentException`.
  - **Tests:** host-side pure-function test for the amplitude-mapping formula (extract it as a testable free function)
  - **Dependencies:** DEVICES-0042

- [ ] DEVICES-0044 — Implement runtime amplitude-control-availability check
  - **Area:** Android / JNI
  - **Files:** `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** `Vibrator.hasAmplitudeControl()` gates whether the intensity extension actually varies strength; if `false`, fall back to `DEFAULT_AMPLITUDE` regardless of requested intensity (document this as an honest limitation, not a bug).
  - **Acceptance criteria:** Behavior documented; `getIsSupportedProperty()`-equivalent probe does not claim amplitude control it can't deliver.
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0043

- [ ] DEVICES-0045 — Implement `Stop()` → `Vibrator.cancel()`
  - **Area:** Android / JNI
  - **Files:** `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** `Stop()` calls `cancel()`; safe/no-op if nothing is currently vibrating.
  - **Acceptance criteria:** No JNI exception thrown/uncaught when called with nothing active.
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0041

- [ ] DEVICES-0046 — Implement runtime missing-vibrator-hardware check
  - **Area:** Android / JNI
  - **Files:** `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** `Vibrator.hasVibrator()` backs `IsSupported()`; devices with no vibrator motor (some tablets) correctly report unsupported.
  - **Acceptance criteria:** Documented; no crash if `hasVibrator()` is `false` and `Start()` is still called (silent no-op, matching the class-wide contract).
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0041

- [ ] DEVICES-0047 — Add `NOXNA Start(TimeSpan, intensity, waveform pattern)` — advanced haptics
  - **Area:** Android / JNI
  - **Files:** `include/Microsoft/Devices/VibrateController.hpp`, `src/Microsoft/Devices/Detail/AndroidVibrationBackend.cpp`
  - **Required behavior:** Only if a concrete need is identified (do not add speculatively per project convention) — `VibrationEffect.createWaveform(long[], int[], int repeat)` for pattern-based `NOXNA` haptics on Android; falls back to a single `Start()` pulse (or no-op) on backends without waveform support (SDL/desktop).
  - **Acceptance criteria:** Clearly `NOXNA`-tagged; falls back gracefully everywhere else; only implemented if a task requester provides a concrete use case — otherwise mark this task "skipped, no evidenced need" per this project's own anti-speculative-feature convention.
  - **Tests:** compile-only if implemented
  - **Dependencies:** DEVICES-0043

- [ ] DEVICES-0048 — Add `android.permission.VIBRATE` to any CNA-authored Android manifest fragment
  - **Area:** Android / packaging
  - **Files:** wherever Phase 9's Android manifest work lands (cross-reference DEVICES-0140+)
  - **Required behavior:** Ensure the permission is declared once Phase 9's demo APK exists; this task only adds the manifest line, not the whole APK pipeline.
  - **Acceptance criteria:** Manifest XML contains `<uses-permission android:name="android.permission.VIBRATE"/>`.
  - **Tests:** N/A (manifest content, not code)
  - **Dependencies:** Phase 9's manifest-creation task

- [ ] DEVICES-0049 — Add a fake/injectable `AndroidVibrationBackend` test double
  - **Area:** Android / Testing
  - **Files:** `tests/Microsoft/Devices/Detail/FakeAndroidVibrationBackendTests.cpp` (new)
  - **Required behavior:** A host-buildable fake implementing `IDeviceVibrationBackend` that records calls instead of doing real JNI, so the amplitude-mapping/duration-mapping logic (DEVICES-0042/0043) can be unit-tested on a desktop CI machine without Android.
  - **Acceptance criteria:** Runs in the normal `ctest`/desktop suite; asserts exact mapped amplitude/duration values for a table of `(intensity, duration)` inputs.
  - **Tests:** `FakeAndroidVibrationBackendTests.IntensityMapsToExpectedAmplitude`, `...ZeroIntensityStopsRatherThanZeroAmplitude`
  - **Dependencies:** DEVICES-0043

- [ ] DEVICES-0050 — Manual physical-device test task for `AndroidVibrationBackend`
  - **Area:** Android / Manual verification
  - **Files:** `docs/devices-hardware-checklist.md` (add a subsection)
  - **Required behavior:** Add a checklist item mirroring the existing "VibrateController::Start() actually vibrates" item, but specifically calling out testing the *native* backend path (once wired) vs. the SDL path, so a future session with real hardware can tell which one actually ran.
  - **Acceptance criteria:** Checklist item added, not executed here (no hardware in this container).
  - **Tests:** N/A — manual only
  - **Dependencies:** DEVICES-0049

### Phase 4: `SensorBase<T>` Lifecycle and Test Hardening

- [ ] DEVICES-0051 — Re-confirm `getCurrentValueProperty()` throw contract on unsupported sensors
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - **Required behavior:** Throws `InvalidOperationException` when `isSupported_ == false`, matching MSDN `hh239261` (Task P3-1's original fix) — re-confirm still true, not re-implement.
  - **Acceptance criteria:** Existing test(s) re-run and pass; code inspection confirms no regression.
  - **Tests:** re-run `SensorBaseTests.*`
  - **Dependencies:** none

- [ ] DEVICES-0052 — Re-confirm `getCurrentValueProperty()` behavior before any `Start()` call, on a supported device
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - **Required behavior:** On a hypothetically supported device that has never called `Start()`, confirm `getCurrentValueProperty()` returns a default-constructed reading without throwing (it only throws for *unsupported*, not *not-yet-started*) — document this precisely, since it's easy to conflate the two states.
  - **Acceptance criteria:** A test using the `SetSupportedForTesting(true)` hook (Accelerometer-specific, or an equivalent minimal test fixture for the base class) confirms this distinction explicitly.
  - **Tests:** `SensorBaseTests.CurrentValueDoesNotThrowBeforeStartWhenSupported` (add if missing)
  - **Dependencies:** none

- [ ] DEVICES-0053 — Re-confirm `IsDataValid` default is `false` before any reading arrives
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - **Required behavior:** `isDataValid_` starts `false`, matching the WP7 contract that `IsDataValid` reports whether `CurrentValue` reflects a real, current reading.
  - **Acceptance criteria:** Test confirms default state.
  - **Tests:** `SensorBaseTests.IsDataValidDefaultsFalse` (add if missing)
  - **Dependencies:** none

- [ ] DEVICES-0054 — Re-confirm `Dispose()` called twice throws `ObjectDisposedException` (single-threaded case)
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - **Required behavior:** Single-threaded double-`Dispose()` call throws, per the class's own doc comment.
  - **Acceptance criteria:** Existing test re-confirmed.
  - **Tests:** re-run relevant `AccelerometerTests`/`GyroscopeTests`/`CompassTests`/`MotionTests` double-dispose case
  - **Dependencies:** none

- [ ] DEVICES-0055 — Re-confirm concurrent `Dispose()` race resolves per documented contract (loser waits, no double-cleanup)
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - **Required behavior:** `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` behave exactly as documented (Phase 7's fix) — re-verify, don't re-derive.
  - **Acceptance criteria:** Existing concurrency tests pass 40/40 looped, clean under `devices-tsan`.
  - **Tests:** re-run + loop the relevant `Dispose`-race tests across `Accelerometer`/`Gyroscope`/`Compass`/`Motion`
  - **Dependencies:** none

- [ ] DEVICES-0056 — Re-confirm calling `Start()`/`Stop()`/getters after `Dispose()` throws `ObjectDisposedException` where documented
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/{Accelerometer,Gyroscope,Compass,Motion}.hpp`
  - **Required behavior:** Each concrete sensor's `Start()`/`Stop()` throw `ObjectDisposedException` after disposal (per their own doc comments); confirm getters (`CurrentValue`, `IsDataValid`, `TimeBetweenUpdates`) do or don't throw post-disposal and that this is intentional/documented either way.
  - **Acceptance criteria:** A per-class matrix of "post-dispose behavior per member" is written and each row has a passing test.
  - **Tests:** one test per class × per relevant member if missing
  - **Dependencies:** none

- [ ] DEVICES-0057 — Re-confirm event subscribe/unsubscribe safety during dispatch
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`, `include/CNA` `System::EventHandler<T>` (read-only cross-reference)
  - **Required behavior:** Subscribing/unsubscribing `CurrentValueChanged` from within its own handler (reentrant) must not crash or skip/double-invoke other subscribers unexpectedly — confirm `EventHandler<T>`'s iteration safety.
  - **Acceptance criteria:** Test covers add/remove-during-raise for at least one concrete sensor.
  - **Tests:** `AccelerometerTests.UnsubscribingDuringDispatchDoesNotCrash` (add if missing)
  - **Dependencies:** none

- [ ] DEVICES-0058 — Re-confirm event dispatch is prevented after `Stop()`
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`/`.cpp`, `Gyroscope.hpp`/`.cpp`
  - **Required behavior:** After `Stop()`, no further `CurrentValueChanged`/`ReadingChanged` dispatch occurs even if a stray synthetic/real event arrives (already implied by `started_` gating — confirm the gate is checked at the right point).
  - **Acceptance criteria:** Test using `InjectSyntheticSensorUpdate()` after `Stop()` confirms no event fires.
  - **Tests:** `AccelerometerTests.NoDispatchAfterStop` (add if missing), same for `GyroscopeTests`
  - **Dependencies:** none

- [ ] DEVICES-0059 — Re-confirm event dispatch is prevented after `Dispose()`
  - **Area:** SensorBase
  - **Files:** same as DEVICES-0058
  - **Required behavior:** Same guarantee as DEVICES-0058 but post-`Dispose()`.
  - **Acceptance criteria:** Test confirms no dispatch after disposal, and no crash/UAF (cross-reference Task P8-1's `dispatchToken_` fix).
  - **Tests:** `AccelerometerTests.NoDispatchAfterDispose` (add if missing), same for `Gyroscope`
  - **Dependencies:** DEVICES-0058

- [ ] DEVICES-0060 — Re-confirm a throwing `CurrentValueChanged` handler doesn't corrupt dispatch state
  - **Area:** SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - **Required behavior:** Per Task P8-5's documented exception-swallowing policy, a throwing handler must not prevent the next instance in a dispatch batch from receiving its own event, and must not leave this instance's own dispatch-tracking state (`dispatchToken_`) corrupted.
  - **Acceptance criteria:** Existing `ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent`/`ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose` tests re-run and pass.
  - **Tests:** re-run named tests above
  - **Dependencies:** none

- [ ] DEVICES-0061 — Re-confirm exact exception types match documented contracts across all 4 sensors
  - **Area:** SensorBase
  - **Files:** all four concrete sensor `.hpp`/`.cpp` files
  - **Required behavior:** `AccelerometerFailedException` (not plain `SensorFailedException`) for `Accelerometer`'s own start failures; `SensorFailedException` for `Gyroscope`/`Compass`/`Motion` (no dedicated subclass exists for these three — confirm this matches the real API, which per `AUDIT.md` only documents `AccelerometerFailedException` as a distinct type).
  - **Acceptance criteria:** Written confirmation per class; any mismatch becomes its own follow-up task, not silently fixed here.
  - **Tests:** re-run existing exception-type assertions
  - **Dependencies:** none

- [ ] DEVICES-0062 — Add a minimal `TestSensorBase` fixture reusable across Phase 4/6/7/8 tests
  - **Area:** Testing infrastructure
  - **Files:** `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` (extend the existing fixture, or extract to a shared test header if reused by Compass/Motion backend tests)
  - **Required behavior:** A concrete, minimal `SensorBase<T>` subclass usable by new Compass/Motion backend tests (Phase 7/8) without duplicating the fixture — extract to `tests/Microsoft/Devices/Sensors/TestSensorBaseFixture.hpp` if more than one test file needs it.
  - **Acceptance criteria:** Existing `SensorBaseTests.cpp` still passes after any extraction; new fixture header (if created) is test-only, never shipped in `include/`.
  - **Tests:** existing `SensorBaseTests.*`, unchanged pass count
  - **Dependencies:** none

### Phase 5: Accelerometer and Gyroscope Verification

- [ ] DEVICES-0063 — Confirm SDL-to-XNA unit conversion for `Accelerometer` (m/s² vs. g)
  - **Area:** Accelerometer
  - **Files:** `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - **Required behavior:** SDL3 `SDL_SENSOR_ACCEL` reports m/s²; WP7 `AccelerometerReading.Acceleration` is documented in g (1g ≈ 9.80665 m/s²). Confirm `DispatchSensorReading()` performs this conversion (divide by standard gravity) — this is a real, previously-unflagged gap risk worth explicitly re-checking, not assuming Phase 2's original port got it right.
  - **Acceptance criteria:** Either the conversion is confirmed present and correct (test asserts a known SDL input converts to the expected g value within tolerance), or a bug is found and fixed with a dedicated task/commit, documented as an intentional deviation fix, not silently folded into this task.
  - **Tests:** `AccelerometerTests.SdlMetersPerSecondSquaredConvertsToGForce` (add if missing)
  - **Dependencies:** none

- [ ] DEVICES-0064 — Confirm SDL-to-XNA unit conversion for `Gyroscope` (rad/s)
  - **Area:** Gyroscope
  - **Files:** `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - **Required behavior:** SDL3 `SDL_SENSOR_GYRO` reports rad/s; confirm WP7 `GyroscopeReading.RotationRate` is documented in the same unit (rad/s) — if so, confirm no unwanted conversion is applied; if WP7 actually expects degrees/s, this is a real bug.
  - **Acceptance criteria:** Matrix entry states which unit WP7 documents (re-verify against MSDN, don't assume) and whether current code matches.
  - **Tests:** `GyroscopeTests.UnitsMatchDocumentedConvention` (add if missing)
  - **Dependencies:** none

- [ ] DEVICES-0065 — Re-confirm Android landscape axis-remap sign convention against its own doc comment
  - **Area:** Accelerometer / Gyroscope / Android
  - **Files:** `include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`
  - **Required behavior:** `ConvertAndroidPortraitToXnaLandscape()`'s `Rotation90`/`Rotation270` sign math matches its own doc comment exactly (already unit-tested; this task is a fresh read-through, not a rewrite).
  - **Acceptance criteria:** Existing `AndroidSensorOrientationTests.cpp` (9 tests) re-run and pass; no code change unless a discrepancy is found.
  - **Tests:** re-run `AndroidSensorOrientationTests.*`
  - **Dependencies:** none

- [ ] DEVICES-0066 — Re-confirm `TimeBetweenUpdates` throttling behavior (or documented absence thereof)
  - **Area:** Accelerometer / Gyroscope / SensorBase
  - **Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`, `src/Microsoft/Devices/Sensors/{Accelerometer,Gyroscope}.cpp`
  - **Required behavior:** Determine and document whether `TimeBetweenUpdates` actually throttles dispatch frequency today, or is purely a stored/observable value with no enforcement (SDL's own event rate may not respect it) — this is a real compatibility question worth resolving explicitly rather than leaving implicit.
  - **Acceptance criteria:** Written, explicit answer in the class's Doxygen comment; if no throttling exists and WP7 documents it as authoritative, file this as a known, accepted deviation (do not silently implement throttling as a side effect of this audit task — that's a separate, larger task if pursued).
  - **Tests:** N/A for the audit; a follow-up task if throttling is added
  - **Dependencies:** none

- [ ] DEVICES-0067 — Re-confirm timestamp is always real wall-clock time, never SDL monotonic ticks
  - **Area:** Accelerometer / Gyroscope
  - **Files:** `src/Microsoft/Devices/Sensors/{Accelerometer,Gyroscope}.cpp`
  - **Required behavior:** Per Task P4-7's fix, `Timestamp` uses `System::DateTimeOffset::getUtcNowProperty()`, not `SDL_GetTicksNS()`.
  - **Acceptance criteria:** Existing tests re-confirmed; no regression to the pre-fix bug.
  - **Tests:** re-run relevant `AccelerometerReadingTests`/`GyroscopeReadingTests` timestamp assertions
  - **Dependencies:** none

- [ ] DEVICES-0068 — Re-confirm legacy `ReadingChanged` compatibility on `Accelerometer` only
  - **Area:** Accelerometer
  - **Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - **Required behavior:** `ReadingChanged` (WP7 7.0 legacy) is raised alongside `CurrentValueChanged` (WP7 7.1) from `ProcessSensorUpdateEvent()`/`DispatchSensorReading()`; confirm `Gyroscope` correctly has no equivalent (matches real API).
  - **Acceptance criteria:** Existing coverage re-confirmed.
  - **Tests:** re-run `AccelerometerTests.*ReadingChanged*`
  - **Dependencies:** none

- [ ] DEVICES-0069 — Re-confirm unsupported-backend behavior on desktop for both classes
  - **Area:** Accelerometer / Gyroscope
  - **Files:** `src/Microsoft/Devices/Sensors/{Accelerometer,Gyroscope}.cpp`
  - **Required behavior:** On this hardware-less container, `getIsSupportedProperty()` is `false`, `Start()` throws the appropriate `*FailedException`, `getCurrentValueProperty()` throws `InvalidOperationException`.
  - **Acceptance criteria:** Confirmed by re-running (not just reading) `GetIsSupportedPropertyDoesNotCrash`/`StartOnUnsupportedPlatformThrows`/`GetCurrentValuePropertyThrowsWhenUnsupported` for both classes.
  - **Tests:** re-run named tests
  - **Dependencies:** none

- [ ] DEVICES-0070 — Loop the full `Accelerometer`/`Gyroscope` concurrency suite (40+ iterations) as a Phase 5 gate
  - **Area:** Testing
  - **Files:** none
  - **Required behavior:** Since Phase 5 tasks read (and, for DEVICES-0063, may modify) `Accelerometer.cpp`/`Gyroscope.cpp`, re-run the full concurrency stress loop per `docs/devices-build.md` Section 2 before declaring Phase 5 done.
  - **Acceptance criteria:** 40/40 clean.
  - **Tests:** `AccelerometerTests.*:GyroscopeTests.*` looped 40×
  - **Dependencies:** DEVICES-0063, DEVICES-0064 (only if either produced a code change)

- [ ] DEVICES-0071 — Update `docs/devices-hardware-checklist.md` with any new unit-conversion verification steps
  - **Area:** Docs
  - **Files:** `docs/devices-hardware-checklist.md`
  - **Required behavior:** If DEVICES-0063/0064 found a real conversion gap and fixed it, add a manual-verification step confirming the g-force/rad-per-second values "feel right" on real hardware (magnitude sanity, not just sign).
  - **Acceptance criteria:** Checklist updated only if a code change occurred; otherwise no-op.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0063, DEVICES-0064

- [ ] DEVICES-0072 — Desktop SDL accelerometer/gyroscope behavior when real hardware IS present
  - **Area:** Accelerometer / Gyroscope / Desktop
  - **Files:** none changed; investigation only
  - **Required behavior:** Document what happens if this code runs on a desktop Linux/Windows/macOS machine that does have a physical accelerometer (e.g. a laptop with one) — confirm the non-Android code path (no landscape remap) reports raw SDL axes directly, and that this is the intended, documented behavior (not a gap).
  - **Acceptance criteria:** Written confirmation in the class's Doxygen comment if not already present.
  - **Tests:** N/A (no such hardware in this container)
  - **Dependencies:** none

### Phase 6: Android Native Sensor Bridge Foundation

- [ ] DEVICES-0073 — Decide bridge implementation language (Java/Kotlin + JNI vs. pure C++ via NDK sensor APIs)
  - **Area:** Android / Design
  - **Files:** design note appended to `docs/devices-native-backend-design.md`
  - **Required behavior:** Evaluate the NDK's native `ASensorManager`/`ASensorEventQueue` API (available without any JNI/Java bridge at all, unlike the vibration case) as the primary option before defaulting to a Java/Kotlin bridge — this may make Compass/Motion's Android backend significantly simpler than the originating brief assumed. Decide and document which approach this plan uses.
  - **Acceptance criteria:** Written decision with rationale; if `ASensorManager` is chosen, Phase 6/7/8's "JNI bridge" tasks are re-scoped to "NDK native sensor API" tasks — update task titles accordingly before implementation starts.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0013 (confirms SDL has no equivalent, so this bridge is genuinely new work)

- [ ] DEVICES-0074 — Design the shared Android sensor bridge interface
  - **Area:** Android / Design
  - **Files:** `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp` (new, design/skeleton only)
  - **Required behavior:** One shared class registering/unregistering `ASensorEventQueue` listeners for arbitrary sensor types, delivering events into a caller-supplied callback; reusable by both the Compass and Motion backends (Phase 7/8) rather than duplicated per-sensor bridges.
  - **Acceptance criteria:** Header compiles (Android cross-compile) with no behavior yet; doc comments explain the pull vs. push model decision from `docs/devices-native-backend-design.md`.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0073

- [ ] DEVICES-0075 — Implement sensor listener registration
  - **Area:** Android / Sensor bridge
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp` (new)
  - **Required behavior:** `ASensorManager_getInstanceForPackage()`/`ASensorManager_getDefaultSensor(type)`/`ASensorManager_createEventQueue()` wired correctly, matching Android NDK sample patterns.
  - **Acceptance criteria:** Compiles under the Android NDK cross-compile target; correct looper/event-queue association (`ALooper_prepare()`/`ASensorManager_createEventQueue()` with a valid looper).
  - **Tests:** compile-only (no Android device in this container)
  - **Dependencies:** DEVICES-0074

- [ ] DEVICES-0076 — Implement sensor listener unregistration
  - **Area:** Android / Sensor bridge
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** `ASensorEventQueue_disableSensor()`/`ASensorManager_destroyEventQueue()` called exactly once per registration, symmetric with DEVICES-0075.
  - **Acceptance criteria:** No double-destroy/leak across repeated Start/Stop cycles (reasoned, since no device to run it on).
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0075

- [ ] DEVICES-0077 — Implement timestamp conversion (Android sensor `int64_t` nanoseconds → `System::DateTimeOffset`)
  - **Area:** Android / Sensor bridge
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** Android's `ASensorEvent.timestamp` is a monotonic `int64_t` in nanoseconds (boot time), **not** wall-clock — per this codebase's own established precedent (Task P4-7's fix for `Accelerometer`/`Gyroscope`), the bridge must use real wall-clock time for `Timestamp`, not the raw sensor timestamp, for consistency with every other reading class in this namespace.
  - **Acceptance criteria:** Doc comment explicitly cross-references Task P4-7's precedent; a host-side unit test (via the fake backend, DEVICES-0083) confirms wall-clock is used.
  - **Tests:** `AndroidSensorBridgeTests` (fake-backend-based, see DEVICES-0083)
  - **Dependencies:** DEVICES-0075

- [ ] DEVICES-0078 — Implement event queueing from the Android sensor thread to the calling thread
  - **Area:** Android / Sensor bridge / Concurrency
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** Decide (and document) which thread delivers events — the NDK sensor API delivers via `ALooper_pollAll()`, meaning **the caller's thread that pumps the looper receives events**, not an arbitrary background thread the way SDL's `SDL_AddEventWatch()` works. This is an important, different threading model from `Accelerometer`/`Gyroscope` and must be documented clearly, not assumed identical.
  - **Acceptance criteria:** Doc comment explicitly contrasts this threading model with `Detail::SdlSensorSubsystem<TSensor>`'s.
  - **Tests:** N/A (design/documentation-level task, verified by review)
  - **Dependencies:** DEVICES-0075

- [ ] DEVICES-0079 — Add thread-safety rules for the bridge reusing `SensorBase<T>`'s existing locking
  - **Area:** Android / Sensor bridge / Concurrency
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** Route delivered readings through `setCurrentValueProperty()` exactly like `Accelerometer::DispatchSensorReading()` does — reuse the existing mutex-guarded setter, never invent a second locking scheme (mandatory per `docs/devices-native-backend-design.md`'s "Lifecycle" note).
  - **Acceptance criteria:** Code review confirms no new mutex/lock is introduced in the bridge itself for `currentValue_`/`isDataValid_` — only `SensorBase<T>`'s existing ones are used.
  - **Tests:** covered by DEVICES-0083's fake-backend tests
  - **Dependencies:** DEVICES-0078

- [ ] DEVICES-0080 — Implement lifecycle hooks tied to `Dispose()`/destructor
  - **Area:** Android / Sensor bridge
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** Bridge unregisters listeners inside the owning `Compass`/`Motion`'s `Dispose(bool)` override, using `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` exactly as `Accelerometer`/`Gyroscope` do — reuse, don't reinvent.
  - **Acceptance criteria:** Code review confirms the exact same disposal-claiming pattern is followed.
  - **Tests:** covered once wired into Compass/Motion (Phase 7/8)
  - **Dependencies:** DEVICES-0076

- [ ] DEVICES-0081 — Implement sensor-rate selection from `TimeBetweenUpdates`
  - **Area:** Android / Sensor bridge
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** Map `TimeBetweenUpdates` (a `System::TimeSpan`) to `ASensorEventQueue_setEventRate()`'s microsecond parameter.
  - **Acceptance criteria:** Correct unit conversion (`TimeSpan` → microseconds); a host-side pure-function test for the conversion formula.
  - **Tests:** `AndroidSensorBridgeTests.TimeBetweenUpdatesConvertsToMicroseconds`
  - **Dependencies:** DEVICES-0075

- [ ] DEVICES-0082 — Document Android 12+ sensor-rate-limiting behavior
  - **Area:** Android / Sensor bridge
  - **Files:** `docs/devices-native-backend-design.md`
  - **Required behavior:** Android 12+ restricts high-frequency sensor sampling without the `HIGH_SAMPLING_RATE_SENSORS` permission for rates above 200Hz — document this constraint so a future implementer doesn't get silently throttled and blame the bridge.
  - **Acceptance criteria:** Doc note added.
  - **Tests:** N/A
  - **Dependencies:** none

- [ ] DEVICES-0083 — Add a fake/injectable Android sensor bridge test double
  - **Area:** Android / Testing
  - **Files:** `tests/Microsoft/Devices/Sensors/Detail/FakeAndroidSensorBridgeTests.cpp` (new)
  - **Required behavior:** A host-buildable fake that lets tests inject synthetic `ASensorEvent`-equivalent structs (magnetic field / rotation vector / gravity / linear acceleration / gyroscope values with timestamps) and assert the timestamp-conversion (DEVICES-0077) and rate-selection (DEVICES-0081) math without needing a real Android device.
  - **Acceptance criteria:** Runs in the normal desktop `ctest` suite.
  - **Tests:** `FakeAndroidSensorBridgeTests.*` (multiple, covering DEVICES-0077/0081's formulas)
  - **Dependencies:** DEVICES-0077, DEVICES-0081

- [ ] DEVICES-0084 — Avoid global-reference leaks in the bridge (Android NDK JNI/ASensorManager hygiene)
  - **Area:** Android / Sensor bridge
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** If DEVICES-0073 chose a pure-NDK `ASensorManager` approach, this task instead confirms there is no JNI at all in this bridge (no leak risk); if a JNI bridge was chosen, apply the same global-reference discipline as Phase 3's vibration JNI code (DEVICES-0040).
  - **Acceptance criteria:** Code review; N/A if no JNI is used.
  - **Tests:** N/A or covered by DEVICES-0083
  - **Dependencies:** DEVICES-0073

- [ ] DEVICES-0085 — Avoid use-after-free when the owning `Compass`/`Motion` is disposed mid-callback
  - **Area:** Android / Sensor bridge / Concurrency
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - **Required behavior:** Apply the same `dispatchToken_`-style protection Task P8-1 added to `Accelerometer`/`Gyroscope` — a callback destroying its own owning sensor object must not leave the bridge's cleanup code touching freed memory.
  - **Acceptance criteria:** Code review confirms the same shared-token pattern is reused (not reinvented); a fake-backend test exercises the destroy-during-dispatch scenario the way `AccelerometerTests`'s equivalent does.
  - **Tests:** `FakeAndroidSensorBridgeTests.SelfDestroyingDuringDispatchDoesNotUseAfterFree`
  - **Dependencies:** DEVICES-0083, DEVICES-0080

### Phase 7: Compass Native Backend

- [ ] DEVICES-0086 — Design `ICompassBackend` concretely (from the existing sketch)
  - **Area:** Compass / Design
  - **Files:** `include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp` (new, promotes the sketch in `docs/devices-native-backend-design.md` to real, compiling code)
  - **Required behavior:** `IsSupported()`, `Start()`, `Stop()`, `GetLatestReading()` returning `CompassReading`, per the design doc.
  - **Acceptance criteria:** Compiles; no behavior yet; `Compass.hpp`/`.cpp` unchanged so far (per the design doc's migration-plan step 1: no public API change).
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0074

- [ ] DEVICES-0087 — Implement `AndroidCompassBackend` skeleton
  - **Area:** Compass / Android
  - **Files:** `include/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp` (new), `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp` (new, `#ifdef __ANDROID__`)
  - **Required behavior:** Class shape only, backed by `AndroidSensorBridge` (Phase 6).
  - **Acceptance criteria:** Compiles under Android NDK cross-compile.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0086, DEVICES-0080

- [ ] DEVICES-0088 — Register `TYPE_MAGNETIC_FIELD` via the bridge
  - **Area:** Compass / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - **Required behavior:** Registers a listener for `ASENSOR_TYPE_MAGNETIC_FIELD`.
  - **Acceptance criteria:** Compiles; correct sensor-type constant used (confirm NDK header name, e.g. `ASENSOR_TYPE_MAGNETIC_FIELD`).
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0087

- [ ] DEVICES-0089 — Register `TYPE_ACCELEROMETER` or `TYPE_ROTATION_VECTOR` as the orientation basis
  - **Area:** Compass / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - **Required behavior:** Per the design doc, pick `TYPE_ROTATION_VECTOR` if it avoids doing fusion math in the bridge itself; document the chosen trade-off explicitly (accuracy vs. bridge complexity) rather than picking silently.
  - **Acceptance criteria:** Doxygen comment states which sensor was chosen and why.
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0088

- [ ] DEVICES-0090 — Compute `MagneticHeading` from the chosen sensor data
  - **Area:** Compass / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - **Required behavior:** `SensorManager.getOrientation()`-equivalent math (or direct rotation-vector-to-azimuth conversion if using `TYPE_ROTATION_VECTOR`) producing a degrees-from-magnetic-north value, mapped to `CompassReading.MagneticHeading`.
  - **Acceptance criteria:** A pure-function version of the azimuth math is extracted and host-testable (feed known magnetic-field/gravity vectors, assert expected heading in degrees).
  - **Tests:** `AndroidCompassMathTests.KnownMagneticFieldVectorProducesExpectedHeading`
  - **Dependencies:** DEVICES-0089

- [ ] DEVICES-0091 — Handle `TrueHeading` honestly (no location source available)
  - **Area:** Compass / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - **Required behavior:** Per Non-Goals and the design doc, `TrueHeading` cannot be computed without geomagnetic declination, which requires `System.Device.Location` (not implemented). Set `TrueHeading` equal to `MagneticHeading` **only if** that is the honest, documented fallback the design doc specifies, or leave it at a sentinel/default and document clearly why — do **not** invent a declination value or silently assume 0.
  - **Acceptance criteria:** Doxygen comment explicitly states the limitation; a test asserts the chosen fallback behavior exactly.
  - **Tests:** `AndroidCompassMathTests.TrueHeadingLimitationIsDocumentedAndTested`
  - **Dependencies:** DEVICES-0090

- [ ] DEVICES-0092 — Map `HeadingAccuracy` from `SensorManager`'s `SENSOR_STATUS_*` accuracy constants
  - **Area:** Compass / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - **Required behavior:** `SENSOR_STATUS_UNRELIABLE`/`_LOW`/`_MEDIUM`/`_HIGH` → a `HeadingAccuracy` degrees value (design doc leaves the exact mapping to implementation time — pick a reasonable, documented mapping, e.g. `UNRELIABLE→180, LOW→45, MEDIUM→15, HIGH→5`, clearly marked as a CNA choice, not an XNA-documented value).
  - **Acceptance criteria:** Mapping documented and unit-tested for all 4 input constants.
  - **Tests:** `AndroidCompassMathTests.AccuracyConstantMapsToExpectedDegrees` (parameterized, 4 cases)
  - **Dependencies:** DEVICES-0090

- [ ] DEVICES-0093 — Raise `Compass::Calibrate` on low-accuracy transitions
  - **Area:** Compass / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Compass.cpp`, `Detail/AndroidCompassBackend.cpp`
  - **Required behavior:** `SENSOR_STATUS_UNRELIABLE` (and optionally `_LOW`) triggers `Calibrate.Raise()`, mirroring `CLLocationManagerDelegate.shouldDisplayHeadingCalibration()`'s intent on iOS per the design doc.
  - **Acceptance criteria:** Test using the fake bridge (DEVICES-0083) confirms `Calibrate` fires exactly once per transition into unreliable state, not once per sample.
  - **Tests:** `CompassTests.CalibrateRaisedOnAccuracyDrop` (fake-backend-based)
  - **Dependencies:** DEVICES-0092, DEVICES-0080

- [ ] DEVICES-0094 — Populate `MagnetometerReading` (raw µT vector)
  - **Area:** Compass / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - **Required behavior:** Raw `TYPE_MAGNETIC_FIELD` x/y/z (µT) mapped directly into `CompassReading.MagnetometerReading`, applying any needed Android-to-XNA axis remap (reuse `Detail::AndroidSensorOrientation.hpp`'s existing pure function if the same landscape convention applies here — confirm this explicitly, don't assume).
  - **Acceptance criteria:** Test confirms axis convention consistency with `Accelerometer`'s remap for the same rotation states, or documents why compass axes differ.
  - **Tests:** `AndroidCompassMathTests.MagnetometerAxisConventionMatchesAccelerometer` (or documents the deviation)
  - **Dependencies:** DEVICES-0088, DEVICES-0065

- [ ] DEVICES-0095 — Wire `AndroidCompassBackend` into `Compass` via the backend-selection seam
  - **Area:** Compass
  - **Files:** `src/Microsoft/Devices/Sensors/Compass.cpp`
  - **Required behavior:** Per the design doc's migration plan: add a private `std::unique_ptr<Detail::ICompassBackend>` member, `#if defined(__ANDROID__)` selects `AndroidCompassBackend`, else no backend (today's exact stub behavior, unchanged). `getIsSupportedProperty()`/`Start()` only change behavior on Android once this lands.
  - **Acceptance criteria:** Every existing `CompassTests` case (`GetIsSupportedPropertyDoesNotCrash`/`IsFalse`, `StartThrowsSensorFailedException`) still passes unmodified on this (non-Android) build, per the design doc's own requirement.
  - **Tests:** full existing `CompassTests.*` re-run green; new Android-gated tests added (compile-only on this host)
  - **Dependencies:** DEVICES-0087 through DEVICES-0094

- [ ] DEVICES-0096 — Add fake-backend `CompassTests` for the new Android path
  - **Area:** Compass / Testing
  - **Files:** `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - **Required behavior:** Using a fake `ICompassBackend` (not `AndroidCompassBackend` directly, so it's host-testable), confirm `Compass::Start()`/`CurrentValueChanged`/`Calibrate` correctly delegate to a "supported" backend when one is injected — this proves the C++ delegation plumbing without needing Android.
  - **Acceptance criteria:** New tests pass on this host; existing NotSupported-stub tests remain the default (no backend injected) path.
  - **Tests:** `CompassTests.WithInjectedSupportedBackendStartSucceeds`, `...CurrentValueChangedFiresFromBackendReading`
  - **Dependencies:** DEVICES-0095

- [ ] DEVICES-0097 — Cross-compile `Compass`/`AndroidCompassBackend` for Android and verify via `llvm-nm`
  - **Area:** Compass / Android / Build
  - **Files:** none changed; build verification only
  - **Required behavior:** Same technique as `docs/devices-build.md` Section 4 (`llvm-nm -C ... | grep`) confirming the new Android-only symbols actually compile into the object file, not just that the library target succeeds.
  - **Acceptance criteria:** `llvm-nm` output shows the expected new symbol names.
  - **Tests:** N/A (build verification)
  - **Dependencies:** DEVICES-0095

- [ ] DEVICES-0098 — Document remaining Compass limitations honestly in `AUDIT.md`/`NEXT.md`
  - **Area:** Docs
  - **Files:** `AUDIT.md`, `NEXT.md`
  - **Required behavior:** Update `Compass`'s row: still `NotSupported` on desktop/iOS (by design), real on Android (once this phase lands), `TrueHeading` still limited (DEVICES-0091), never physically verified (no Android hardware in this container).
  - **Acceptance criteria:** No flat "Compass is complete" claim — layered status per `NEXT.md`'s own convention.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0095, DEVICES-0097

- [ ] DEVICES-0099 — Add Compass manual hardware-verification checklist items
  - **Area:** Docs / Manual verification
  - **Files:** `docs/devices-hardware-checklist.md`
  - **Required behavior:** New section: confirm `MagneticHeading` roughly matches a known reference (phone's own compass app) at a fixed physical orientation; confirm `Calibrate` fires when performing the classic figure-8 calibration gesture.
  - **Acceptance criteria:** Checklist items added, not executed here.
  - **Tests:** N/A — manual only
  - **Dependencies:** DEVICES-0095

- [ ] DEVICES-0100 — Do NOT fake Compass from Accelerometer-only data — explicit negative-verification task
  - **Area:** Compass / Correctness gate
  - **Files:** none changed; verification only
  - **Required behavior:** Re-read the final `Compass.cpp` after Phase 7 lands and confirm no code path synthesizes a heading from `Accelerometer` data alone without a real magnetometer reading — this is the single most important rule in this phase per the Safety and Correctness Rules section.
  - **Acceptance criteria:** Explicit sign-off note in this plan file or `AUDIT.md` confirming the check was done, referencing the exact functions read.
  - **Tests:** N/A (review-only gate)
  - **Dependencies:** DEVICES-0095

### Phase 8: Motion Native Backend

- [ ] DEVICES-0101 — Design `IMotionBackend` concretely (from the existing sketch)
  - **Area:** Motion / Design
  - **Files:** `include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp` (new)
  - **Required behavior:** `IsSupported()`, `Start()`, `Stop()`, `GetLatestReading()` returning `MotionReading`, per the design doc.
  - **Acceptance criteria:** Compiles; no behavior yet.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0074

- [ ] DEVICES-0102 — Implement `AndroidMotionBackend` skeleton
  - **Area:** Motion / Android
  - **Files:** `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp` (new), `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp` (new, `#ifdef __ANDROID__`)
  - **Required behavior:** Class shape only, backed by `AndroidSensorBridge`.
  - **Acceptance criteria:** Compiles under Android NDK cross-compile.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0101, DEVICES-0080

- [ ] DEVICES-0103 — Register `TYPE_ROTATION_VECTOR` as the primary `Attitude` source
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** Primary path per the design doc (has true-north reference, at the cost of magnetometer coupling).
  - **Acceptance criteria:** Compiles; correct sensor-type constant.
  - **Tests:** compile-only
  - **Dependencies:** DEVICES-0102

- [ ] DEVICES-0104 — Register `TYPE_GAME_ROTATION_VECTOR` as a magnetometer-free fallback
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** Used when `TYPE_ROTATION_VECTOR` is unavailable (device has no magnetometer) — no true-north reference, but still usable for relative attitude.
  - **Acceptance criteria:** Fallback-selection logic documented; a fake-backend test confirms the selection order.
  - **Tests:** `AndroidMotionBackendTests.FallsBackToGameRotationVectorWhenRotationVectorUnavailable`
  - **Dependencies:** DEVICES-0103

- [ ] DEVICES-0105 — Convert rotation-vector output to `Quaternion`
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** Android's rotation-vector sensor reports `(x, y, z, [w])` quaternion components directly (`w` may need to be derived: `w = sqrt(1 - x²-y²-z²)` if the vector array is length 3) — map to `Microsoft::Xna::Framework::Quaternion` with correct axis-convention handling (Android's coordinate system vs. XNA's).
  - **Acceptance criteria:** Pure-function conversion extracted and host-tested against known input vectors.
  - **Tests:** `AndroidMotionMathTests.RotationVectorConvertsToExpectedQuaternion`
  - **Dependencies:** DEVICES-0103

- [ ] DEVICES-0106 — Derive `RotationMatrix` from the same quaternion, consistently
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** `RotationMatrix` must represent the exact same orientation as `Quaternion` (not independently derived from a second, possibly-inconsistent source like `SensorManager.getRotationMatrix()`).
  - **Acceptance criteria:** Test confirms `Matrix.CreateFromQuaternion(quaternion) == rotationMatrix` (within float tolerance) for several sample orientations.
  - **Tests:** `AndroidMotionMathTests.RotationMatrixConsistentWithQuaternion`
  - **Dependencies:** DEVICES-0105

- [ ] DEVICES-0107 — Derive `Yaw`/`Pitch`/`Roll` from the same quaternion/matrix, consistently
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** `AttitudeReading.Pitch`/`Roll`/`Yaw` must decompose from the same orientation as `Quaternion`/`RotationMatrix`, not computed independently from raw sensor angles (avoids the classic "3 sources of truth disagree" bug class).
  - **Acceptance criteria:** Test confirms round-trip consistency: constructing a quaternion from known yaw/pitch/roll and decomposing it back matches within tolerance.
  - **Tests:** `AndroidMotionMathTests.YawPitchRollConsistentWithQuaternion`
  - **Dependencies:** DEVICES-0105

- [ ] DEVICES-0108 — Register `TYPE_GRAVITY` for `MotionReading.Gravity`
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** Android's virtual `TYPE_GRAVITY` sensor already isolates the gravity component (no manual low-pass filtering needed, per the design doc).
  - **Acceptance criteria:** Compiles; unit consistency confirmed (Android reports m/s², `MotionReading.Gravity` doc says "in g" per `MotionReading.hpp`'s own comment — confirm and apply the same g-conversion as DEVICES-0063 if needed).
  - **Tests:** `AndroidMotionMathTests.GravityConvertsToExpectedGForce`
  - **Dependencies:** DEVICES-0102, DEVICES-0063

- [ ] DEVICES-0109 — Register `TYPE_LINEAR_ACCELERATION` for `MotionReading.DeviceAcceleration`
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** Android's virtual `TYPE_LINEAR_ACCELERATION` sensor already excludes gravity (matching XNA's own gravity/acceleration split, per the design doc — no manual high-pass filtering needed).
  - **Acceptance criteria:** Same unit-conversion diligence as DEVICES-0108.
  - **Tests:** `AndroidMotionMathTests.DeviceAccelerationConvertsToExpectedGForce`
  - **Dependencies:** DEVICES-0108

- [ ] DEVICES-0110 — Register `TYPE_GYROSCOPE` for `MotionReading.DeviceRotationRate`
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** Same rad/s unit question as DEVICES-0064, applied to this fourth, independent registration of the gyroscope sensor (Motion registers its own, separate from any live `Gyroscope` instance — confirm this doesn't double-register or conflict with a real `Gyroscope` object existing simultaneously).
  - **Acceptance criteria:** Test confirms a live `Gyroscope` instance and a live `Motion` instance can coexist without interfering with each other's sensor registration (Android allows multiple listeners on the same sensor, but confirm this codebase's own instance-counting (`MaxSensorCount`) doesn't wrongly conflate the two).
  - **Tests:** `AndroidMotionBackendTests.CoexistsWithIndependentGyroscopeInstance`
  - **Dependencies:** DEVICES-0064, DEVICES-0102

- [ ] DEVICES-0111 — Map Android's coordinate system to XNA/WP7 expectations for all four Motion vectors
  - **Area:** Motion / Android
  - **Files:** `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - **Required behavior:** Apply `Detail::ConvertAndroidPortraitToXnaLandscape()` (or an equivalent, explicitly justified different remap if Motion's coordinate needs differ from Accelerometer's) consistently to `Gravity`/`DeviceAcceleration`/`DeviceRotationRate`, and an equivalent orientation remap to `Attitude`'s quaternion/matrix/yaw-pitch-roll.
  - **Acceptance criteria:** Explicit doc comment justifying reuse or divergence from the existing Accelerometer/Gyroscope remap function.
  - **Tests:** `AndroidMotionMathTests.CoordinateConversionMatchesAccelerometerConventionOrDocumentsDeviation`
  - **Dependencies:** DEVICES-0065, DEVICES-0107, DEVICES-0108, DEVICES-0109, DEVICES-0110

- [ ] DEVICES-0112 — Wire `AndroidMotionBackend` into `Motion` via the backend-selection seam
  - **Area:** Motion
  - **Files:** `src/Microsoft/Devices/Sensors/Motion.cpp`
  - **Required behavior:** Same migration-plan discipline as DEVICES-0095: private `std::unique_ptr<Detail::IMotionBackend>` member, Android-only selection, no behavior change on other platforms; existing `MotionTests` (`GetIsSupportedPropertyDoesNotCrash`/`IsFalse`, `StartThrowsSensorFailedException`) still pass unmodified on this host.
  - **Acceptance criteria:** Full existing `MotionTests.*` green.
  - **Tests:** full existing suite + new tests
  - **Dependencies:** DEVICES-0102 through DEVICES-0111

- [ ] DEVICES-0113 — Add fake-backend `MotionTests` for the new Android path
  - **Area:** Motion / Testing
  - **Files:** `tests/Microsoft/Devices/Sensors/MotionTests.cpp`
  - **Required behavior:** Same pattern as DEVICES-0096, but for `Motion`/`IMotionBackend`.
  - **Acceptance criteria:** New tests pass; existing stub-path tests remain default.
  - **Tests:** `MotionTests.WithInjectedSupportedBackendStartSucceeds`, `...CurrentValueChangedFiresFromBackendReading`
  - **Dependencies:** DEVICES-0112

- [ ] DEVICES-0114 — Motion's dependency on Compass — decide and document the actual coupling
  - **Area:** Motion / Design
  - **Files:** `src/Microsoft/Devices/Sensors/Motion.cpp`, `include/Microsoft/Devices/Sensors/Motion.hpp`
  - **Required behavior:** The class comment says "Motion requires an Accelerometer, Compass, and Gyroscope" — but Android's `TYPE_ROTATION_VECTOR`/`TYPE_GAME_ROTATION_VECTOR` fuse this internally in the OS, meaning `AndroidMotionBackend` does **not** need a live `Compass`/`Accelerometer`/`Gyroscope` C++ instance at all. Update the doc comment to reflect the real dependency (Android sensor availability, not this codebase's own sensor object graph) once this phase lands, so it doesn't mislead a future reader into thinking `Motion` requires constructing the other three classes first.
  - **Acceptance criteria:** Doc comment corrected; confirm no code actually requires constructing `Compass`/`Accelerometer`/`Gyroscope` instances for `Motion` to work.
  - **Tests:** `MotionTests.DoesNotRequireOtherSensorInstancesToBeConstructed`
  - **Dependencies:** DEVICES-0112

- [ ] DEVICES-0115 — Cross-compile `Motion`/`AndroidMotionBackend` for Android and verify via `llvm-nm`
  - **Area:** Motion / Android / Build
  - **Files:** none changed; build verification only
  - **Required behavior:** Same technique as DEVICES-0097.
  - **Acceptance criteria:** `llvm-nm` output shows expected new symbols.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0112

- [ ] DEVICES-0116 — Document drift differences between rotation-vector and game-rotation-vector modes
  - **Area:** Docs
  - **Files:** `docs/devices-native-backend-design.md`
  - **Required behavior:** Explain that `TYPE_GAME_ROTATION_VECTOR` (gyroscope+accelerometer only) drifts in yaw over time with no magnetometer correction, while `TYPE_ROTATION_VECTOR` stays yaw-stable but couples to magnetic interference — a game switching between the two fallback modes should expect this trade-off, not treat it as a bug.
  - **Acceptance criteria:** Doc note added.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0104

- [ ] DEVICES-0117 — Document remaining Motion limitations honestly in `AUDIT.md`/`NEXT.md`
  - **Area:** Docs
  - **Files:** `AUDIT.md`, `NEXT.md`
  - **Required behavior:** Same layered-status discipline as DEVICES-0098, applied to `Motion`.
  - **Acceptance criteria:** No flat "Motion is complete" claim.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0112, DEVICES-0115

- [ ] DEVICES-0118 — Add Motion manual hardware-verification checklist items
  - **Area:** Docs / Manual verification
  - **Files:** `docs/devices-hardware-checklist.md`
  - **Required behavior:** New section: confirm `Attitude`'s yaw tracks a known 90°/180° physical rotation; confirm `Gravity`/`DeviceAcceleration` split behaves as expected (device at rest: `Gravity ≈ (0,±1,0)`-ish depending on orientation, `DeviceAcceleration ≈ 0`; device thrown/shaken: `DeviceAcceleration` spikes, `Gravity` stays roughly constant).
  - **Acceptance criteria:** Checklist items added, not executed here.
  - **Tests:** N/A — manual only
  - **Dependencies:** DEVICES-0112

- [ ] DEVICES-0119 — Do NOT fake Motion from Accelerometer+Gyroscope-only fusion math — explicit negative-verification task
  - **Area:** Motion / Correctness gate
  - **Files:** none changed; verification only
  - **Required behavior:** Re-read the final `Motion.cpp` after Phase 8 lands and confirm no code path does manual sensor-fusion math in the bridge when `TYPE_ROTATION_VECTOR`/`TYPE_GAME_ROTATION_VECTOR` (OS-fused) are used correctly — this task exists so a future implementer doesn't accidentally reintroduce the exact anti-pattern this whole plan is designed to avoid (per the Safety and Correctness Rules section) by, e.g., manually integrating raw gyroscope data instead of consuming Android's own fused rotation-vector output.
  - **Acceptance criteria:** Explicit sign-off note referencing the exact functions read.
  - **Tests:** N/A (review-only gate)
  - **Dependencies:** DEVICES-0112

### Phase 9: Android Demo APK and Manual Hardware Testing

- [ ] DEVICES-0120 — Audit whether `cna_demo_devices`'s current CMake target is even Android-buildable
  - **Area:** Android / Build
  - **Files:** `CMakeLists.txt` (read-only investigation, per DEVICES-0011's finding)
  - **Required behavior:** Confirm/deny that `add_executable(cna_demo_devices ...)` (not excluded for `ANDROID` today, unlike `cna_demo_xact`) would even link correctly for an Android target, given SDL-on-Android apps normally need a shared library (`libmain.so`) loaded by a Java `Activity`, not a standalone executable.
  - **Acceptance criteria:** Written yes/no with reasoning; feeds directly into DEVICES-0121.
  - **Tests:** attempt a `-DCNA_BUILD_EXAMPLES=ON` Android cross-compile of just this target and record the actual result (success/link error/etc.)
  - **Dependencies:** DEVICES-0011

- [ ] DEVICES-0121 — Fix or correctly guard `cna_demo_devices`'s Android CMake integration
  - **Area:** Android / Build
  - **Files:** `CMakeLists.txt`
  - **Required behavior:** Based on DEVICES-0120's finding: either (a) add the same `NOT ANDROID` guard `cna_demo_xact` has, deferring real Android packaging entirely to the Gradle-based path below, or (b) change the target type to a shared library if that's what's actually needed for Android, whichever DEVICES-0120 concludes is correct.
  - **Acceptance criteria:** No regression to the existing desktop/Emscripten build of `cna_demo_devices`; Android build result (whichever path chosen) is explicit and intentional, not accidental.
  - **Tests:** re-build `cna_demo_devices` for desktop (unchanged) and for Android (per the chosen fix)
  - **Dependencies:** DEVICES-0120

- [ ] DEVICES-0122 — Generate an `android-project` from SDL's template for `cna_demo_devices`
  - **Area:** Android / Packaging
  - **Files:** new `examples/demo_devices/android/` directory (generated via `third_party/SDL/build-scripts/create-android-project.py`)
  - **Required behavior:** Adapt SDL's vendored Gradle template, per `docs/devices-build.md` Section 4.1's own findings on what already exists (`third_party/SDL/android-project/`, `create-android-project.py`).
  - **Acceptance criteria:** Generated project structure exists and is checked in (or documented as a build-time generation step, whichever this project's convention prefers — check for precedent before choosing).
  - **Tests:** N/A (scaffolding)
  - **Dependencies:** DEVICES-0121

- [ ] DEVICES-0123 — Add `AndroidManifest.xml` with `VIBRATE` permission and optional sensor `uses-feature` declarations
  - **Area:** Android / Packaging
  - **Files:** `examples/demo_devices/android/app/src/main/AndroidManifest.xml`
  - **Required behavior:** `<uses-permission android:name="android.permission.VIBRATE"/>` (if Phase 3 concluded it's needed) plus **optional** (`android:required="false"`) `uses-feature` for `android.hardware.sensor.accelerometer`/`gyroscope`/`compass` — per the Safety rule "do not require Android sensors in the manifest unless the app explicitly needs them," since this is a diagnostic demo that should still install on devices missing any one sensor.
  - **Acceptance criteria:** Manifest correctly marks every sensor feature optional.
  - **Tests:** N/A (manifest content)
  - **Dependencies:** DEVICES-0122, DEVICES-0048

- [ ] DEVICES-0124 — Wire Gradle `externalNativeBuild` to CNA's own CMake
  - **Area:** Android / Packaging
  - **Files:** `examples/demo_devices/android/app/build.gradle`
  - **Required behavior:** Point Gradle's native build at the root `CMakeLists.txt` with the correct target (`cna_demo_devices` or whatever DEVICES-0121 concluded), `ANDROID_ABI`/`ANDROID_PLATFORM` matching `docs/devices-build.md`'s existing NDK r30/API 24 precedent.
  - **Acceptance criteria:** `./gradlew assembleDebug` (or equivalent) at least attempts the native build (may still fail — record the exact failure if so, this is real, failure-prone engineering per the design doc's own warning).
  - **Tests:** attempt the actual Gradle build, record result honestly
  - **Dependencies:** DEVICES-0123

- [ ] DEVICES-0125 — Produce an installable APK for `cna_demo_devices`
  - **Area:** Android / Packaging
  - **Files:** none new; build artifact only
  - **Required behavior:** `./gradlew assembleDebug` produces a `.apk` file.
  - **Acceptance criteria:** APK file exists; do not claim success if the build fails — document the exact error and stop, per NEXT.md's "do not claim Android/iOS hardware support... unless actually done."
  - **Tests:** N/A (build artifact)
  - **Dependencies:** DEVICES-0124

- [ ] DEVICES-0126 — Attempt install/run on the existing `Medium_Phone` AVD (expected to fail, per prior sessions)
  - **Area:** Android / Manual verification
  - **Files:** none
  - **Required behavior:** Re-attempt the emulator launch exactly as `docs/devices-build.md` Section 4.1 describes; if `/dev/kvm` is still absent, document that fact fresh (don't assume the old result still holds without re-checking — environments can change, per that doc's own caveat).
  - **Acceptance criteria:** Either a successful install+run (major finding, update everything downstream), or a freshly-confirmed same failure with the exact error text.
  - **Tests:** the actual `adb install`/`emulator` commands, run for real
  - **Dependencies:** DEVICES-0125

- [ ] DEVICES-0127 — CI build for the Android library target (not the APK) if feasible
  - **Area:** Android / CI
  - **Files:** whatever CI config this repo uses (check for `.github/workflows/` or equivalent first)
  - **Required behavior:** Add an Android NDK cross-compile step for the `CNA` library target (matching `docs/devices-build.md` Section 4's manual command) to CI, if CI infrastructure exists in this repo — this task is conditional on that infrastructure existing; if none exists, this task becomes "document the manual command as the CI-equivalent gate" instead.
  - **Acceptance criteria:** Either a real CI job added, or an explicit note that no CI exists in this repo and manual verification is the current gate.
  - **Tests:** N/A
  - **Dependencies:** none

- [ ] DEVICES-0128 — Write manual APK installation instructions
  - **Area:** Docs
  - **Files:** `docs/devices-build.md`
  - **Required behavior:** Document the exact `./gradlew assembleDebug` / `adb install` sequence used in DEVICES-0125/0126, including the real failure encountered if the emulator still can't run.
  - **Acceptance criteria:** Instructions are copy-pasteable and were actually run, not guessed.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0125, DEVICES-0126

- [ ] DEVICES-0129 — Document emulator limitations for Devices testing specifically
  - **Area:** Docs
  - **Files:** `docs/devices-hardware-checklist.md`
  - **Required behavior:** Note that even a working x86_64/ARM emulator typically has no real vibration motor and only simulated (not physical) sensor values (adjustable via the emulator's "Extended Controls" virtual sensor panel) — so a successful emulator run still cannot close every item in the hardware checklist, only the software-dispatch-plumbing ones.
  - **Acceptance criteria:** Doc note added.
  - **Tests:** N/A
  - **Dependencies:** none

- [ ] DEVICES-0130 — Full physical-device hardware-checklist pass, if/when hardware becomes available
  - **Area:** Manual verification
  - **Files:** `docs/devices-hardware-checklist.md` (results recorded, not the checklist itself rewritten)
  - **Required behavior:** Work through every unresolved item in the existing 6-case checklist (cases 1, 2, 3's phone-motor claim, 5, 6) plus every new item added by Phases 3/7/8 (DEVICES-0050, 0099, 0118), marking each verified/failed with concrete evidence.
  - **Acceptance criteria:** Checklist updated with real results; any failure becomes its own bug-fix task, not silently reinterpreted as expected.
  - **Tests:** N/A — this task IS the manual test
  - **Dependencies:** DEVICES-0125, DEVICES-0126, physical hardware availability (outside this plan's control)

- [ ] DEVICES-0131 — iOS toolchain re-check (confirm still blocked, do not attempt implementation)
  - **Area:** iOS / Audit
  - **Files:** none
  - **Required behavior:** Re-run the same toolchain check every prior phase has (`xcodebuild`/`xcrun`/`osxcross`/`*ios*toolchain*` search) — re-confirm, don't assume, per this environment's own established pattern of periodically re-checking things that could change.
  - **Acceptance criteria:** Fresh, dated confirmation either way.
  - **Tests:** the actual search commands, run for real
  - **Dependencies:** none

### Phase 10: Docs, Cleanup, CI, Final Compatibility Report

- [ ] DEVICES-0132 — Update `docs/devices-native-backend-design.md` from sketch to as-built record
  - **Area:** Docs
  - **Files:** `docs/devices-native-backend-design.md`
  - **Required behavior:** Once Phases 6–8 land, update this doc's status from "design sketch only, nothing implemented" to reflect what was actually built, keeping the iOS sections as still-a-sketch (explicitly unimplemented).
  - **Acceptance criteria:** Doc no longer claims "nothing implemented" for the parts that now are; iOS sections remain honestly marked unimplemented.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0095, DEVICES-0112

- [ ] DEVICES-0133 — Create `docs/devices-api-coverage.md`
  - **Area:** Docs
  - **Files:** `docs/devices-api-coverage.md` (new)
  - **Required behavior:** A standalone, per-member API coverage table (real API + `NOXNA` extensions) for all classes in scope, extracted from this plan's Phase 0 matrices — a permanent, easy-to-scan reference distinct from `AUDIT.md`'s prose-heavy history.
  - **Acceptance criteria:** Table covers every class in the Compatibility Matrix section above.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0002 through DEVICES-0008

- [ ] DEVICES-0134 — Create `docs/devices-android.md`
  - **Area:** Docs
  - **Files:** `docs/devices-android.md` (new)
  - **Required behavior:** Consolidate every Android-specific decision from Phases 2–9 (backend selection rationale, NDK sensor API vs. JNI decision, permission list, manifest feature-optionality rule, known emulator limitations) into one Android-focused doc, cross-linking rather than duplicating `docs/devices-native-backend-design.md`/`docs/devices-build.md`.
  - **Acceptance criteria:** No content duplicated verbatim from the other two docs — links instead.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0031, DEVICES-0073, DEVICES-0123

- [ ] DEVICES-0135 — Update `docs/devices-hardware-checklist.md`'s summary table
  - **Area:** Docs
  - **Files:** `docs/devices-hardware-checklist.md`
  - **Required behavior:** Refresh the "Net result: N of M cases verified" summary line to reflect Phase 9's actual outcome, whatever it turns out to be.
  - **Acceptance criteria:** Number is accurate as of the session that closes Phase 9.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0130

- [ ] DEVICES-0136 — Update `docs/devices-build.md` with every new build/test command introduced by this plan
  - **Area:** Docs
  - **Files:** `docs/devices-build.md`
  - **Required behavior:** Add sections for the new fake-backend test suites (Phase 2/6/7/8), the Android demo APK build (Phase 9), any new sanitizer-loop guidance specific to the new bridge code.
  - **Acceptance criteria:** Every new command in this plan has a corresponding, actually-run entry in this doc.
  - **Tests:** N/A
  - **Dependencies:** all Phase 1–9 tasks that introduce new build/test commands

- [ ] DEVICES-0137 — Update `examples/demo_devices` documentation/inline comments for the new sensors
  - **Area:** Docs / Demo
  - **Files:** `examples/demo_devices/src/DevicesDemo.{hpp,cpp}` (comments only, unless Phase 16-equivalent demo tasks below change behavior)
  - **Required behavior:** Update any comment claiming Compass/Motion are "always NotSupported" once Phase 7/8 give them a real Android backend.
  - **Acceptance criteria:** No stale comments contradicting the new behavior.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0095, DEVICES-0112

- [ ] DEVICES-0138 — Update `NOXNA.md`'s Devices-namespace entries if it enumerates them separately
  - **Area:** Docs
  - **Files:** `NOXNA.md`
  - **Required behavior:** Confirm whether `NOXNA.md` tracks `Microsoft::Devices` extensions separately from in-source tags; if so, add any new `NOXNA` members introduced by this plan (e.g. `IDeviceVibrationBackend`-related, if any becomes public-facing, which it should not).
  - **Acceptance criteria:** Consistent with the in-source `NOXNA` inventory from DEVICES-0009.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0009

- [ ] DEVICES-0139 — Write migration notes for a future iOS backend
  - **Area:** Docs
  - **Files:** `docs/devices-native-backend-design.md`
  - **Required behavior:** Confirm the `ICompassBackend`/`IMotionBackend` interfaces built in Phases 7/8 are still iOS-shaped (no Android-specific leakage into the interface itself) — this validates the interface design retroactively rather than writing new iOS code.
  - **Acceptance criteria:** Written confirmation; no `.mm`/Swift files added (no Apple toolchain in this environment, per DEVICES-0131).
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0086, DEVICES-0101, DEVICES-0131

- [ ] DEVICES-0140 — Full regression pass: entire Devices-only `ctest` filter, looped, all three sanitizers
  - **Area:** Testing / CI gate
  - **Files:** none
  - **Required behavior:** Final gate before closing this plan: run the full Devices-only filter (`docs/devices-build.md` Section 2) plus 40-iteration loop plus all three sanitizer presets (Section 6), on the final state of all phases.
  - **Acceptance criteria:** All green except the one pre-existing, documented `sharp-runtime` TSan finding.
  - **Tests:** full Devices-only suite, looped, ×3 sanitizers
  - **Dependencies:** every prior phase

- [ ] DEVICES-0141 — Full `ctest` suite regression pass (confirm no unrelated breakage)
  - **Area:** Testing / CI gate
  - **Files:** none
  - **Required behavior:** Run the complete `ctest` suite, confirm only the 2 pre-existing, unrelated `EasyGL` failures remain (same as every prior phase since Phase 5).
  - **Acceptance criteria:** No new failures outside `Microsoft::Devices`'s own scope.
  - **Tests:** full `ctest`
  - **Dependencies:** DEVICES-0140

- [ ] DEVICES-0142 — Write the final, dated compatibility report
  - **Area:** Docs
  - **Files:** `AUDIT.md`, `NEXT.md`
  - **Required behavior:** Replace `plan_devices_phase9.md` Task P9-7's status table with an updated one reflecting this plan's actual outcome (Compass/Motion real on Android or still stub if Phase 7/8 didn't fully land; Android APK packaged or still blocked; hardware verification results from DEVICES-0130).
  - **Acceptance criteria:** Table uses the same layered-status columns (API surface / SDL-or-native runtime / sanitizers / Android compile / physical hardware) `plan_devices_phase9.md` established — no regression to a flatter, less honest format.
  - **Tests:** N/A
  - **Dependencies:** DEVICES-0140, DEVICES-0141

- [ ] DEVICES-0143 — Close this plan file
  - **Area:** Process
  - **Files:** `plan_devices.md` (this file)
  - **Required behavior:** Once every task above is either done or explicitly deferred with a reason (hardware unavailable, DEVICES-0031 concluded "not needed", etc.), mark this plan closed per this project's own convention (`plan_devices.md` → `plan_devices_phase2.md`-style rename is NOT this plan's convention this time, since `plan_devices.md` itself was previously retired after Phase 1 — follow whatever the maintainer prefers at closing time: archive, delete, or leave as historical record).
  - **Acceptance criteria:** Every checkbox in this file is either checked or has an explicit "deferred: <reason>" note next to it — no silently-abandoned tasks.
  - **Tests:** N/A
  - **Dependencies:** every task above

---

## Manual Hardware Test Matrix

| Item | Source | Status after this plan (fill in when run) |
|---|---|---|
| Android accelerometer axis/timestamp sanity | `docs/devices-hardware-checklist.md` §1 | Pending DEVICES-0130 |
| Android gyroscope axis correctness | §2 | Pending DEVICES-0130 |
| VibrateController actually vibrates phone motor | §3 | Pending DEVICES-0050, DEVICES-0130 |
| StartLeftRight drives two distinct motors | §4 | Pending DEVICES-0130 |
| Gamepad-exclusion doesn't compete with GamePad::SetVibration | §5 | Pending DEVICES-0130 |
| iOS device/toolchain | §6 | Pending DEVICES-0131 (expected: still blocked) |
| Compass MagneticHeading vs. reference compass | new, DEVICES-0099 | Pending DEVICES-0130 |
| Compass Calibrate fires on figure-8 gesture | new, DEVICES-0099 | Pending DEVICES-0130 |
| Motion Attitude tracks known rotation | new, DEVICES-0118 | Pending DEVICES-0130 |
| Motion Gravity/DeviceAcceleration split sanity | new, DEVICES-0118 | Pending DEVICES-0130 |
| Native Android vibration backend (if built) vs. SDL path | new, DEVICES-0050 | Pending DEVICES-0031's outcome |

## CI Test Matrix

| Target | Platform | Status |
|---|---|---|
| `CNA` (library) | Linux desktop, EASYGL/VULKAN/BGFX | Green (pre-existing) |
| `CnaTests` | Linux desktop | Green (pre-existing); Devices-only filter ~229 tests |
| `CNA` (library) | Android NDK r30, arm64-v8a, API 24 | Green, compile-only (pre-existing); re-verify after Phases 6-8 (DEVICES-0097, DEVICES-0115) |
| `cna_demo_devices` | Android | New, Phase 9 (DEVICES-0120-0127) — status TBD |
| Sanitizers (ASan/TSan/UBSan) | Linux desktop | Green (pre-existing); re-verify after every phase touching concurrency (DEVICES-0026, 0036, 0070, 0140) |
| iOS (any target) | N/A | Blocked, no toolchain (DEVICES-0131) |

## Documentation Tasks

Already itemized as Phase 10 (DEVICES-0132–0139). Summary of net-new docs:
`docs/devices-api-coverage.md` (new), `docs/devices-android.md` (new). Updated:
`docs/devices-native-backend-design.md`, `docs/devices-hardware-checklist.md`,
`docs/devices-build.md`, `AUDIT.md`, `NEXT.md`, `NOXNA.md` (conditionally).

## Known Risks

- **DEVICES-0031's outcome could invalidate most of Phase 3.** If SDL3's Android haptic
  backend already fully covers the vibration use case, building a parallel JNI backend
  would be pure wasted, unauthorized-scope work — this is why that task is a hard gate
  before any Phase 3 implementation task starts.
- **Phase 6's NDK-native-vs-JNI decision (DEVICES-0073) changes many downstream task
  shapes.** If the pure NDK `ASensorManager` path is viable (likely, since Android
  ships it precisely for native apps), Phases 6–8 are meaningfully simpler and have no
  JNI-specific risk (global reference leaks, JVM attach/detach) at all — re-scope
  Phase 6/7/8 task titles once this is decided, don't build both paths speculatively.
- **No physical Android/iOS hardware or rumble-capable gamepad in this environment**,
  same constraint every prior phase has hit. Phases 3/7/8/9's "real" implementation work
  can be built, unit-tested via fakes, and cross-compiled, but genuinely cannot be
  hardware-verified until DEVICES-0130's preconditions are met outside this plan's
  control.
- **No `/dev/kvm` in this container** blocks the one configured x86_64 AVD regardless of
  how well Phase 9's Gradle integration works — an ARM-image AVD or a host with KVM
  exposed could change this, per `docs/devices-build.md`'s own note.
- **Compass `TrueHeading` is permanently limited** without `System.Device.Location` —
  this is an accepted, documented limitation, not a defect to chase further within this
  plan's scope.
- **Concurrency regressions are the highest-severity risk class in this namespace's
  history** (Phases 5–8 each found a real bug the previous phase's own tests missed).
  Every phase here that touches `SensorBase<T>`, `Detail::SdlSensorSubsystem<TSensor>`,
  or the new `AndroidSensorBridge` must re-run the loop/sanitizer gates
  (DEVICES-0026/0036/0070/0140) — do not skip these because "the tests passed once."

## Definition of Done

This plan is done when:

1. Every task above is checked or has an explicit, reasoned "deferred" note (no silent
   drops).
2. DEVICES-0031's decision is recorded and Phase 2/3 either fully executed or explicitly
   skipped per that decision.
3. `Compass`/`Motion` are either genuinely `IsSupported() == true` with a working
   `AndroidCompassBackend`/`AndroidMotionBackend` on Android, or explicitly still
   `NotSupported` everywhere with a documented reason referencing whichever task blocked
   it.
4. The full Devices-only test suite (including all new fake-backend suites) passes,
   looped 40+ times, clean under ASan/TSan/UBSan except the one known unrelated
   `sharp-runtime` finding (DEVICES-0140).
5. The full `ctest` suite has no new failures beyond the 2 pre-existing, unrelated
   `EasyGL` ones (DEVICES-0141).
6. `AUDIT.md`/`NEXT.md`/`docs/devices-api-coverage.md`/`docs/devices-android.md` reflect
   the plan's actual, honest end state — layered per component (API/runtime/sanitizers/
   Android compile/physical hardware), never a flat "complete" claim (DEVICES-0142).
7. Every item in the Manual Hardware Test Matrix above is either physically verified
   with recorded evidence, or explicitly still blocked with a concrete, current reason
   (no hardware, no KVM, no toolchain) — never silently assumed.
8. No GPS/location code was added anywhere in `Microsoft::Devices::Sensors`, and no
   Compass/Motion reading was ever synthesized from insufficient sensor data (DEVICES-
   0100, DEVICES-0119's explicit gate tasks passed).

---

## Safety and Correctness Rules (restated for this plan's execution)

- Do not fake XNA-compatible `Compass` if magnetometer/heading data is not available.
- Do not fake XNA-compatible `Motion` from insufficient data (manual fusion when the OS
  already provides a fused rotation vector).
- Do not let `IsSupported` return `true` unless the required backend is genuinely usable.
- Do not break existing `Accelerometer`/`Gyroscope` behavior while adding Android-native
  backends elsewhere.
- Do not merge `GamePad::SetVibration` semantics with phone `VibrateController`.
- Do not expose `NOXNA` features as XNA compatibility.
- Do not require Android sensors in the manifest unless the app explicitly needs them.
- Do not make desktop tests depend on physical sensors.
- Do not assume emulator haptics/sensors are equivalent to real hardware.
- Do not silently swallow serious backend initialization errors in debug/test builds
  (the one documented exception: `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()`'s
  deliberate per-instance handler-exception swallowing, Task P8-5 — that is a reasoned,
  documented exception to this rule, not a contradiction of it).
- Do not create giant untestable tasks — if executing any task above reveals it is
  actually several independent pieces of work, split it further rather than doing it as
  one unreviewable change.
- Do not add GPS/location to `Microsoft::Devices::Sensors` under any circumstances,
  including as `NOXNA`.
- Do not restructure `SensorBase<T>`/`Detail::SdlSensorSubsystem<TSensor>`'s existing
  locking/dispatch scheme without a concrete, newly-found bug.
- Do not claim Android/iOS hardware support, or an APK/emulator run, unless it was
  actually done in the current session.
