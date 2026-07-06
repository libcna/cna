# CNA Devices and Sensors XNA 4.0 Compatibility Plan

**Written from scratch on 2026-07-05.** This document fully replaces every prior version
of `plan_devices.md` (the original 31-task plan, `plan_devices_phase2.md` through
`plan_devices_phase9.md`, and the second 143-task/Phases-0-10 plan). None of those task
IDs, phase numbers, "done" claims, or assumptions carry over. Prior plans are historical
record only — see git history if that detail is ever needed — and are not a source of
truth for this plan. Every task below was written after inspecting the current state of
the code, headers, tests, demo, and build files in this repository, not by trusting any
earlier plan's status text.

This plan does not implement anything. It only defines the work. No task listed here has
been carried out as part of writing this plan.

**Terminology note, stated once here and assumed throughout the rest of this document:**
the correct XNA 4.0 / Windows Phone class name is `Microsoft::Devices::VibrateController`.
"VibrationController" is not the XNA API name and must not be used as the public class
name, in documentation, or in code — see Section 4 (`VIB-001`) for the audit task that
enforces this everywhere else in the repository.

---

## 0. Scope

This plan covers:

- `Microsoft::Devices::VibrateController`
- `Microsoft::Devices::Sensors::SensorBase<TSensorReading>`
- `Microsoft::Devices::Sensors::Accelerometer`
- `Microsoft::Devices::Sensors::Gyroscope`
- `Microsoft::Devices::Sensors::Compass`
- `Microsoft::Devices::Sensors::Motion`
- Sensor reading structs and event-args classes (`AccelerometerReading`,
  `GyroscopeReading`, `CompassReading`, `MotionReading`, `AttitudeReading`,
  `AccelerometerReadingEventArgs`, `SensorReadingEventArgs<T>`, `CalibrationEventArgs`,
  `ISensorReading`, `SensorState`, `SensorFailedException`,
  `AccelerometerFailedException`)
- Android sensor backends (`Detail::AndroidSensorBridge`, `Detail::AndroidCompassBackend`,
  `Detail::AndroidMotionBackend`, `Detail::AndroidCompassMath`, `Detail::AndroidMotionMath`,
  `Detail::AndroidSensorOrientation`)
- SDL sensor backends (`Detail::SdlSensorSubsystem<TSensor>`, used by `Accelerometer` and
  `Gyroscope`)
- Platform vibration/haptic backends (currently SDL3 haptic only — see Section 4)
- Tests, demos, docs, and CI for all of the above

This plan does not cover unrelated input, audio, graphics, or gamepad APIs, except where
they interact with vibration or sensor routing (e.g. `VibrateController` deliberately
excluding gamepad haptic devices so it never competes with
`Microsoft::Xna::Framework::Input::GamePad::SetVibration()`).

---

## 1. Baseline observations (verified by inspection, 2026-07-05)

These are facts confirmed by reading the current repository — not carried over from any
old plan's claims. Where a fact could not be directly confirmed by reading code, it is
stated as unverified rather than assumed.

- **`VibrateController` is the correct XNA name, already used correctly as the public
  class name** in `include/Microsoft/Devices/VibrateController.hpp` and
  `src/Microsoft/Devices/VibrateController.cpp`. It currently has exactly one backend:
  SDL3's `SDL_Haptic` API (`SDL3/SDL_haptic.h`). There is no dedicated phone-vibrator
  backend, no `IVibrateBackend` abstraction, and no Android JNI vibrator bridge — SDL3's
  own bundled Android haptic backend is relied on to reach `Context.VIBRATOR_SERVICE`
  (documented in a comment in `VibrateController.cpp` above `OpenFirstHapticDevice()`).
  `Start(TimeSpan)` is the strict XNA method; `Start(TimeSpan, float)`,
  `getIsSupportedProperty()`, `getDeviceNameProperty()`, and `StartLeftRight(...)` are
  all marked `NOXNA` in the header.
- **`Accelerometer` and `Gyroscope` are SDL3-backed** via the shared
  `Detail::SdlSensorSubsystem<TSensor>` template
  (`include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`). `Accelerometer`
  additionally raises a legacy `ReadingChanged` event alongside `CurrentValueChanged`
  (`AccelerometerReadingEventArgs`); `Gyroscope` only raises `CurrentValueChanged`.
- **`Compass` and `Motion` are real on Android only**, via
  `Detail::AndroidCompassBackend`/`Detail::AndroidMotionBackend`, both built on the
  shared `Detail::AndroidSensorBridge` (a pure-NDK `ASensorManager`/`ASensorEventQueue`/
  `ALooper` wrapper, no JNI). On every other platform both classes keep the same
  permanent stub behavior they always had (`getIsSupportedProperty()` false,
  `Start()` throws). Neither has an iOS backend.
- **`getStateProperty()`'s `NOXNA` split across the four sensor classes is intentional,
  not drift — re-confirmed 2026-07-06 (`DEV-API-003`).** `Accelerometer::getStateProperty()`
  has no `NOXNA` marker while `Gyroscope`/`Compass`/`Motion`'s equivalents all do; this
  plan's Section 1 draft (written before that re-check) flagged the shape difference as
  unexplained drift, but a prior pass (`plan_devices_phase2.md` Task P2-17, 2026-07-02)
  had already resolved the underlying question against archived MSDN "previous-versions"
  pages: `Accelerometer.State` is real WP7 API (MSDN `ff707930`, cited in `AUDIT.md`'s
  `Accelerometer` row and `docs/devices-api-coverage.md`), while `Gyroscope.State`
  (`hh239201`), `Compass.State` (`hh220912`), and `Motion.State` (`hh239189`) do not exist
  on the real classes — so their `getStateProperty()` is correctly a CNA symmetry
  extension. No code change needed; see `DEV-API-003`'s closing note.
- **Verified: `TimeBetweenUpdates` is not enforced by the SDL backends at all.**
  `src/Microsoft/Devices/Sensors/Accelerometer.cpp` and
  `src/Microsoft/Devices/Sensors/Gyroscope.cpp` contain **zero** references to
  `getTimeBetweenUpdatesProperty()` — the value is stored and
  `TimeBetweenUpdatesChanged` fires on change (both handled generically in
  `SensorBase<T>`), but nothing reads it back to throttle dispatch or configure SDL's
  sensor polling rate. On Android, `Detail::AndroidCompassBackend`/
  `Detail::AndroidMotionBackend` do forward `timeBetweenUpdates` into
  `Detail::AndroidSensorBridge::Start()`, which converts it to
  `ASensorEventQueue_setEventRate()`'s microsecond parameter — but only at `Start()`
  time; there is no code path today that changes the requested rate on an
  already-running bridge. See `SENSORBASE-001`, `ACCEL-005`, `GYRO-004`,
  `ANDROID-BRIDGE-002`, `SDL-SENSOR-002`.
- **Verified: two different "sensor failed" exception types are in use.** `Accelerometer`
  throws its own dedicated `AccelerometerFailedException`
  (`include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp`), while
  `Gyroscope`, `Compass`, and `Motion` all throw the generic `SensorFailedException`
  (`include/Microsoft/Devices/Sensors/SensorFailedException.hpp`) — confirmed by
  grepping `throw` sites in all four `.cpp` files. Whether this split is intentional
  (`AccelerometerFailedException` being the one real XNA/WP7 exception type, with
  `SensorFailedException` a CNA-invented stand-in for the other three, which may not
  have a real XNA equivalent at all) has not been verified against any authoritative
  XNA/WP7 API reference. See `DEV-API-005`.
- **Verified: `Compass::TrueHeading` is deliberately set equal to `MagneticHeading`.**
  `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  (`PublishReading()`) has an explicit comment stating this is intentional because real
  declination requires a location source this codebase does not implement. This is a
  documented current behavior, not a bug — but whether it is the *correct* XNA-compatible
  behavior for "declination unknown" has not been verified against any authoritative
  reference. See `COMPASS-002`.
- **Verified: `Compass::getIsSupportedProperty()` requires both a rotation-vector sensor
  and a magnetic-field sensor** (`AndroidCompassBackend::IsSupported()` returns
  `rotationVectorBridge_.IsAvailable() && magneticFieldBridge_.IsAvailable()`). Devices
  with only a magnetometer are reported unsupported today. See `COMPASS-005`.
- **Verified: no CI infrastructure exists in this repository.** `.github/workflows/`
  does not exist. All verification in this repository today is manual, local
  `cmake`/`ctest` invocation. See `DEV-BUILD-003`.
- **Verified: three sanitizer CMake presets already exist** —
  `devices-asan`, `devices-tsan`, `devices-ubsan` — confirmed in `CMakePresets.json`
  (both as configure and matching build presets, alongside `web` and `tests`). These
  are reused directly in Section 14's verification commands rather than inventing new
  preset names.
- **Verified: this environment has all four required submodules/vendored dependencies
  present** (`third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer`,
  `vendor/googletest`, confirmed via `git submodule status`) — this specific checkout is
  not a bare ZIP export. Whether a *fresh* clone reliably reaches this same state with
  clearly actionable errors if a submodule is missing has not been re-verified as part
  of writing this plan. See `DEV-BUILD-001`.
- **The test suite is extensive by file count** — 21 test files under
  `tests/Microsoft/Devices/` covering every reading struct, event-args class, exception
  type, `SensorBase<T>` itself, the SDL ownership/dispatch machinery
  (`SensorSubsystemOwnershipTests.cpp`), the Android bridge and its pure-math helpers
  (`AndroidSensorBridgeTests.cpp`, `AndroidCompassMathTests.cpp`,
  `AndroidMotionMathTests.cpp`, both under `tests/Microsoft/Devices/Sensors/Detail/`),
  and `VibrateController`. Whether these tests currently build and pass in a given
  environment must still be confirmed by actually running them (Section 14) — file
  count alone is not evidence of passing behavior, and this plan does not claim any
  test currently passes without having run it.
- **Hardware orientation/coordinate-mapping correctness for `Accelerometer`,
  `Gyroscope`, `Compass`, and `Motion` has never been verified against real Android (or
  any) hardware** in any session that produced the prior plans — this is stated
  explicitly in this repository's own `NEXT.md` and `docs/devices-hardware-checklist.md`,
  and nothing found while writing this plan contradicts that. Treat every axis-sign,
  unit-conversion, and heading/attitude claim in Sections 6–9 as needing real-device
  confirmation, not as already-settled.
- **Existing docs relevant to this plan** (to be updated by the tasks that touch them,
  not duplicated): `docs/devices-build.md`, `docs/devices-hardware-checklist.md`,
  `docs/devices-native-backend-design.md`, `docs/devices-api-coverage.md`,
  `docs/devices-android.md`. None of these currently contain a strict XNA-vs-`NOXNA`
  public API matrix, a phone-vibrator-vs-desktop-haptic backend split writeup, or a CI
  description — those are new artifacts this plan's tasks produce.

Nothing in this section should be read as "these are the only problems" — it is the set
of facts that grounded the specific tasks below, not an exhaustive audit result.

---

## 2. Build and verification bootstrap tasks

### DEV-BUILD-001 — Restore reproducible local build

- **Priority:** Critical
- **Area:** Build / CI
- **Problem:** Device/sensor tests cannot be trusted until the project builds
  reproducibly from a clean checkout. This environment already has submodules
  initialized, but that has not been re-verified from a genuinely fresh clone as part of
  this plan.
- **Required work:**
  - Verify required submodules and vendored dependencies from an actually fresh clone
    (`git clone --recurse-submodules`, or `git submodule update --init --recursive`
    after a plain clone).
  - Add or update bootstrap instructions for SDL, SDL_image, SDL_mixer, googletest, and
    any platform-specific sensor dependencies (Android NDK path, minimum API level).
  - Make missing-dependency CMake configure errors actionable (clear message naming the
    missing submodule/package and the exact command to fix it), instead of a generic
    "file not found" from deep inside a `third_party/` include path.
- **Acceptance criteria:**
  - A clean checkout can be prepared with documented commands that another engineer can
    copy-paste without guessing.
  - `cmake --preset devices-ubsan` configures successfully from that clean checkout.
  - `CnaTests` builds successfully from that clean checkout.
- **Suggested files to inspect or edit:**
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `third_party/` (submodule pointers only, never edit vendored content itself)
  - `vendor/` (same caveat)
  - `docs/devices-build.md`
  - `plan_devices.md`

### DEV-BUILD-002 — Add device-only verification commands

- **Priority:** High
- **Area:** Build / Tests
- **Problem:** There is no single documented command sequence specifically for
  Devices/Sensors verification with sanitizer variants included; `docs/devices-build.md`
  exists but must be checked against the actual current test-suite filter (a stale
  filter that silently drops new test suites has happened before in this repository's
  history).
- **Required work:**
  - Document exact configure/build/test commands for Devices/Sensors only.
  - Include a `gtest_filter`/`ctest -R` pattern covering every current Devices/Sensors
    test suite by name (verify the full current list from `tests/Microsoft/Devices/`
    rather than copying an old filter forward).
  - Include normal, `devices-asan`, `devices-tsan`, and `devices-ubsan` variants.
- **Acceptance criteria:**
  - `docs/devices-build.md` (or `plan_devices.md`, cross-linked) contains copy-pasteable
    commands.
  - The documented filter is verified to actually match every current Devices/Sensors
    test suite (no silently-dropped suite).
- **Suggested files to inspect or edit:**
  - `docs/devices-build.md`
  - `CMakePresets.json`
  - `tests/Microsoft/Devices/` (full recursive listing, to build the filter from)

### DEV-BUILD-003 — Add CI job for Devices/Sensors

- **Priority:** High
- **Area:** CI
- **Problem:** No CI infrastructure exists in this repository at all (`.github/workflows/`
  is absent) — sensor/vibration regressions can currently slip in with zero automated
  gate.
- **Required work:**
  - Add CI (e.g. GitHub Actions) to build and run Devices/Sensors tests on at least one
    desktop platform.
  - Ensure no real hardware is required for the default CI path — fake/injected
    backends only (see `ACCEL-006`, `GYRO-005`, `VIB-009`).
  - Clearly mark any hardware-dependent test as manual/integration-only, excluded from
    the default CI filter.
- **Acceptance criteria:**
  - CI has a Devices/Sensors job that runs on every push/PR touching these paths.
  - CI runs all pure unit tests without physical sensors or haptic hardware present.
  - Hardware tests are excluded from the CI filter with a clear comment explaining why.
- **Suggested files to inspect or edit:**
  - `.github/workflows/` (new)
  - `tests/Microsoft/Devices/`
  - `tests/Microsoft/Devices/Sensors/`
  - `docs/devices-build.md`

---

## 3. Public API compatibility audit tasks

### DEV-API-001 — Create official XNA public API matrix

- **Priority:** Critical
- **Area:** API Compatibility
- **Problem:** There is no explicit, single table comparing CNA's current public API in
  this area to XNA 4.0 / Windows Phone 7's actual API surface. `docs/devices-api-coverage.md`
  exists but was not written against a fresh, from-scratch audit for this plan and must
  be re-verified, not assumed current.
- **Required work:**
  - Build a table with one row per public class, struct, method, property, enum, event,
    and exception in this plan's scope (Section 0).
  - Mark each row as: strict XNA 4.0, Windows Phone 7 legacy (e.g. `ReadingChanged`),
    CNA `NOXNA` extension, or internal-only (should not be public at all).
  - Include `VibrateController` explicitly — not "VibrationController" — as its own
    section of the table.
- **Acceptance criteria:**
  - The matrix exists (in `docs/devices-api-coverage.md` or a new file this task
    creates) and covers every public member currently declared in the headers listed
    below.
  - The matrix identifies at least the known drift already found in Section 1
    (`getStateProperty()`'s inconsistent `NOXNA` marking) as a concrete example of
    something it must catch.
  - The matrix distinguishes missing API (present in real XNA/WP7 but absent here),
    extra API (present here but not in XNA/WP7 and not marked `NOXNA`), and wrong
    signatures.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `include/Microsoft/Devices/Sensors/*.hpp`
  - `include/Microsoft/Devices/Sensors/Detail/*.hpp`
  - `docs/devices-api-coverage.md`

### DEV-API-002 — Enforce the `NOXNA` boundary

- **Priority:** Critical
- **Area:** API Compatibility
- **Problem:** CNA-specific extensions must not silently become indistinguishable from
  the strict XNA API. 36 `NOXNA` occurrences already exist across 13 headers under
  `include/Microsoft/Devices/` (confirmed by grep) — this task audits whether that
  marking is complete and consistently enforced, not whether `NOXNA` is used at all.
- **Required work:**
  - Audit every `NOXNA` declaration in Devices/Sensors headers against `DEV-API-001`'s
    matrix.
  - Verify every non-XNA method/property is consistently marked (the `getStateProperty()`
    drift in Section 1 is the known counter-example to fix first).
  - Add a compile-time or test-time check if the project's build system can express a
    "strict XNA surface" mode; if it cannot yet, document that limitation rather than
    silently skipping this requirement.
- **Acceptance criteria:**
  - No CNA-only method/property is missing a `NOXNA` marker in any header in scope.
  - `NOXNA` extensions are documented separately from strict XNA API (in the matrix from
    `DEV-API-001`).
  - A test (or documented manual check, if no strict-mode build exists yet) fails when
    an extension is accidentally left unmarked.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `include/Microsoft/Devices/Sensors/Compass.hpp`
  - `include/Microsoft/Devices/Sensors/Motion.hpp`
  - `tests/Microsoft/Devices/`
  - `tests/Microsoft/Devices/Sensors/`

### DEV-API-003 — Standardize `State`/`getStateProperty()` exposure — CLOSED, no code change (2026-07-06)

- **Priority:** High
- **Area:** API Compatibility
- **Problem as originally stated:** apparent inconsistency (Section 1 draft):
  `Accelerometer::getStateProperty()` has no `NOXNA` marker; `Gyroscope::getStateProperty()`,
  `Compass::getStateProperty()`, and `Motion::getStateProperty()` all do.
- **Resolution (2026-07-06):** not a bug. This exact question was already investigated
  and answered by an earlier pass, `plan_devices_phase2.md` Task P2-17 (2026-07-02),
  against archived MSDN "previous-versions" pages (cited in `AUDIT.md`'s per-class rows
  and `docs/devices-api-coverage.md`):
  - `Accelerometer.State` is real WP7 API — confirmed against MSDN `ff707930`. Its
    `getStateProperty()` correctly has **no** `NOXNA` marker.
  - `Gyroscope.State` (`hh239201`), `Compass.State` (`hh220912`), and `Motion.State`
    (`hh239189`) do **not** exist on the real classes. Their `getStateProperty()` is a
    CNA-added symmetry extension, correctly marked `NOXNA`.
  - The four classes are therefore already consistent with the authoritative reference —
    "all `NOXNA` or all strict XNA" (this task's original acceptance criterion) was the
    wrong bar; the real API itself is asymmetric across these four sibling classes.
  - `getStateProperty()`'s actual runtime behavior (the `SensorState` values it returns)
    already has test coverage in `AccelerometerTests.cpp`/`GyroscopeTests.cpp`/etc. —
    `NOXNA` itself is a compile-time-only empty marker macro (`CNAHelper.hpp`), so there
    is no additional runtime test to add for the marking policy; it is enforced by
    code/doc review, not `ctest`.
  - No header or source change made. `docs/devices-api-coverage.md` and `AUDIT.md`
    already reflect this; this closing note exists so a future pass doesn't re-open the
    same already-answered question.
- **Suggested files inspected (no changes needed):**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `include/Microsoft/Devices/Sensors/Compass.hpp`
  - `include/Microsoft/Devices/Sensors/Motion.hpp`
  - `include/Microsoft/Devices/Sensors/SensorState.hpp`

### DEV-API-004 — Audit reading struct `ToString`, equality, and hash behavior

- **Priority:** High
- **Area:** API Compatibility
- **Problem:** Every reading struct (`AccelerometerReading`, `GyroscopeReading`,
  `CompassReading`, `MotionReading`, `AttitudeReading`) implements `ToString()`,
  equality, and hashing; whether this matches expected .NET `ValueType`-style behavior,
  or is CNA convenience behavior that happens to look plausible, has not been verified
  against an authoritative source.
- **Required work:**
  - Verify expected behavior for all reading structs and event-args classes.
  - Decide, per struct, whether CNA should mimic .NET `ValueType.ToString()` conventions
    exactly or keep the current C++-convenience format behind a documented `NOXNA`
    rationale.
  - Add or extend tests for the decided behavior.
- **Acceptance criteria:**
  - All reading structs have documented, decided compatibility behavior (not just
    "whatever the code currently does").
  - Tests cover `ToString()`, equality (`==`/`!=`), and hash consistency for every
    reading struct that implements them.
  - Any non-XNA convenience behavior is explicitly labelled as an extension in docs.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/AccelerometerReading.hpp`
  - `include/Microsoft/Devices/Sensors/GyroscopeReading.hpp`
  - `include/Microsoft/Devices/Sensors/CompassReading.hpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `src/Microsoft/Devices/Sensors/*Reading.cpp`
  - `tests/Microsoft/Devices/Sensors/*ReadingTests.cpp`

### DEV-API-005 — Audit exception types and messages

- **Priority:** High
- **Area:** API Compatibility
- **Problem:** Confirmed (Section 1): `Accelerometer` throws its own
  `AccelerometerFailedException`; `Gyroscope`, `Compass`, and `Motion` all throw the
  generic `SensorFailedException`. Whether this split matches real XNA/WP7 exception
  types (which may not have a generic "sensor failed" exception at all) is unverified.
- **Required work:**
  - Verify expected exception types/messages for: unsupported sensor, disposed sensor,
    double `Start()`, double `Stop()`, failed `Start()`, invalid `TimeBetweenUpdates`.
  - Decide, with a documented rationale, whether `SensorFailedException` should be kept
    as a CNA-wide stand-in (and marked `NOXNA` if so) or whether `Gyroscope`/`Compass`/
    `Motion` should gain their own dedicated exception types matching
    `AccelerometerFailedException`'s pattern.
  - Add tests for every public failure path per class.
- **Acceptance criteria:**
  - Exception-type policy is documented in `DEV-API-001`'s matrix.
  - Tests cover unsupported sensor, disposed sensor, double start, double stop, and
    invalid parameters for all four sensor classes.
  - Messages are either verified-compatible or explicitly documented as intentionally
    non-exact CNA wording.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp`
  - `include/Microsoft/Devices/Sensors/SensorFailedException.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerFailedExceptionTests.cpp`
  - `tests/Microsoft/Devices/Sensors/SensorFailedExceptionTests.cpp`

---

## 4. `VibrateController` tasks

**Reminder:** the XNA 4.0 class name is `VibrateController`. Any occurrence of
"VibrationController" found while doing this work is a naming error to fix (`VIB-001`),
not an alternate spelling to preserve.

### VIB-001 — Correct terminology everywhere

- **Priority:** Critical
- **Area:** Vibration API
- **Problem:** The XNA class is `VibrateController`, not `VibrationController`. The
  public class name in this repository is already correct
  (`include/Microsoft/Devices/VibrateController.hpp`), but docs, comments, tests,
  examples, and any future plan text must be audited to make sure the wrong name never
  creeps in.
- **Required work:**
  - Grep the whole repository (docs, comments, tests, examples, this plan file's own
    future edits) for "VibrationController" and fix any occurrence found, unless it is
    explicitly quoting/explaining the common mistake.
  - Ensure the public class stays exactly `VibrateController` through every task in this
    section.
- **Acceptance criteria:**
  - Public API uses `VibrateController` everywhere.
  - No documentation, comment, test name, or example accidentally says
    "VibrationController" as if it were the real name.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`
  - `examples/demo_devices/`
  - `docs/devices-*.md`
  - `plan_devices.md`

### VIB-002 — Split XNA phone vibration from SDL haptics

- **Priority:** Critical
- **Area:** Vibration Backend
- **Problem:** Confirmed (Section 1): today there is exactly one backend, SDL3's
  `SDL_Haptic`, used for both the strict XNA `Start(TimeSpan)` and every `NOXNA`
  extension. A generic SDL haptic device (e.g. an arbitrary desktop force-feedback
  wheel) is not the same concept as "the Windows Phone's vibration motor," and treating
  them identically is a compatibility risk, not just a naming one.
- **Required work:**
  - Introduce a backend abstraction (e.g. `Detail::IVibrateBackend`) that
    `VibrateController` calls through, instead of calling `SDL_Haptic` functions
    directly.
  - Separate "the phone/system vibration motor" backend concept from "any SDL haptic
    device" — the desktop SDL-haptic path should be an explicit, documented fallback or
    `NOXNA`-flavored behavior, not silently presented as equivalent to strict XNA phone
    vibration.
  - Make the backend choice injectable for tests (see `VIB-009`).
- **Acceptance criteria:**
  - Strict XNA `Start(TimeSpan)` behavior does not rumble an arbitrary desktop haptic
    device (e.g. a random USB force-feedback gadget) as if it were "the phone vibrating,"
    unless that mapping is explicitly documented as the deliberate desktop behavior.
  - Backend choice is documented and independently testable.
  - Unit tests can inject a fake `IVibrateBackend`.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `src/Microsoft/Devices/Detail/` (new — no `Detail/` directory exists yet under
    `Microsoft::Devices`, unlike `Microsoft::Devices::Sensors::Detail`)
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-003 — Implement Android phone vibrator backend

- **Priority:** Critical
- **Area:** Android Backend
- **Problem:** Confirmed (Section 1): there is no dedicated Android vibrator backend
  today — the code relies entirely on SDL3's own bundled Android haptic backend
  reaching `Context.VIBRATOR_SERVICE`. This was a previously-made, deliberate decision
  (per an existing comment in `VibrateController.cpp`) to not build a redundant native
  bridge; this task's job is to re-verify that decision still holds, not to assume it
  is wrong.
- **Required work:**
  - Re-verify, by reading SDL3's actual Android haptic backend source
    (`third_party/SDL`), that it truly reaches `Vibrator`/`VibratorManager` with
    adequate amplitude control for this project's needs.
  - If the existing SDL3-only approach is confirmed sufficient, document that
    re-verification explicitly (do not silently re-assert the old conclusion without
    having checked it again).
  - If gaps are found (e.g. missing `VibrationEffect`/`VibratorManager` support for
    modern Android versions, or missing legacy fallback), implement or plan a
    dedicated Android backend behind the `IVibrateBackend` abstraction from `VIB-002`.
  - Ensure Android manifest permissions are documented (`VIBRATE` permission) whether or
    not a new native path is added.
- **Acceptance criteria:**
  - The Android demo (`examples/demo_devices/android/`) can vibrate a physical phone's
    motor.
  - Backend handles devices without a vibrator gracefully (no crash, `IsSupported`-style
    check returns false).
  - Unit tests cover backend selection using fake platform hooks, not a physical device.
  - A manual test checklist entry records Android OS/API version and device model used.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `src/Microsoft/Devices/Detail/` (new, from `VIB-002`)
  - `third_party/SDL/src/haptic/` (read-only research — vendored, do not edit)
  - `examples/demo_devices/android/`
  - `docs/devices-android.md`

### VIB-004 — Add iOS vibration backend plan or implementation

- **Priority:** High
- **Area:** iOS Backend
- **Problem:** There is no iOS toolchain available in this development environment
  (confirmed repeatedly in this repository's own history), and no explicit iOS-native
  vibration/haptics path exists in the code.
- **Required work:**
  - Decide whether CNA should support iOS vibration in this API at all, and document
    the decision with a rationale.
  - If yes: plan (or implement, if an Apple toolchain ever becomes available) using
    `UIImpactFeedbackGenerator`/`CHHapticEngine` as appropriate.
  - If no: document the unsupported behavior clearly (silent no-op, matching the
    "no hardware" desktop case), so callers get deterministic behavior either way.
- **Acceptance criteria:**
  - iOS behavior is deterministic and documented, whichever choice is made.
  - If a backend is added, it compiles behind the appropriate platform guard even
    without an Apple toolchain available to actually link it here.
  - Unsupported devices/platforms do not crash.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Detail/` (new, from `VIB-002`)
  - iOS build/toolchain files (none currently present — confirm before assuming a
    location)
  - `docs/devices-*.md`

### VIB-005 — Fix `IsSupported` semantics

- **Priority:** High
- **Area:** Vibration API
- **Problem:** `getIsSupportedProperty()` is a `NOXNA` extension. Confirmed current
  behavior (`VibrateController.cpp`): it opens (or reuses) a haptic device and reports
  supported if a device could be opened, without confirming
  `SDL_InitHapticRumble()`-style actual rumble capability — this exact tradeoff was
  already reviewed once in this codebase's history and left unchanged because
  `SDL_InitHapticRumble()` has a real, unverifiable-without-hardware side effect
  (uploads an effect onto the physical device). This task must re-examine that decision
  with fresh eyes, not just re-assert it.
- **Required work:**
  - Re-decide exact `NOXNA` semantics for `getIsSupportedProperty()` given the backend
    split introduced in `VIB-002`/`VIB-003` (a phone vibrator backend may have different,
    simpler support semantics than a generic SDL haptic device).
  - Ensure probing checks genuinely usable vibration capability for whichever backend is
    selected.
  - Avoid side effects such as leaving devices open or changing global haptic/backend
    state as a side effect of probing.
- **Acceptance criteria:**
  - `getIsSupportedProperty()` returns false when no usable vibration backend exists,
    for every backend in play after `VIB-002`/`VIB-003`.
  - Tests cover supported, unsupported, and backend-failure paths via a fake backend.
  - Any remaining known-imprecise case (e.g. "device opens but rumble capability itself
    is unconfirmed") is explicitly documented, not silently accepted.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-006 — Validate duration compatibility

- **Priority:** High
- **Area:** Vibration API
- **Problem:** `Start(TimeSpan)`'s duration boundaries must match XNA/Windows Phone
  behavior (this codebase currently validates `TimeSpan.Zero` to
  `TimeSpan.FromSeconds(5)` — confirm this range and the exact rejection behavior
  against an authoritative reference rather than assuming the current implementation is
  correct just because it exists).
- **Required work:**
  - Verify minimum and maximum duration behavior against XNA/WP7 documentation.
  - Add boundary tests for zero, negative, exactly 5 seconds, above 5 seconds, very
    large `TimeSpan` values, and repeated `Start()` calls.
  - Confirm whether `Start(TimeSpan.Zero)` should stop, no-op, or start a
    zero-duration vibration, and make the implementation match that decision exactly.
- **Acceptance criteria:**
  - Duration validation behavior is documented with its rationale.
  - Tests cover every boundary case listed above.
  - Behavior is consistent across every backend introduced by `VIB-002`/`VIB-003`.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-007 — Define repeated `Start`/`Stop` behavior

- **Priority:** High
- **Area:** Vibration API
- **Problem:** Repeated vibration calls must have fully deterministic, tested behavior
  across backend changes from `VIB-002`.
- **Required work:**
  - Verify behavior for `Start()` while already vibrating (does it restart the timer,
    ignore the new call, or something else?).
  - Verify `Stop()` before any `Start()`, repeated `Stop()`, and `Stop()` after a backend
    failure.
  - Add fake-backend tests for all of the above.
- **Acceptance criteria:**
  - Repeated calls behave consistently and are documented.
  - Tests do not require real hardware.
  - No backend resource leaks occur across repeated Start/Stop cycles (verify under
    `devices-asan`).
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-008 — Make left/right motor support explicitly `NOXNA`

- **Priority:** Medium
- **Area:** `NOXNA` Extension
- **Problem:** `StartLeftRight(float, float, TimeSpan)` is already marked `NOXNA` in the
  header — this task is to keep it that way through the `VIB-002`/`VIB-003` backend
  refactor and make sure its documentation is unambiguous, not to introduce the marker
  for the first time.
- **Required work:**
  - Keep `StartLeftRight` behind `NOXNA` through any backend changes.
  - Document that it is a CNA extension for dual-motor/gamepad-like haptic hardware, not
    XNA `VibrateController` API.
  - Ensure any future strict-XNA-surface check (from `DEV-API-002`) rejects it if that
    mechanism is built.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `StartLeftRight` as `NOXNA`.
  - Docs clearly state it is not XNA 4.0 API.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-009 — Add fake vibration backend tests

- **Priority:** Critical
- **Area:** Tests
- **Problem:** Current `VibrateControllerTests.cpp` exercises the real SDL3 haptic path
  directly; after `VIB-002` introduces a backend abstraction, tests should be able to
  inject a fake backend instead of depending on whatever haptic hardware (or lack of it)
  happens to be present in the test environment.
- **Required work:**
  - Add fake-backend injection (mirroring the `SetBackendForTesting()`-style pattern
    already used by `Compass`/`Motion` for their Android backends).
  - Test duration forwarding, stop forwarding, supported/unsupported probing, backend
    errors, and resource cleanup — all via the fake.
  - Ensure these tests run in CI (`DEV-BUILD-003`) without any hardware.
- **Acceptance criteria:**
  - All core `VibrateController` tests pass without real hardware present.
  - Hardware-dependent tests (if any remain) are separate and explicitly opt-in.
- **Suggested files to inspect or edit:**
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`
  - `src/Microsoft/Devices/Detail/` (new, from `VIB-002`)

### VIB-010 — Add manual hardware vibration checklist

- **Priority:** Medium
- **Area:** QA
- **Problem:** Phone vibration cannot be fully validated by unit tests alone; there is
  currently no dedicated manual checklist scoped specifically to vibration (the existing
  `docs/devices-hardware-checklist.md` covers sensors broadly and should be extended,
  not duplicated).
- **Required work:**
  - Add a manual checklist section covering: Android phone, iOS phone (if `VIB-004`
    adds support), desktop without haptics, desktop with a connected haptic device, and
    gamepad-connected desktop (confirming `VibrateController` and
    `GamePad::SetVibration()` do not fight over the same motor).
  - Record expected behavior for both strict XNA and `NOXNA` modes.
- **Acceptance criteria:**
  - `docs/devices-hardware-checklist.md` (extended, not duplicated) contains a
    vibration-specific validation matrix.
  - Each manual test row has device, OS, backend, action, expected result, and observed
    result columns.
- **Suggested files to inspect or edit:**
  - `docs/devices-hardware-checklist.md`
  - `examples/demo_devices/`
  - `plan_devices.md`

---

## 5. `SensorBase<T>` tasks

### SENSORBASE-001 — Implement real `TimeBetweenUpdates` semantics

- **Priority:** Critical
- **Area:** `SensorBase<T>`
- **Problem:** Confirmed (Section 1): `TimeBetweenUpdates` is public XNA API, stored and
  change-notified generically in `SensorBase<T>`, but the SDL backends
  (`Accelerometer`, `Gyroscope`) never read it back at all — zero references in either
  `.cpp` file. The Android backends only apply it once, at `Start()` time.
- **Required work:**
  - Define exact minimum, maximum, and default behavior for `TimeBetweenUpdates` (the
    current default, `TimeSpan.FromMilliseconds(2.0)`, is commented as matching ".NET
    source" — verify that comment against an authoritative reference rather than
    trusting it at face value).
  - Apply the value to every backend (SDL and Android).
  - Support changing the value while the sensor is already running, for every backend.
  - Add tests proving the callback rate is actually throttled, or the backend's own
    sample rate is actually updated — not just that the property getter/setter round-trips.
- **Acceptance criteria:**
  - `Accelerometer`, `Gyroscope`, `Compass`, and `Motion` all honor
    `TimeBetweenUpdates` in their actual event delivery rate.
  - Setting `TimeBetweenUpdates` while a sensor is started changes behavior without
    requiring `Stop()`/`Start()` or object recreation.
  - Tests cover invalid values (negative, zero if disallowed) and valid updates, for
    every sensor class.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp`

### SENSORBASE-002 — Verify default `TimeBetweenUpdates`

- **Priority:** High
- **Area:** `SensorBase<T>`
- **Problem:** The current default (`TimeSpan.FromMilliseconds(2.0)`, shared by all four
  sensor classes via `SensorBase<T>`'s constructor) may not match per-sensor-type XNA/WP7
  defaults — a single shared default is a simplifying assumption that has not been
  checked against a per-class authoritative default.
- **Required work:**
  - Verify the expected default for each of the four sensor types individually.
  - Decide whether one common default is acceptable, or whether per-class defaults are
    required for compatibility.
  - Add tests asserting whatever default is decided, per class.
- **Acceptance criteria:**
  - Default values are documented per sensor class, with rationale.
  - Tests assert the chosen defaults for each of the four classes.
  - Backend startup actually uses those defaults (ties into `SENSORBASE-001`).
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### SENSORBASE-003 — Fix event reentrancy and self-destruction safety

- **Priority:** Critical
- **Area:** Lifecycle / Events
- **Problem:** Event handlers may call `Stop()`, `Dispose()`, or otherwise trigger
  destruction of the sensor object while a callback dispatch is still executing on some
  thread. This exact class of bug has been found and fixed multiple times in this
  codebase's history for `Detail::AndroidSensorBridge`; this task is to re-audit the
  *current* state (after those fixes) for `SensorBase<T>`'s own dispatch path and the
  SDL subsystem, not to assume prior fixes fully closed every angle.
- **Required work:**
  - Audit all sensor dispatch methods (`SensorBase<T>`'s own `CurrentValueChanged`
    raising, `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()`, and each
    Android backend's callback path).
  - Confirm `this`/captured pointers are not touched after raising user callbacks unless
    lifetime is provably still valid (shared ownership, or an established documented
    boundary).
  - Add tests where event handlers call `Stop()`, `Dispose()`, and destroy the sensor
    object from inside `CurrentValueChanged`/`ReadingChanged`/`Calibrate`.
- **Acceptance criteria:**
  - No use-after-free occurs when event handlers stop/dispose sensors, for every
    documented-supported case.
  - `devices-asan`/`devices-tsan` runs are clean for these specific tests.
  - Any remaining unsupported case (e.g. destroying the owning object from within its
    own callback, on its own worker thread) is explicitly documented, matching this
    codebase's existing "accepted boundary" pattern rather than silently ignored.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`

### SENSORBASE-004 — Clarify thread-safety contract

- **Priority:** High
- **Area:** Lifecycle / API
- **Problem:** Real .NET instance members are generally not guaranteed thread-safe by
  the framework itself, but this codebase's `SensorBase<T>` and the SDL/Android backends
  use mutexes in several places — the exact boundary of what is and is not promised
  thread-safe has not been written down as a single explicit contract.
- **Required work:**
  - Define CNA's thread-safety promise for sensor instances as an explicit, written
    contract (e.g. "getters/setters are safe from any thread; concurrent `Start()`/
    `Stop()`/`Dispose()` from multiple threads has the following specific guarantees and
    the following specific gaps").
  - Audit for inconsistent locking across `Start()`/`Stop()`/`Dispose()`/property
    getters in all four sensor classes and `SensorBase<T>` itself.
  - Add tests for benign cross-thread `Stop()`/`Dispose()` during callbacks, scoped to
    whatever the contract actually promises.
- **Acceptance criteria:**
  - Thread-safety policy is written down in one place and cross-referenced from each
    class's own doc comments.
  - Code either enforces the policy or explicitly documents where it does not
    (rather than an implicit, undocumented gap).
  - `devices-tsan` runs report no data races in scenarios the contract claims to
    support.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `src/Microsoft/Devices/Sensors/*.cpp`
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp`

### SENSORBASE-005 — Verify `CurrentValue`/`IsDataValid` behavior

- **Priority:** High
- **Area:** `SensorBase<T>`
- **Problem:** `getCurrentValueProperty()`/`getIsDataValidProperty()` behavior before
  `Start()`, after `Stop()`, after a failed `Start()`, and when unsupported must be
  exact and identical across all four sensor classes, not merely "whatever each
  class's own code happens to do."
- **Required work:**
  - Verify expected XNA behavior for each of those states.
  - Add tests for each state, for each of the four sensor classes.
  - Ensure all derived sensors behave consistently unless a documented, intentional
    per-class difference exists.
- **Acceptance criteria:**
  - Tests cover no-data, valid-data, stopped, disposed, and unsupported states for
    `Accelerometer`, `Gyroscope`, `Compass`, and `Motion`.
  - All four sensors follow the same base contract unless a difference is explicitly
    documented and justified.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### SENSORBASE-006 — Verify `Dispose` semantics

- **Priority:** High
- **Area:** `SensorBase<T>`
- **Problem:** Double-`Dispose()` and `Dispose()`-while-started behavior must match
  .NET `IDisposable` expectations exactly, and must be identical across all four sensor
  classes.
- **Required work:**
  - Verify whether repeated `Dispose()` should throw or silently no-op (the .NET
    convention is typically "no-op," but confirm this is actually what's implemented
    and intended here, per class).
  - Ensure `Stop()` is called safely as part of `Dispose()` in every class.
  - Add tests for double-dispose, dispose-while-started, and dispose-then-any-public-call
    (expecting `ObjectDisposedException`, per the existing pattern).
- **Acceptance criteria:**
  - `Dispose()` behavior is documented and identical in spirit across all four classes.
  - Repeated-`Dispose()` behavior is a deliberate, tested choice, not an accident of
    implementation order.
  - No backend resources leak across a `Start()`→`Dispose()` cycle (verify under
    `devices-asan`).
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### SENSORBASE-007 — Audit protected/internal extension hooks

- **Priority:** Medium
- **Area:** API Design
- **Problem:** Hooks like `TimeBetweenUpdatesChanged` (a public `System::EventHandler`
  member on `SensorBase<T>`, confirmed present) may be useful CNA-internal plumbing but
  must not be confused with real XNA API surface.
- **Required work:**
  - Review all protected/public hooks on `SensorBase<T>` and each derived class's own
    `NOXNA`-tagged testing hooks (e.g. `InjectSyntheticSensorUpdate`,
    `SetStartedForTesting`, `SetSupportedForTesting`, all confirmed present on
    `Accelerometer`/`Gyroscope`).
  - Mark or re-confirm every non-XNA hook is documented as CNA-internal/testing-only.
- **Acceptance criteria:**
  - Extension hooks are not confused with XNA API anywhere in docs or the
    `DEV-API-001` matrix.
  - Docs clearly separate compatibility surface from CNA internals/testing hooks.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`

---

## 6. Accelerometer tasks

### ACCEL-001 — Verify XNA/WP public surface

- **Priority:** Critical
- **Area:** Accelerometer API
- **Problem:** `Accelerometer` has both the modern `CurrentValueChanged` event and the
  legacy `ReadingChanged` event (`AccelerometerReadingEventArgs`), confirmed present in
  the header with a doc comment describing the dual-event design. Whether both are
  correctly scoped to their respective XNA/WP7 API versions needs verification against
  an authoritative reference, not just against this codebase's own comments.
- **Required work:**
  - Verify which events/properties belong to which XNA 4.0/Windows Phone API version.
  - Mark any genuinely obsolete/legacy API clearly (beyond the existing doc-comment
    note).
  - Add or confirm compile-level tests for the expected public API shape.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Accelerometer` completely.
  - `ReadingChanged`'s exact compatibility status (kept for compatibility vs. CNA
    convenience) is documented with a rationale.
  - Tests cover both event paths given both are currently supported simultaneously.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-002 — Audit `ReadingChanged`-related comments for accuracy

- **Priority:** High
- **Area:** Documentation / Code Quality
- **Problem:** Comments describing `ReadingChanged`'s relationship to
  `CurrentValueChanged` and to the "destroying from within your own callback" lifetime
  boundary must exactly match actual dispatch order and behavior — verify this rather
  than assume the existing comment (already present, describing a specific reentrancy
  concern) is still accurate after any later change.
- **Required work:**
  - Re-read the current comments in `Accelerometer.hpp`/`.cpp` and
    `AccelerometerReadingEventArgs.hpp` against the actual dispatch code.
  - Fix any comment that no longer matches implementation.
  - Add tests verifying event raising order (`CurrentValueChanged` then
    `ReadingChanged`, or whatever order is actually implemented) and args content.
- **Acceptance criteria:**
  - No comment contradicts implementation.
  - Tests verify `CurrentValueChanged` and `ReadingChanged` firing order and content
    together.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-003 — Verify acceleration units

- **Priority:** Critical
- **Area:** Sensor Math
- **Problem:** SDL3 likely reports raw accelerometer data in m/s² on at least some
  platforms, while XNA's `AccelerometerReading.Acceleration` is documented in g units.
  This codebase's current conversion constant/logic must be re-verified against SDL3's
  actual per-platform behavior, not assumed correct because a conversion already exists.
- **Required work:**
  - Confirm SDL3's actual reported units on every target platform this project builds
    for (read `third_party/SDL`'s sensor backend source per-platform, not just the
    top-level SDL3 header docs).
  - Keep or adjust the conversion-by-standard-gravity constant accordingly.
  - Add tests for the conversion using known raw input values and expected g output.
- **Acceptance criteria:**
  - Known raw SDL values convert to the expected g values in tests.
  - The conversion (and its source, e.g. `StandardGravity = 9.80665f`, already used
    elsewhere in this codebase for the Android Motion backend) is documented with its
    origin.
  - Platform-specific differences, if any are found, are explicitly handled and tested.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-004 — Verify axis orientation on real hardware

- **Priority:** Critical
- **Area:** Sensor Math / Hardware QA
- **Problem:** Confirmed (Section 1 and this repository's own `NEXT.md`): Android
  orientation remapping (`Detail::AndroidSensorOrientation`) has never been verified
  against real hardware in any session.
- **Required work:**
  - Define the expected XNA axis convention for portrait and landscape orientations.
  - Test face-up, face-down, portrait-upright, portrait-upside-down, landscape-left, and
    landscape-right on real Android hardware.
  - Adjust the remapping code if real-device results disagree with current assumptions.
- **Acceptance criteria:**
  - A manual hardware checklist entry records expected vs. observed values per
    orientation, per device tested.
  - Automated tests cover the remapping math itself (already partially covered by
    `AndroidSensorOrientationTests.cpp` — confirm and extend, don't duplicate).
  - Code comments identify which specific devices/orientations have actually been
    verified.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`
  - `tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp`
  - `docs/devices-hardware-checklist.md`

### ACCEL-005 — Apply `TimeBetweenUpdates`

- **Priority:** Critical
- **Area:** Accelerometer Backend
- **Problem:** Confirmed (Section 1): `Accelerometer.cpp` never reads
  `getTimeBetweenUpdatesProperty()` at all — the requested update interval has no effect
  on the actual SDL-backed event rate today.
- **Required work:**
  - Apply the requested update interval to the SDL backend if SDL3 exposes a sensor
    polling-rate control; otherwise add software throttling in the dispatch path.
  - Ensure changing `TimeBetweenUpdates` while the sensor is running takes effect
    without requiring `Stop()`/`Start()`.
- **Acceptance criteria:**
  - Tests using a fake clock/backend prove throttling actually happens (avoid real-time
    sleeps in automated tests to prevent flakiness).
  - The manual demo visibly shows a lower event rate when the interval is increased.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-006 — Add fake accelerometer backend for tests

- **Priority:** High
- **Area:** Tests / Architecture
- **Problem:** Tests should not depend on real SDL sensor hardware being present in the
  CI/build environment. `Accelerometer` already has `NOXNA` testing hooks
  (`InjectSyntheticSensorUpdate`, `SetStartedForTesting`, `SetSupportedForTesting`,
  confirmed present) — this task is to verify those hooks are sufficient for full
  coverage, or extend them, not to assume no test seam exists yet.
- **Required work:**
  - Confirm existing testing hooks cover Start/Stop, event dispatch, unit conversion,
    state, exceptions, and throttling (from `ACCEL-005`); extend if any gap is found.
  - Keep the production public API clean of any test-only surface leaking into strict
    XNA mode (cross-check against `DEV-API-002`).
- **Acceptance criteria:**
  - Unit tests can simulate accelerometer samples end-to-end without SDL hardware.
  - CI (`DEV-BUILD-003`) does not require physical sensor hardware for
    `AccelerometerTests`.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-007 — Decide desktop support policy

- **Priority:** Medium
- **Area:** Platform Policy
- **Problem:** XNA/Windows Phone accelerometer semantics do not map cleanly onto
  arbitrary desktop/laptop SDL-exposed accelerometers (e.g. a 2-in-1 laptop's
  accelerometer, if SDL surfaces one).
- **Required work:**
  - Decide whether desktop accelerometer support should be strict-XNA no-op,
    `NOXNA`-flavored best-effort, or fully supported wherever SDL exposes hardware.
  - Document the decision.
  - Add tests for platform detection/behavior where feasible.
- **Acceptance criteria:**
  - Desktop behavior is deterministic and documented.
  - Docs explain the strict-XNA-vs-CNA-extension distinction for this specific
    platform case.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `docs/devices-*.md`

---

## 7. Gyroscope tasks

### GYRO-001 — Verify gyroscope public surface

- **Priority:** Critical
- **Area:** Gyroscope API
- **Problem:** `Gyroscope`'s public API must match XNA/WP7 expectations, and its
  `getStateProperty()` is already marked `NOXNA` (unlike `Accelerometer`'s, per Section
  1's confirmed finding) — this task is where that specific fact gets folded into the
  full matrix, cross-referenced with `DEV-API-003`.
- **Required work:**
  - Compare `Gyroscope.hpp` to the official XNA/WP7 API.
  - Verify `getIsSupportedProperty()`, inherited `CurrentValue`, `CurrentValueChanged`,
    `TimeBetweenUpdates`, `Start()`/`Stop()`/`Dispose()`.
  - Cross-check `getStateProperty()`'s `NOXNA` status against `DEV-API-003`'s decision.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Gyroscope` completely.
  - Non-XNA API is marked `NOXNA` consistently with the rest of the sensor classes.
  - Tests compile against the expected, decided signatures.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

### GYRO-002 — Verify gyroscope units

- **Priority:** Critical
- **Area:** Sensor Math
- **Problem:** XNA expects angular velocity in a specific, documented unit; SDL3's raw
  gyroscope unit per platform must be confirmed and converted if it differs, the same
  way `ACCEL-003` does for the accelerometer.
- **Required work:**
  - Verify the expected XNA unit for `GyroscopeReading.RotationRate`.
  - Verify SDL3's actual gyroscope unit per platform (read `third_party/SDL` backend
    source, not just top-level docs).
  - Add or adjust conversion, with tests using known raw values.
- **Acceptance criteria:**
  - Reading values use the documented, XNA-compatible unit.
  - Tests fail if the conversion is removed or changed incorrectly (i.e. the test
    actually pins a specific numeric expectation, not just "doesn't crash").
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

### GYRO-003 — Verify gyroscope axes and Android orientation remap

- **Priority:** Critical
- **Area:** Sensor Math / Hardware QA
- **Problem:** Gyroscope axis signs and Android landscape/portrait remapping are exactly
  as unverified-on-real-hardware as the accelerometer's (`ACCEL-004`) — this is a
  separate task because the two sensors' remap code, while sharing
  `Detail::AndroidSensorOrientation`, are applied independently in
  `Accelerometer.cpp`/`Gyroscope.cpp` and must each be checked.
- **Required work:**
  - Define expected axis behavior for rotation around each of the three axes.
  - Add a hardware checklist entry for X/Y/Z-axis rotations in portrait and landscape.
  - Adjust remap code if hardware results disagree with current assumptions.
- **Acceptance criteria:**
  - Manual hardware results are recorded per device tested.
  - Automated math tests cover the remapping logic itself.
  - Code comments state exactly which coordinate convention has been verified, and on
    what.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`
  - `tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp`
  - `docs/devices-hardware-checklist.md`

### GYRO-004 — Apply `TimeBetweenUpdates`

- **Priority:** Critical
- **Area:** Gyroscope Backend
- **Problem:** Confirmed (Section 1): `Gyroscope.cpp` never reads
  `getTimeBetweenUpdatesProperty()`, identical to the confirmed `Accelerometer` gap in
  `ACCEL-005`.
- **Required work:**
  - Apply the backend sample rate if SDL3 supports it; add software throttling
    otherwise.
  - Support changing the interval while the sensor is actively running.
- **Acceptance criteria:**
  - Fake-backend tests prove throttling with deterministic (non-sleep-based) timing.
  - The demo can visibly show a reduced update frequency when the interval increases.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

### GYRO-005 — Add fake gyroscope backend for tests

- **Priority:** High
- **Area:** Tests / Architecture
- **Problem:** CI should not require physical gyroscope hardware, matching
  `ACCEL-006`'s equivalent concern for `Accelerometer`.
- **Required work:**
  - Confirm/extend the existing `NOXNA` testing hooks
    (`InjectSyntheticSensorUpdate`/`SetStartedForTesting`/`SetSupportedForTesting`,
    already present on `Gyroscope`) to cover simulated samples, backend errors,
    unsupported state, and stop-during-callback.
- **Acceptance criteria:**
  - Unit tests cover `Gyroscope` fully without SDL hardware present.
  - The fake backend supports deterministic, test-controlled timestamps.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

---

## 8. Compass tasks

### COMPASS-001 — Verify Compass public API

- **Priority:** Critical
- **Area:** Compass API
- **Problem:** `Compass` has the inherited `SensorBase<T>` API plus a `Calibrate` event
  and compass-specific reading fields; its `getStateProperty()` is already `NOXNA`
  (confirmed Section 1) — verify the rest of the surface just as thoroughly.
- **Required work:**
  - Compare `Compass.hpp` to the official XNA/WP7 API.
  - Verify `getIsSupportedProperty()`, `Calibrate`, `CurrentValueChanged`,
    `TimeBetweenUpdates`, `Start()`/`Stop()`/`Dispose()`, and `SetBackendForTesting()`
    (confirmed `NOXNA`, correctly).
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Compass` completely.
  - All extra API is marked or documented.
  - Tests compile against expected signatures.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Compass.hpp`
  - `include/Microsoft/Devices/Sensors/CompassReading.hpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

### COMPASS-002 — Define correct `TrueHeading` behavior

- **Priority:** Critical
- **Area:** Compass Math / Compatibility
- **Problem:** Confirmed (Section 1): `AndroidCompassBackend::PublishReading()`
  deliberately sets `TrueHeading` equal to `MagneticHeading`, with an explicit comment
  explaining that real declination needs a location source this codebase doesn't have.
  Whether this specific fallback (as opposed to, say, `NaN`, `0`, or an actual
  declination calculation) is the XNA/WP7-compatible choice for "declination unknown"
  has not been verified against an authoritative reference.
- **Required work:**
  - Verify the expected XNA/WP7 behavior when true heading cannot be computed.
  - Decide, with rationale, whether to keep the current "equals magnetic heading"
    fallback, switch to a different sentinel, or implement real declination (see
    `COMPASS-003` for the latter).
  - Add tests for both the "heading unavailable" and (if implemented) "heading
    available" scenarios.
- **Acceptance criteria:**
  - `TrueHeading` fallback behavior is explicitly documented as either
    verified-compatible or intentionally-chosen-CNA-behavior, not left as an unexamined
    assumption.
  - Tests cover both scenarios explicitly.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

### COMPASS-003 — Add optional declination/location support plan

- **Priority:** Medium
- **Area:** Compass Accuracy
- **Problem:** Real true heading requires magnetic declination, which in turn requires
  location and date — this repository has an explicit, existing decision (documented in
  `docs/location-future-plan.md`, referenced elsewhere in this codebase) to keep
  location/GPS out of `Microsoft::Devices::Sensors` entirely. This task's job is to
  confirm that decision is still the right call for `Compass` specifically, not to
  silently re-open or silently re-confirm it without checking.
- **Required work:**
  - Re-read `docs/location-future-plan.md` and confirm its reasoning still applies to
    `Compass::TrueHeading` specifically.
  - Decide whether CNA should ever implement true-heading calculation, and if so, plan
    how a location/declination dependency could be added without polluting the strict
    XNA `Compass` surface (e.g. as an optional, separately-injected `NOXNA` dependency).
  - If the answer remains "no," document that explicitly as this task's outcome.
- **Acceptance criteria:**
  - The plan states clearly, with current reasoning, whether true heading is or is not
    planned to be supported.
  - No fake/approximated true heading is ever reported without a clearly documented
    rationale for the approximation.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `docs/location-future-plan.md`
  - `plan_devices.md`

### COMPASS-004 — Verify Android heading math on hardware

- **Priority:** Critical
- **Area:** Compass Math / Hardware QA
- **Problem:** `Detail::AndroidCompassMath`'s rotation-vector-to-heading conversion is
  currently only unit-tested against self-consistency properties (identity quaternion →
  0°, monotonic response to a known yaw) — confirmed by its own doc comment stating it
  has "never been checked against real hardware." Physical validation is still
  outstanding.
- **Required work:**
  - Test known real-world orientations against a real compass reference (e.g. a phone
    compass app, or a known magnetic-north reference) on real Android hardware.
  - Validate north/east/south/west headings, and both portrait and landscape device
    orientation.
  - Adjust the sign/axis convention in `Detail::AndroidCompassMath` if hardware results
    disagree.
- **Acceptance criteria:**
  - Hardware results are recorded (device, OS version, orientation, expected vs.
    observed heading).
  - Math tests reflect verified-correct behavior, not merely "whatever the current
    implementation happens to output."
  - Code comments describe the confirmed coordinate convention, replacing the current
    "never checked" caveat once real verification has occurred.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidCompassMathTests.cpp`
  - `docs/devices-hardware-checklist.md`

### COMPASS-005 — Revisit the rotation-vector-plus-magnetometer support requirement

- **Priority:** High
- **Area:** Platform Policy
- **Problem:** Confirmed (Section 1): `AndroidCompassBackend::IsSupported()` requires
  both a rotation-vector sensor and a magnetic-field sensor. Devices with only a
  magnetometer (no fused rotation vector) are reported unsupported today, even though a
  magnetometer-only heading (with reduced accuracy) might be preferable to reporting
  "not supported" at all.
- **Required work:**
  - Verify the minimum sensor set genuinely needed for an XNA-compatible compass
    reading.
  - Consider a magnetometer-only fallback path if technically feasible, with clearly
    documented accuracy tradeoffs (e.g. worse tilt compensation without a fused
    rotation vector).
  - Document whichever policy is chosen.
- **Acceptance criteria:**
  - `getIsSupportedProperty()`'s exact policy is explicit and justified.
  - Tests cover: rotation vector missing, magnetometer missing, and both available.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

### COMPASS-006 — Verify accuracy mapping and `Calibrate` event policy

- **Priority:** High
- **Area:** Compass Events
- **Problem:** Android magnetic-field sensor accuracy statuses
  (`Detail::AndroidSensorAccuracyStatus`) are mapped to XNA `HeadingAccuracy` degree
  values, and to whether `Calibrate` fires, by a CNA-chosen policy (already documented
  in this codebase as a deliberate choice, e.g. "`Low` deliberately excluded" from
  triggering `Calibrate`) — this task re-verifies that specific chosen mapping is still
  the right one, not that a mapping exists at all.
- **Required work:**
  - Verify the accuracy-status-to-degrees mapping against any available reference (WP7
    docs, or a reasoned default if none exists).
  - Confirm the current `Calibrate`-firing policy (unreliable/no-contact fire it,
    low/medium/high do not) doesn't cause event spam in practice.
  - Add or extend tests for the mapping and for `Calibrate` firing conditions.
- **Acceptance criteria:**
  - Accuracy mapping is documented and tested for every
    `AndroidSensorAccuracyStatus` value.
  - `Calibrate` fires only under the intended, tested conditions.
  - `CalibrationEventArgs` content is verified correct.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `include/Microsoft/Devices/Sensors/CalibrationEventArgs.hpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidCompassMathTests.cpp`

### COMPASS-007 — Add iOS compass backend plan or implementation

- **Priority:** High
- **Area:** iOS Backend
- **Problem:** `Compass` has no iOS-native backend at all today (confirmed: only
  `Detail::AndroidCompassBackend` exists as a concrete `ICompassBackend`
  implementation).
- **Required work:**
  - Decide whether to support iOS compass in this API.
  - If yes: plan or implement using `CLLocationManager`'s heading APIs
    (`CoreLocation`).
  - If no: document the unsupported behavior clearly (permanent stub, matching every
    non-Android platform's current behavior).
- **Acceptance criteria:**
  - iOS build behavior is deterministic, whichever choice is made.
  - An unsupported backend returns "not supported" cleanly rather than crashing.
  - A manual iOS checklist exists if support is added.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp`
  - iOS build/toolchain files (none currently present — confirm before assuming a
    location)
  - `docs/devices-*.md`

### COMPASS-008 — Harden Android compass callback lifetime further

- **Priority:** Critical
- **Area:** Lifecycle / Android
- **Problem:** `Detail::AndroidCompassBackend`'s callbacks run on
  `Detail::AndroidSensorBridge`'s own worker threads and capture `this`. A prior
  hardening pass already addressed several use-after-free/reentrancy concerns in
  `Detail::AndroidSensorBridge` itself (shared-ownership `Impl`, mutex-guarded
  Start/Stop state) — this task re-verifies that hardening is sufficient specifically
  for `AndroidCompassBackend`'s own callback closures
  (`HandleRotationVectorSample`/`HandleMagneticFieldSample`/`PublishReading`), not just
  the shared bridge.
- **Required work:**
  - Re-confirm `Compass`/`AndroidCompassBackend`'s own object lifetime story under a
    `Stop()`/`Dispose()`-from-within-`Calibrate`-or-`CurrentValueChanged` scenario.
  - Add tests using a fake backend that destroys the `Compass` object from inside a
    callback, to the extent this is a supported scenario (document if it is not).
- **Acceptance criteria:**
  - `devices-asan`/`devices-tsan` report no lifetime or race issues for documented-
    supported scenarios.
  - `Stop()`/`Dispose()` during a callback is either verified safe and tested, or
    explicitly documented as an unsupported boundary (matching the existing accepted
    boundary pattern for `Detail::AndroidSensorBridge`).
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

---

## 9. Motion tasks

### MOTION-001 — Verify Motion public API

- **Priority:** Critical
- **Area:** Motion API
- **Problem:** `Motion` is the most complex class in this scope (`MotionReading` nests
  an `AttitudeReading`) and must expose exactly the intended XNA/WP7 API plus clearly
  marked extensions. Its `getStateProperty()` is already `NOXNA` (confirmed Section 1).
- **Required work:**
  - Compare `Motion.hpp`, `MotionReading.hpp`, and `AttitudeReading.hpp` to the expected
    API.
  - Verify `Calibrate`, `getIsSupportedProperty()`, `CurrentValueChanged`,
    `Start()`/`Stop()`/`Dispose()`, and every reading property.
  - Verify whether any currently-exposed property is a non-XNA addition that needs
    `NOXNA` marking.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Motion` completely.
  - Tests compile against expected signatures.
  - Any extra API is marked `NOXNA` or removed.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Motion.hpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-002 — Verify quaternion and attitude coordinate mapping

- **Priority:** Critical
- **Area:** Motion Math
- **Problem:** `Detail::AndroidMotionMath`'s Android-rotation-vector-to-XNA-attitude
  mapping is exactly as likely to have sign/order/orientation issues as the compass
  heading math (`COMPASS-004`), and is documented in this repository's own history as
  an explicitly open, unresolved question — not yet contradicted or confirmed by real
  hardware testing.
- **Required work:**
  - Define the XNA-expected quaternion, yaw/pitch/roll, and rotation-matrix conventions
    precisely.
  - Validate with independent golden data (hand-computed expected quaternions/matrices
    for known rotations).
  - Validate on real Android hardware in multiple physical orientations.
  - Adjust the conversion in `Detail::AndroidMotionMath`/`Detail::AndroidMotionBackend`
    if needed.
- **Acceptance criteria:**
  - Tests cover identity, yaw 90°/180°/270°, pitch, roll, and combined rotations.
  - Hardware validation results match the automated tests' expectations (or the tests
    are updated to match reality, with the discrepancy documented).
  - Code comments explain the coordinate conversion precisely.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidMotionMathTests.cpp`
  - `docs/devices-hardware-checklist.md`

### MOTION-003 — Verify gravity and device acceleration units

- **Priority:** Critical
- **Area:** Motion Math
- **Problem:** XNA `MotionReading.Gravity`/`.DeviceAcceleration` are documented in
  gravitational units (g), not raw platform units. This codebase already applies a
  `StandardGravity = 9.80665f` divisor in `Detail::AndroidMotionBackend.cpp` (a prior
  fix for a real bug found in this area) — this task re-verifies that conversion is
  still correct and complete, not that a conversion needs to be added from scratch.
- **Required work:**
  - Re-verify the current gravity/linear-acceleration conversion against
    `ACCEL-003`'s accelerometer findings (both should agree on the platform's raw
    unit).
  - Add tests for the m/s²-to-g conversion with known values.
  - Verify the platform source units haven't changed across any NDK/SDL upgrade since
    the conversion was last checked.
- **Acceptance criteria:**
  - Gravity at rest has magnitude near 1g in tests.
  - Linear acceleration excludes the gravity component (matches `TYPE_LINEAR_ACCELERATION`
    semantics, not `TYPE_ACCELEROMETER`).
  - Tests cover the conversion and sign convention explicitly.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-004 — Verify rotation rate units

- **Priority:** High
- **Area:** Motion Math
- **Problem:** `MotionReading.DeviceRotationRate` units must match XNA expectations;
  Android's `TYPE_GYROSCOPE` reports radians/second, which this codebase currently
  passes through unconverted for `Motion` (distinct from the accelerometer/gravity
  conversion) — confirm this pass-through is actually correct rather than an oversight.
- **Required work:**
  - Verify whether XNA expects radians/second or degrees/second for this specific
    property.
  - Verify Android's actual gyroscope unit (`ASENSOR_TYPE_GYROSCOPE`, NDK docs).
  - Convert if the two units don't already match; add tests either way.
- **Acceptance criteria:**
  - Rotation-rate units are documented explicitly.
  - Tests use known sample values with a pinned expected output.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-005 — Define Motion support policy

- **Priority:** High
- **Area:** Platform Policy
- **Problem:** Confirmed: `AndroidMotionBackend::IsSupported()` requires an attitude
  source (rotation vector or game-rotation-vector fallback — already implemented) plus
  gravity, linear-acceleration, and gyroscope sensors, all simultaneously. Whether this
  all-or-nothing requirement is the right compatibility tradeoff, versus a partial-data
  fallback, needs an explicit decision.
- **Required work:**
  - Verify the minimum sensor set genuinely required for an XNA-compatible `Motion`
    reading.
  - Decide fallback behavior when some (but not all) required sensors are missing.
  - Document the tradeoff between full XNA-shape compatibility (every field populated)
    and broader device support (partial data, clearly marked as such).
- **Acceptance criteria:**
  - `getIsSupportedProperty()` behavior is deterministic and documented.
  - Tests cover every missing-sensor combination (attitude missing, gravity missing,
    linear-acceleration missing, gyroscope missing, and combinations).
  - Docs explain why `Motion` is supported or unsupported on a given device.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-006 — Fix timestamp policy

- **Priority:** High
- **Area:** Motion Reading Semantics
- **Problem:** `MotionReading` fuses four independent Android sensor streams (attitude,
  gravity, linear acceleration, gyroscope), each with its own sample timestamp; the
  fused reading's own timestamp meaning (and `AttitudeReading`'s nested timestamp) must
  be defined precisely, not left as "whatever happened to be convenient when the code
  was written."
- **Required work:**
  - Define the timestamp meaning for `MotionReading` and nested `AttitudeReading`
    explicitly (e.g. "wall-clock time of the fused reading's publication" vs. "the
    attitude sample's own sensor timestamp").
  - Prefer platform event timestamps where they're compatible with the chosen
    `System::DateTimeOffset`-based representation; otherwise document the wall-clock
    substitution explicitly (mirroring the existing, already-documented rationale for
    `Detail::AndroidSensorSample::Timestamp` being wall-clock, not
    `ASensorEvent::timestamp`).
  - Ensure the fused reading's timestamp is internally consistent (not contradicted by
    its own nested `AttitudeReading`'s timestamp).
- **Acceptance criteria:**
  - Timestamp policy is documented for both `MotionReading` and `AttitudeReading`.
  - Tests verify monotonic timestamp progression across successive readings.
  - Fused readings never contain two different timestamps that claim to represent "now"
    inconsistently.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-007 — Prevent stale sample fusion

- **Priority:** High
- **Area:** Motion Fusion
- **Problem:** `AndroidMotionBackend::PublishReading()` currently publishes as soon as
  all four sources have delivered at least one sample ever (`hasAttitudeSample_` etc.,
  confirmed present) — it does not check whether those four most-recent samples were
  taken close together in time. A fast-changing gyroscope value could be fused with a
  stale gravity sample from much earlier.
- **Required work:**
  - Track a per-source last-sample timestamp.
  - Define a maximum acceptable age window across the four sources for them to be
    considered a valid fused reading.
  - Decide behavior when sources are outside that window (drop the stale one and wait,
    or publish anyway with a documented caveat) and implement that decision.
- **Acceptance criteria:**
  - Fused readings only combine samples that are fresh relative to each other, per the
    defined window.
  - Tests simulate stale gravity, stale acceleration, stale gyroscope, and stale
    attitude samples independently.
  - The chosen behavior (drop/wait vs. publish-with-caveat) is documented.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-008 — Apply `TimeBetweenUpdates`

- **Priority:** Critical
- **Area:** Motion Backend
- **Problem:** `Motion`'s update rate must honor `TimeBetweenUpdates`, including changes
  made while the sensor is running — the same class of gap already confirmed for
  `Compass`/`AndroidCompassBackend`'s Android event-rate handling (only applied at
  `Start()` time today), but `Motion` drives five separate
  `Detail::AndroidSensorBridge` instances simultaneously (rotation vector,
  game-rotation-vector fallback, gravity, linear acceleration, gyroscope), so all five
  must be kept in sync.
- **Required work:**
  - Apply the requested interval to all five underlying Android sensor queues, or add
    software throttling at the fused-reading publish step.
  - Ensure interval changes while running take effect across all five sources
    consistently.
  - Add fake-backend tests.
- **Acceptance criteria:**
  - `Motion`'s published event rate follows the requested `TimeBetweenUpdates`.
  - Tests cover interval changes made during active streaming.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-009 — Add iOS Motion backend plan or implementation

- **Priority:** High
- **Area:** iOS Backend
- **Problem:** `Motion` is a natural fit for iOS's `CoreMotion` (`CMDeviceMotion`), but
  no iOS backend exists today (confirmed: only `Detail::AndroidMotionBackend` exists as
  a concrete `IMotionBackend`).
- **Required work:**
  - Plan (or implement, once an Apple toolchain is available) a `CMDeviceMotion`-backed
    `IMotionBackend`.
  - Map `attitude`, `gravity`, `userAcceleration`, and `rotationRate` to
    `AttitudeReading`/`MotionReading` fields, following the same unit-verification
    discipline as `MOTION-003`/`MOTION-004`.
  - Add an iOS manual test checklist entry.
- **Acceptance criteria:**
  - iOS behavior is documented, whichever choice is made.
  - The backend compiles behind the appropriate platform guard, or the unsupported path
    is deterministic if not implemented.
  - A manual checklist covers major orientations and movement patterns.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp`
  - iOS build/toolchain files (none currently present — confirm before assuming a
    location)
  - `docs/devices-*.md`

### MOTION-010 — Harden Motion callback lifetime further

- **Priority:** Critical
- **Area:** Lifecycle / Android
- **Problem:** Same class of concern as `COMPASS-008`, but for `Motion`/
  `AndroidMotionBackend`'s five independent bridge callbacks
  (`HandleAttitudeSample`/`HandleGravitySample`/`HandleLinearAccelerationSample`/
  `HandleGyroscopeSample`/`PublishReading`), which is more surface area than `Compass`'s
  two.
- **Required work:**
  - Re-confirm `Motion`/`AndroidMotionBackend`'s object lifetime story under
    `Stop()`/`Dispose()`-from-within-`CurrentValueChanged` for each of the five
    callback paths.
  - Add tests for `Stop()`/`Dispose()`/destroy-from-within-callback using a fake
    backend, to the extent this is a supported scenario (document if not).
  - Audit all five callbacks' shared-state mutations
    (`attitude_`/`gravity_`/`deviceAcceleration_`/`deviceRotationRate_`, all
    mutex-guarded per existing code) for races introduced by concurrent delivery from
    multiple bridge worker threads.
- **Acceptance criteria:**
  - `devices-asan`/`devices-tsan` runs are clean for documented-supported scenarios.
  - No callback path touches a destroyed `Motion`/`AndroidMotionBackend`.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

---

## 10. Android sensor bridge tasks

### ANDROID-BRIDGE-001 — Verify per-sensor sample value counts

- **Priority:** High
- **Area:** Android Bridge
- **Problem:** `Detail::AndroidSensorSample::ValueCount` must reflect the real
  per-sensor-type value count (rotation vector up to 5, magnetic field/gravity/linear
  acceleration/gyroscope 3, etc.) — verify the current bridge implementation actually
  sets this correctly per sensor type, rather than a single generic constant.
- **Required work:**
  - Confirm `ValueCount` is set according to actual sensor type at every callsite in
    `Detail::AndroidSensorBridge.cpp`.
  - Ensure backends validate they've received enough values before reading indices
    (defensive bounds-checking against a short/malformed event).
  - Add tests for each consumed sensor type's expected value count.
- **Acceptance criteria:**
  - Rotation vector, magnetic field, gravity, linear acceleration, and gyroscope
    samples all expose the correct count.
  - Backends handle an incomplete sample safely (no out-of-bounds read).
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp`

### ANDROID-BRIDGE-002 — Support update-interval changes while running

- **Priority:** Critical
- **Area:** Android Bridge
- **Problem:** Confirmed (Section 1): `Detail::AndroidSensorBridge::Start()` converts
  `timeBetweenUpdates` to `ASensorEventQueue_setEventRate()`'s microsecond parameter
  only once, at `Start()` time — there is no code path to change an already-running
  bridge's sample rate today. This is the root cause underlying `ACCEL-005`,
  `GYRO-004`, `COMPASS`'s and `MOTION-008`'s `TimeBetweenUpdates` gaps for the Android
  side specifically.
- **Required work:**
  - Add an API to `Detail::AndroidSensorBridge` (e.g. `SetSampleInterval(TimeSpan)`)
    that calls `ASensorEventQueue_setEventRate()` again on the live queue, from the
    correct thread.
  - Wire `Compass`/`Motion`'s `TimeBetweenUpdates` setter through to every active
    underlying bridge.
  - Add tests, using whatever host-testable seam is available (the real NDK path can't
    run in this development container — see Section 14).
- **Acceptance criteria:**
  - Active Android-backed sensors update their sampling delay without requiring
    `Stop()`/`Start()` or object recreation.
  - Tests cover the new delay-update code path to the extent host-testable.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`

### ANDROID-BRIDGE-003 — Improve Start/Stop lifecycle guarantees further

- **Priority:** High
- **Area:** Android Bridge
- **Problem:** `Detail::AndroidSensorBridge::Start()`/`Stop()` have already been through
  several hardening passes in this codebase's history (a startup handshake, a
  mutex-guarded state machine, shared-ownership `Impl`, a stale-looper-reset RAII
  guard) — this task re-audits the *current* state for any remaining gap, rather than
  assuming those passes closed every case. One specific, already-documented remaining
  gap: two or more distinct external (non-worker) threads calling `Stop()`
  concurrently on the same bridge is still unserialized and can race on `join()`.
- **Required work:**
  - Add tests for: `Start()` failure (queue creation/enable failure), `Stop()` before
    `Start()`, repeated `Stop()`, `Stop()` called reentrantly from the worker's own
    callback, and destructor cleanup.
  - Decide whether the still-open "two concurrent external `Stop()` callers" gap should
    finally be closed (it was previously left as a documented, deliberate boundary to
    avoid a different deadlock risk) or remains an accepted limitation — re-examine
    with fresh eyes rather than re-stating the old conclusion unchecked.
  - Prefer safe, deterministic cleanup over detach-and-abandon wherever a safe
    alternative can be found without reintroducing the previously-identified deadlock
    risk.
- **Acceptance criteria:**
  - No leaked event queues or worker threads across repeated Start/Stop cycles.
  - `Stop()` behavior is deterministic for every documented-supported calling pattern.
  - Tests cover every lifecycle edge case listed above.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp`

### ANDROID-BRIDGE-004 — Add Android API-level documentation

- **Priority:** Medium
- **Area:** Android Platform
- **Problem:** Android sensor and vibrator behavior depends on API level and runtime
  permissions (e.g. Android 12+/API 31+ restricting high-frequency sensor sampling
  without the `HIGH_SAMPLING_RATE_SENSORS` permission — already noted in this
  codebase's own doc comments in one place, but not centralized).
- **Required work:**
  - Consolidate documentation of minimum Android API level, sampling-rate
    restrictions, and vibrator permission requirements into `docs/devices-android.md`
    (already exists — extend it, don't create a duplicate).
  - Document which physical/emulated devices have actually been tested.
- **Acceptance criteria:**
  - `docs/devices-android.md` lists API levels, permissions, and known limitations for
    both sensors and vibration.
  - The demo app's Android manifest matches the documented required permissions.
- **Suggested files to inspect or edit:**
  - `docs/devices-android.md`
  - `examples/demo_devices/android/`

---

## 11. SDL sensor subsystem tasks

### SDL-SENSOR-001 — Verify SDL3 sensor units and axes

- **Priority:** Critical
- **Area:** SDL Backend
- **Problem:** CNA cannot assume SDL3's reported units and axis conventions without
  reading SDL3's own backend source per platform — this underlies `ACCEL-003`'s and
  `GYRO-002`'s unit-verification tasks, but is listed here as its own cross-cutting task
  because `Detail::SdlSensorSubsystem<TSensor>` is shared plumbing, not
  per-sensor-class code.
- **Required work:**
  - Check SDL3 documentation and `third_party/SDL`'s actual per-platform sensor backend
    source for both accelerometer and gyroscope units.
  - Confirm axis conventions match what `Detail::AndroidSensorOrientation` and the
    per-class conversion code assume.
  - Add code comments and tests recording exactly what was checked and where.
- **Acceptance criteria:**
  - SDL conversion code is backed by a specific documented source (SDL3 docs section,
    or a specific file/line in `third_party/SDL`), not an unstated assumption.
  - Tests cover the expected raw-to-XNA mapping.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `third_party/SDL/src/sensor/` (read-only research — vendored, do not edit)

### SDL-SENSOR-002 — Implement update-rate throttling

- **Priority:** Critical
- **Area:** SDL Backend
- **Problem:** Confirmed (Section 1): SDL sensor callbacks currently ignore
  `TimeBetweenUpdates` entirely — this is the SDL-side counterpart to
  `ANDROID-BRIDGE-002`.
- **Required work:**
  - Use SDL3's own sensor update-rate APIs if they exist for the sensor types in use;
    otherwise add software throttling in
    `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()` (or equivalent
    dispatch point).
  - Ensure throttling is scoped per sensor *instance*, not global — two
    `Accelerometer` instances with different `TimeBetweenUpdates` values must behave
    independently.
- **Acceptance criteria:**
  - `Accelerometer` and `Gyroscope` event rates follow their own instance's
    `TimeBetweenUpdates`.
  - Tests use fake/injected timestamps to avoid real-time-based test flakiness.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

### SDL-SENSOR-003 — Strengthen SDL event lifetime tests

- **Priority:** High
- **Area:** Lifecycle / Tests
- **Problem:** SDL event watches and callbacks are a classic source of
  use-after-free/reentrancy bugs; `SensorSubsystemOwnershipTests.cpp` already exists and
  covers some of this — this task confirms it covers the *current* dispatch
  implementation fully, rather than assuming existing coverage is complete.
- **Required work:**
  - Add tests for `Stop()`/`Dispose()`/destruction occurring during event dispatch.
  - Confirm no sensor object is touched after a user callback returns unless lifetime
    is provably still valid.
  - Run under `devices-asan`/`devices-tsan`.
- **Acceptance criteria:**
  - Lifecycle tests pass under both sanitizers.
  - Any remaining known-unsupported case is explicitly documented rather than silently
    left as an untested gap.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `tests/Microsoft/Devices/Sensors/SensorSubsystemOwnershipTests.cpp`

---

## 12. Reading structs and event-args tasks

### READINGS-001 — Verify all reading struct fields

- **Priority:** Critical
- **Area:** Reading Structs
- **Problem:** Every reading struct must expose the correct fields, types, and units —
  this is the concrete, per-field companion to `DEV-API-004`'s behavioral audit.
- **Required work:**
  - Audit `AccelerometerReading`, `GyroscopeReading`, `CompassReading`, `MotionReading`,
    and `AttitudeReading` field-by-field.
  - Verify field names, types, units (cross-reference `ACCEL-003`/`GYRO-002`/
    `MOTION-003`/`MOTION-004`'s unit findings), and mutability (`DEF_MEMBER`-style
    getter/setter pairs, confirmed pattern in `CompassReading.hpp`).
  - Add tests for construction and every property getter/setter.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix includes every reading struct's fields.
  - Tests cover every property on every reading struct.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/AccelerometerReading.hpp`
  - `include/Microsoft/Devices/Sensors/GyroscopeReading.hpp`
  - `include/Microsoft/Devices/Sensors/CompassReading.hpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `src/Microsoft/Devices/Sensors/AccelerometerReading.cpp`
  - `src/Microsoft/Devices/Sensors/GyroscopeReading.cpp`
  - `src/Microsoft/Devices/Sensors/CompassReading.cpp`
  - `src/Microsoft/Devices/Sensors/MotionReading.cpp`
  - `src/Microsoft/Devices/Sensors/AttitudeReading.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/AttitudeReadingTests.cpp`

### READINGS-002 — Verify event-args types

- **Priority:** High
- **Area:** Events
- **Problem:** Event-args classes must carry the correct reading type and be shaped
  correctly for their consumers.
- **Required work:**
  - Audit `SensorReadingEventArgs<T>`, `AccelerometerReadingEventArgs`, and
    `CalibrationEventArgs`.
  - Verify property names and inheritance against expected XNA/WP7 shape.
  - Add or extend tests.
- **Acceptance criteria:**
  - Event-args classes match the intended XNA/WP7 API.
  - Tests cover construction, property retrieval, and actual use in event dispatch
    (not just standalone construction).
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp`
  - `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`
  - `include/Microsoft/Devices/Sensors/CalibrationEventArgs.hpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerReadingEventArgsTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CalibrationEventArgsTests.cpp`

### READINGS-003 — Verify timestamp source consistently

- **Priority:** High
- **Area:** Reading Semantics
- **Problem:** Some readings use wall-clock timestamps
  (`System::DateTimeOffset::getUtcNowProperty()`, confirmed used in
  `Detail::AndroidSensorBridge.cpp`) rather than platform sensor timestamps
  (`ASensorEvent::timestamp`, a monotonic boot-time value) — this is already a
  documented, deliberate choice in one place; this task confirms the policy is applied
  consistently across every sensor class, not just where it happened to be documented.
- **Required work:**
  - Write down one timestamp policy that applies to all four sensor classes'
    readings.
  - Use monotonic/platform timestamps only where genuinely compatible with
    `System::DateTimeOffset`'s semantics; otherwise document wall-clock use explicitly,
    consistent with the existing rationale already present for
    `Detail::AndroidSensorSample::Timestamp`.
  - Ensure timestamps are stable and testable (i.e. tests can inject a fixed clock
    rather than relying on real elapsed time).
- **Acceptance criteria:**
  - Timestamp policy is documented once and referenced from every sensor class's own
    docs.
  - Tests cover monotonic progression and correct propagation from backend sample to
    public event.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

---

## 13. Demo and manual QA tasks

### DEMO-001 — Make `demo_devices` show all sensor states

- **Priority:** Medium
- **Area:** Demo
- **Problem:** Manual QA needs visible support status, started/stopped state, data
  validity, latest reading, and current `TimeBetweenUpdates` for every sensor, plus
  vibration controls — verify what `DevicesDemo.cpp` currently shows before assuming it
  is missing something specific.
- **Required work:**
  - Update the demo's UI/logging to show every sensor's support status, started/stopped
    state, data validity, and latest reading.
  - Add interactive controls for `TimeBetweenUpdates` (ties directly into verifying
    `SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/`MOTION-008` on real hardware).
  - Add vibration controls covering `Start(TimeSpan)`, the `NOXNA` intensity/left-right
    variants, and `getIsSupportedProperty()`/`getDeviceNameProperty()` display.
- **Acceptance criteria:**
  - The demo can be used standalone for manual Android/iOS/desktop testing without
    needing to read source code alongside it.
  - The demo logs enough data to write a useful bug report from its output alone.
- **Suggested files to inspect or edit:**
  - `examples/demo_devices/src/DevicesDemo.cpp`
  - `examples/demo_devices/src/DevicesDemo.hpp`
  - `examples/demo_devices/src/Main.cpp`
  - `examples/demo_devices/android/`

### DEMO-002 — Add a hardware QA report template

- **Priority:** Medium
- **Area:** QA
- **Problem:** Sensor and vibration correctness requires physical-device validation
  that only a human can perform; `docs/devices-hardware-checklist.md` already exists as
  a checklist of *what* to verify, but there is no standard template for *recording*
  results in a reusable, comparable format across devices/sessions.
- **Required work:**
  - Create a markdown report template (as a new file,
    `docs/devices_sensor_hardware_qa_template.md`, distinct from the existing
    checklist — cross-link the two rather than merging them, since one is "what to
    check" and the other is "how to record a specific run's results").
  - Include device model, OS version, orientation, expected values, observed values,
    backend, and commit hash fields.
  - Include sections for accelerometer, gyroscope, compass, motion, and vibration.
- **Acceptance criteria:**
  - The template exists under `docs/`.
  - `plan_devices.md` links to it from every hardware-verification task above.
  - Manual tests can be recorded consistently, session over session.
- **Suggested files to inspect or edit:**
  - `docs/devices_sensor_hardware_qa_template.md` (new)
  - `docs/devices-hardware-checklist.md` (cross-link, do not duplicate)
  - `plan_devices.md`

---

## 14. Final verification tasks

### VERIFY-001 — Run the full Devices/Sensors test suite

- **Priority:** Critical
- **Area:** Verification
- **Problem:** No task above is considered complete until the tests for it were
  actually built and actually run — this plan must never claim a passing result that
  wasn't observed directly.
- **Required work:**
  - Build and run every test file under `tests/Microsoft/Devices/` and
    `tests/Microsoft/Devices/Sensors/` (including `Detail/`).
  - Record the exact commands used and their exact results (pass/fail counts, not just
    "it worked").
  - Fix failures, or create a new follow-up task with the failure's exact log excerpt
    if the fix is out of scope for the change being made.
- **Acceptance criteria:**
  - `CnaTests` passes with the Devices/Sensors filter from `DEV-BUILD-002`.
  - Results are recorded (in this plan, `NEXT.md`, or a linked verification log) with
    the exact command and exact pass/fail/skip counts observed.
- **Suggested commands** (verify these preset names still exist in `CMakePresets.json`
  before relying on them — confirmed present as of 2026-07-05):
  ```bash
  cmake --preset devices-ubsan
  cmake --build --preset devices-ubsan --target CnaTests
  ./cmake-build-devices-ubsan/CnaTests --gtest_filter='*Devices*:*Sensors*:*Vibrate*:*Accelerometer*:*Gyroscope*:*Compass*:*Motion*:*Android*:*ScopeExit*:*SensorBase*:*SensorFailed*:*SensorSubsystemOwnership*'
  ```

### VERIFY-002 — Run sanitizer verification

- **Priority:** High
- **Area:** Verification
- **Problem:** Sensor callbacks and backend worker threads specifically need sanitizer
  coverage — this is the area of this codebase where prior real bugs (use-after-free,
  thread-termination races, undefined-behavior integer casts) have actually been found
  before, not a generic precaution.
- **Required work:**
  - Run `devices-asan`, `devices-tsan`, and `devices-ubsan` configurations.
  - Prioritize lifecycle/callback tests (`SENSORBASE-003`, `COMPASS-008`,
    `MOTION-010`, `ANDROID-BRIDGE-003`, `SDL-SENSOR-003`) when interpreting results.
  - Record any failure as a new follow-up task with the sanitizer's exact report
    excerpt, not just "sanitizer found something."
- **Acceptance criteria:**
  - Sanitizer logs are clean, or every remaining finding is a knowingly-tracked,
    already-documented, out-of-scope issue (e.g. a `sharp-runtime` race, not a
    `Microsoft::Devices` one) — verify that classification is still accurate rather than
    assuming an old classification still applies.
  - Callback/destruction tests are included in every sanitizer run.
- **Suggested files to inspect or edit:**
  - `CMakePresets.json`
  - `tests/Microsoft/Devices/`
  - `tests/Microsoft/Devices/Sensors/`

### VERIFY-003 — Run a strict XNA API compile check

- **Priority:** High
- **Area:** Verification
- **Problem:** CNA extensions must not leak into strict XNA API surface — this closes
  the loop on `DEV-API-002`'s enforcement task with an actual executable check, not just
  a documented policy.
- **Required work:**
  - Add or run a compile-only test/check for a "strict XNA surface" mode, if the
    project's build system can express one; if it cannot yet, this task includes adding
    that capability (even minimally, e.g. a dedicated test translation unit that must
    fail to compile if it references a `NOXNA` member while some macro is defined).
  - Ensure `NOXNA` APIs are unavailable (or at least clearly flagged) when strict mode
    is enabled.
  - Ensure every genuine XNA API remains available and unaffected by strict mode.
- **Acceptance criteria:**
  - The strict-mode check passes.
  - A deliberately-introduced "leaked extension" (e.g. temporarily removing a `NOXNA`
    marker) is caught by this check, proving it actually works rather than trivially
    passing.
- **Suggested files to inspect or edit:**
  - `tests/Microsoft/Devices/`
  - `tests/Microsoft/Devices/Sensors/`
  - `CMakeLists.txt` / relevant build config files

---

## 15. Definition of done for this plan

The Devices/Sensors work driven by this plan is not done until:

- The public API matrix (`DEV-API-001`) is complete and covers every class in Section 0.
- `VibrateController`'s strict XNA behavior is separated from CNA haptic extensions
  (`VIB-002`), and the class is referred to correctly as `VibrateController` everywhere
  (`VIB-001`).
- Android phone vibration works through a proper phone-vibrator backend, or is
  explicitly and deliberately re-confirmed to be adequately served by SDL3's existing
  Android haptic backend (`VIB-003`) — not left as an unexamined assumption either way.
- `TimeBetweenUpdates` actually works for every sensor (`Accelerometer`, `Gyroscope`,
  `Compass`, `Motion`) and can be changed while the sensor is running
  (`SENSORBASE-001`, `ACCEL-005`, `GYRO-004`, `ANDROID-BRIDGE-002`, `MOTION-008`,
  `SDL-SENSOR-002`) — confirmed fixed, not just planned, given Section 1's concrete
  finding that this is currently broken for every SDL-backed sensor.
- Accelerometer and Gyroscope units and axes are verified against real hardware
  (`ACCEL-003`, `ACCEL-004`, `GYRO-002`, `GYRO-003`), not merely unit-tested against
  the current implementation's own output.
- Compass heading math, accuracy mapping, calibration policy, and true-heading policy
  are verified (`COMPASS-002` through `COMPASS-006`).
- Motion's quaternion/attitude mapping, gravity/acceleration/rotation-rate units,
  timestamp policy, and stale-sample-fusion behavior are verified (`MOTION-002` through
  `MOTION-007`).
- Android callback lifetime issues are fixed or explicitly documented with tests, for
  both `Compass` (`COMPASS-008`) and `Motion` (`MOTION-010`), and the shared bridge's
  own remaining known gap (`ANDROID-BRIDGE-003`) is either closed or re-confirmed as a
  deliberate, documented boundary.
- Desktop, Android, and iOS support policies are documented for every sensor and for
  vibration (`ACCEL-007`, `COMPASS-005`, `COMPASS-007`, `MOTION-005`, `MOTION-009`,
  `VIB-004`).
- CI runs hardware-free unit tests for this entire area (`DEV-BUILD-003`).
- A manual hardware QA checklist and report template exist, and have at least one real
  Android device result recorded against them (`ACCEL-004`, `GYRO-003`, `COMPASS-004`,
  `MOTION-002`, `DEMO-002`).
- Strict XNA builds do not expose CNA-only `NOXNA` APIs, verified by an actual
  executable check, not just documentation (`DEV-API-002`, `VERIFY-003`).
- All relevant tests pass in normal and sanitizer configurations where supported
  (`VERIFY-001`, `VERIFY-002`), with results actually observed and recorded, not
  assumed.

This plan is not implemented as of 2026-07-05. No task above has been started.
