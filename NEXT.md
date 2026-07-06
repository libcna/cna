# NEXT.md — CNA Project Handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3 with
a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It preserves XNA-style
public APIs (`Microsoft::Xna::Framework`, `Microsoft::Devices`) while using modern C++
internally. Targets desktop Linux/Windows/macOS and Android; iOS is planned but has no
toolchain in this environment. Branch: `feature/devices`.

**Main goal on this branch:** bring `Microsoft::Devices`, `Microsoft::Devices::Sensors`,
and `VibrateController` to verified XNA 4.0 / Windows Phone 7 compatibility — not just
"compiles and doesn't crash," but a documented, tested public API surface with a clear
strict-XNA-vs-`NOXNA` boundary.

**Current development phase: `plan_devices.md` is effectively done.** All 74 tasks are
CLOSED except three, all intentionally left open because they need something this
container cannot provide (real hardware or a maintainer decision) — see Section 4.
Tasks were picked up one at a time, each with its own build+test(+sanitizer)
verification and its own commit.

**Important architectural decisions:**
- Public API names/signatures match XNA 4.0 (or, for `Microsoft::Devices`, the archived
  WP7 SDK docs — FNA has no equivalent) exactly; C# properties become
  `getXProperty()`/`setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration, and this is now
  **compile-time enforced** — see Section 3 (`VERIFY-003`/`DEV-API-002`).
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the shared base
  for `Accelerometer`/`Compass`/`Gyroscope`/`Motion`.
- `VibrateController` (correct XNA name — not "VibrationController") is a singleton
  (`getDefaultProperty()`), lives directly in `Microsoft::Devices` (not `::Sensors`),
  does not derive `SensorBase<T>`/`IDisposable`. Calls through a
  `Detail::IVibrateBackend` abstraction; the only production implementation is
  `Detail::SdlHapticVibrateBackend`, swappable via `SetBackendForTesting()`.
- `Compass`/`Motion` each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`. Every other
  platform keeps the original permanent-stub behavior.
- Tests live under `tests/` mirroring the `include`/`src` namespace path 1:1, Google Test.

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly under `EASYGL` (`cmake-build-debug`), and
the **full default build** (every target) is clean, 100%. Android cross-compile of the
`CNA` library target (`arm64-v8a`, NDK r30, API 24) is clean.

**Tests:** the Devices-only exact-suite-name filter is **343 tests, 341 passed, 2
expected `GTEST_SKIP()`s** (no accelerometer/gyroscope hardware in this container).
Clean on plain `cmake-build-debug` and all three sanitizer presets (`VERIFY-001`/
`VERIFY-002`). Plus a new standalone compile-check executable/ctest,
`cna_strict_xna_api_check` (`VERIFY-003`) — not a gtest suite, run separately.

**Sanitizers:** `devices-asan` — 0 issues. `devices-ubsan` — 3 pre-existing findings, all
in `Vector3`/`Matrix` (signed integer overflow), 0 in any `Microsoft::Devices` file.
`devices-tsan` — 37 reports, all the same single pre-existing `sharp-runtime`
`TimeSpan::copy_count` race (now tracked as its own task, `SDL-SENSOR-004`, rather than
left as a repeated "same pre-existing race" aside with no follow-up).

**Working:** `Accelerometer`/`Gyroscope` — real, SDL3-backed. `Compass`/`Motion` — real
on Android only (`Detail::AndroidCompassBackend`/`AndroidMotionBackend`, pure NDK, no
JNI), permanent stub on every other platform. `VibrateController` — real, SDL3
haptic-backed. `examples/demo_devices` (`cna_demo_devices`) — built clean, and confirmed
to run for 6-8+ seconds under `xvfb-run` without crashing (this session added
`TimeBetweenUpdates` interactive controls and completed its title-bar diagnostic
output); no real display was available in this container to visually confirm on-screen
rendering, stated honestly rather than assumed.

**Does not work / not implemented (by design, not bugs):** iOS backend for anything in
this scope. `Compass.TrueHeading` permanently equals `MagneticHeading` (no declination
source — see `docs/location-future-plan.md`). No physical Android/iOS hardware has ever
been used to verify anything in this scope, in any session — see
`docs/devices_sensor_hardware_qa_template.md` for how to record a real hardware session
when one becomes available.

**Working tree:** clean, up to date with local commits on `feature/devices` (not yet
pushed — push only if the user asks).

---

## 3. Recent changes (this session, 2026-07-06)

Most recent first. Full detail (citations, exact test names, sanitizer output) is in
`plan_devices.md`'s per-task closing notes and git commit messages — this section is a
factual summary, not the full log.

- **`VERIFY-003`/`DEV-API-002` (both closed):** built a real strict-XNA-API compile
  check from scratch — `NOXNA` (`CNAHelper.hpp`) now expands to `[[deprecated]]` under a
  new `CNA_STRICT_XNA_API` macro; a new `cna_strict_xna_api_check` CMake target
  (`tools/devices/StrictXnaApiSurfaceCheck.cpp`, `-Werror=deprecated-declarations`)
  fails to build the instant it references any `NOXNA` member. Caught a real bug
  immediately: `SensorBase<T>::setTimeBetweenUpdatesProperty()` (genuine XNA API)
  internally touched the `NOXNA` `TimeBetweenUpdatesChanged` event — fixed with a
  targeted `#pragma` suppression at that one internal call site, not by changing the
  public API. Verified both ways (clean build/run today; a deliberately-reintroduced
  leak reproduces the exact expected build failure).
- **`VERIFY-001`/`VERIFY-002` (closed):** full 21-suite/343-test filter run and recorded
  on plain build and all three sanitizer presets — zero failures, zero new findings.
- **`DEMO-002` (closed):** new `docs/devices_sensor_hardware_qa_template.md`, one
  section per `docs/devices-hardware-checklist.md` section number; linked from every
  task in the plan whose own status says hardware verification is still outstanding.
- **`DEMO-001` (closed):** added `TimeBetweenUpdates` interactive controls (Numpad +/-)
  to `examples/demo_devices`; closed a real gap where the title bar (the demo's one
  text-output channel) never showed Compass/Motion's `IsDataValid` or most of their
  reading fields.
- **`READINGS-001`/`READINGS-003` (closed):** re-verified all five reading structs'
  fields against archived MSDN pages (closed one citation gap — `GyroscopeReading` was
  only ever cited "by pattern," now cited directly, `hh220755`). Confirmed one
  consistent wall-clock timestamp policy across all four sensor classes; found and
  closed a real gap where `CompassTests.cpp`/`MotionTests.cpp` had zero tests asserting
  on `Timestamp` at all.
- **`SDL-SENSOR-001`/`SDL-SENSOR-003` (closed), `SDL-SENSOR-004` (new, OPEN,
  cross-repo):** cited SDL3's documented axis convention directly in
  `Accelerometer.cpp`/`Gyroscope.cpp`; confirmed existing lifecycle test coverage is
  sufficient under both sanitizers. The one sanitizer finding (TSan, `sharp-runtime`'s
  `TimeSpan::copy_count`) was given its own permanent follow-up task for the first time.
- **`ANDROID-BRIDGE-001`/`003`/`004` (closed):** fixed a real bug where `ValueCount` was
  hardcoded to 16 for every Android sensor type; fixed a real concurrent-`Stop()`
  double-`join()` data race; consolidated Android API-level/permission docs and added
  the missing `HIGH_SAMPLING_RATE_SENSORS` manifest permission.
- **Earlier in this session (`ACCEL-*`/`GYRO-*`/`COMPASS-*`/`MOTION-*`, all closed):**
  numerous real bugs found and fixed by code review — a stray debug log tag, a wrong
  manifest-attribute claim, a wrong citation (`ff707930`→`ff707531`), a `Compass`
  accuracy-threshold inconsistency (45°→20°), a real use-after-free ordering bug in
  `AndroidCompassBackend`, a `MotionReading.Timestamp`/`Attitude.Timestamp` divergence,
  and a missing stale-sample-fusion guard in `AndroidMotionBackend` (now a 500ms max-age
  window). Two new open architectural questions were found and deliberately left
  unresolved rather than guessed at — see Section 4.

---

## 4. Current blocker / main problem

**No code-level blocker exists.** `plan_devices.md` has no more actionable tasks this
container can complete. Three tasks remain OPEN, all requiring something this
container genuinely cannot provide:

- **`ACCEL-008`** — an archived MSDN Magazine article states the real WP7
  `Accelerometer`'s raw coordinate system never changes between portrait and landscape
  mode — potentially contradicting this codebase's own Android landscape-remap step.
  Single-source finding, not corroborated, and a breaking change to already-tested
  behavior if acted on without evidence. **Needs either a second authoritative source
  or real hardware** (see `docs/devices_sensor_hardware_qa_template.md` Section 1).
- **`COMPASS-009`** — the real WP7 `Compass` documents a tilt-dependent axis switch (real
  sample code exists) that `Detail::AndroidCompassMath` has no equivalent for. Needs a
  careful Android-specific quaternion derivation with no hardware to check the result
  against. **Needs real hardware** (see the QA template's Section 7) before implementing,
  or a maintainer decision to implement blind and flag as unverified.
- **`SDL-SENSOR-004`** — `sharp-runtime`'s `TimeSpan::copy_count`/`move_count` are
  non-atomic statics that race under concurrent `TimeSpan` copy/move construction
  (confirmed via `devices-tsan`). Lives in the sibling `sharp-runtime` repo, out of this
  repo's scope to fix directly — **needs a `sharp-runtime` session** (make the counters
  `std::atomic`, or gate them out of release builds).

If picking up this branch with no further `Microsoft::Devices` work available, the next
reasonable step is either (a) one of the three items above, if the missing input
(hardware, a second source, or a `sharp-runtime` session) has become available, or (b)
ask the user what to work on next — there is no more open plan work to default to.

---

## 5. Known bugs and limitations

- **By design, not a bug:** `Compass.TrueHeading` always equals `MagneticHeading` — real
  declination needs a location source, out of scope for `Microsoft::Devices::Sensors`
  (see `docs/location-future-plan.md`). `Motion.Calibrate` is never raised by any
  backend.
- **Deliberate, documented limitation:** concurrent `Dispose()` calls on the *same*
  sensor instance from two threads block the losing caller until the winner's cleanup
  finishes, then return as a silent no-op — matches the conventional .NET `IDisposable`
  contract.
- **Deliberate, unfixed by design:** destroying `Accelerometer`/`Compass`/`Motion` from
  within their own callback in certain specific ways is an accepted, documented,
  unsupported boundary — see `COMPASS-008`/`MOTION-010`'s closing notes in
  `plan_devices.md` for the exact shape of what remains unprotected and why.
- **Needs physical hardware verification (never done, for anything, ever):** every
  axis-sign question, `VibrateController` actually actuating a real motor, `Compass`
  heading accuracy/calibration, `Motion` attitude tracking, and `ACCEL-008`/`COMPASS-009`
  above. See `docs/devices-hardware-checklist.md` (what to check) and
  `docs/devices_sensor_hardware_qa_template.md` (how to record results) — the latter is
  new this session.
- **Needs verification, likely permanent:** iOS cross-compilation — no Apple toolchain
  in this Linux container.
- **Out of scope, not this repo's bug:** `sharp-runtime`'s `TimeSpan` copy/move-counter
  TSan race (`SDL-SENSOR-004`) — sibling-repo concern, do not fix from here.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/          ← XNA WP7 sensor API headers
include/Microsoft/Devices/Sensors/Detail/   ← internal-only, never in public headers
src/Microsoft/Devices/Sensors/              ← sensor implementations (SDL3-backed + Android-native)
tests/Microsoft/Devices/Sensors/            ← Google Test suites per class
tests/Microsoft/Devices/Sensors/Detail/     ← Android bridge/math pure-function tests
include/Microsoft/Devices/                  ← VibrateController.hpp
src/Microsoft/Devices/                      ← VibrateController.cpp
tools/devices/                              ← StrictXnaApiSurfaceCheck.cpp (VERIFY-003, standalone, not gtest)
examples/demo_devices/                      ← DevicesDemo (cna_demo_devices target)
examples/demo_devices/android/              ← Android Gradle/CMake app project
docs/devices-hardware-checklist.md          ← manual real-hardware verification steps (what to check)
docs/devices_sensor_hardware_qa_template.md ← manual real-hardware QA report template (how to record results)
docs/devices-build.md                       ← reproducible build/test commands
docs/devices-native-backend-design.md       ← Compass/Motion native backend design
docs/devices-api-coverage.md                ← per-member API coverage table + timestamp policy
docs/devices-thread-safety.md               ← consolidated thread-safety contract
docs/devices-android.md                     ← consolidated Android-specific reference
docs/location-future-plan.md                ← why GPS/location isn't here
plan_devices.md                             ← the plan (2026-07-05); effectively done, 3 tasks OPEN
```

- **`SensorBase<T>`** owns `CurrentValue`, `IsDataValid`, `TimeBetweenUpdates`,
  `CurrentValueChanged`, `Dispose()`. Every field is mutex-guarded; getters return by
  value. **Do not restructure without a concrete, newly-found bug** — stable across many
  hardening passes.
- **`NOXNA` is now compile-time enforced** (`VERIFY-003`): `CNAHelper.hpp`'s `NOXNA`
  macro expands to `[[deprecated]]` when `CNA_STRICT_XNA_API` is defined; the
  `cna_strict_xna_api_check` CMake target builds `tools/devices/StrictXnaApiSurfaceCheck.cpp`
  with that macro plus `-Werror=deprecated-declarations`. Adding a new public
  `Microsoft::Devices`/`Sensors` extension without tagging it `NOXNA` will not be caught
  by this check unless that specific member is also referenced from the check file —
  it proves known-real members stay real and known-`NOXNA` members are truly gated, not
  an exhaustive every-future-member guarantee.
- **`Accelerometer`/`Gyroscope`** share `Detail::SdlSensorSubsystem<TSensor>`.
- **`Compass`/`Motion`** each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`. Both interfaces
  compile and are mockable on every platform. `SetBackendForTesting()` (`NOXNA`, both
  classes) lets tests inject a fake backend.
- **`VibrateController`:** SDL3 haptic-backed only; no native Android bridge (confirmed
  unnecessary — SDL3's own Android haptic backend already reaches `Context.VIBRATOR_SERVICE`
  directly, see `docs/devices-android.md`).
- **Boundaries — do not cross:**
  - `third_party/SDL` is vendored with its own `CLAUDE.md` forbidding AI-authored
    contributions — read-only for research.
  - `sharp-runtime` is a sibling repo under separate development — do not fix its bugs
    (the `TimeSpan` copy/move-counter race, `SDL-SENSOR-004`) by editing files there.
  - Do not expand `Microsoft::Devices` scope to camera, radio, or
    phone-call/photo-picker APIs.
  - Do not fake `Compass`/`Motion` data from other sensors, and do not add GPS/location
    to `Microsoft::Devices::Sensors` under any circumstances (including as `NOXNA`) —
    see `docs/location-future-plan.md`.
  - The XNA 4.0 class name is `VibrateController`, not "VibrationController."

---

## 7. Useful commands

**ZIP-export caveat:** every command below assumes a real `git clone` with submodules
initialized (`git submodule update --init` — non-recursive) plus the two sibling repos
this project depends on (`sharp-runtime`, `easy-gl`, which itself needs `meta-gl`)
checked out next to this one.

```bash
# Configure (only if CMakeCache.txt is stale/missing):
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build:
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Devices-only filter (exact suite names, 21 suites / 343 cases):
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerFailedExceptionTests.*:AccelerometerReadingEventArgsTests.*:AccelerometerReadingTests.*:AccelerometerTests.*:AndroidSensorOrientationTests.*:AttitudeReadingTests.*:CalibrationEventArgsTests.*:CompassReadingTests.*:CompassTests.*:AndroidCompassMathTests.*:AndroidMotionMathTests.*:AndroidSensorBridgeTests.*:GyroscopeReadingTests.*:GyroscopeTests.*:MotionReadingTests.*:MotionTests.*:ScopeExitTests.*:SensorBaseTests.*:SensorFailedExceptionTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*"

# Strict XNA API surface check (VERIFY-003):
cmake --build cmake-build-debug --target cna_strict_xna_api_check -j$(nproc)
./cmake-build-debug/cna_strict_xna_api_check

# Build and run the Devices demo (needs a real display; xvfb-run works for a crash-check
# but cannot visually confirm on-screen rendering):
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices

# Android cross-compile check (NDK at ~/Android/Sdk/ndk/) — CNA library only, known clean:
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Sanitizer builds:
cmake --preset devices-asan && cmake --build --preset devices-asan --target CnaTests
cmake --preset devices-tsan && cmake --build --preset devices-tsan --target CnaTests
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan --target CnaTests
```

No dedicated lint/format tooling is configured for this project. No CI infrastructure
exists in this repo (`DEV-BUILD-003` — CI job spec was written, `.github/workflows/devices-tests.yml`,
but never run on a real CI provider from this container).

---

## 8. Next smallest tasks

`plan_devices.md` has no remaining actionable tasks for this container. If resuming
without new user direction:

1. Check whether real Android/iOS hardware, a physical vibration motor, or a second
   authoritative WP7 API source has become available — if so, that directly unblocks
   `ACCEL-008`/`COMPASS-009` (see Section 4).
2. Check whether a `sharp-runtime` session is available/appropriate — that unblocks
   `SDL-SENSOR-004`.
3. Otherwise, ask the user what to work on next. Do not invent new `plan_devices.md`
   tasks unprompted, and do not start work in a different part of the codebase
   (`Microsoft::Xna::Framework`, networking, etc.) without being asked — this plan's
   scope is `Microsoft::Devices`/`Microsoft::Devices::Sensors`/`VibrateController` only.

---

## 9. Do not do yet

- Do not restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem<TSensor>`, or
  `Detail::AndroidSensorBridge`'s locking scheme without a concrete, newly-found bug —
  all have been through multiple hardening passes already.
- Do not fake `Compass`/`Motion` data from other sensors, and do not add GPS/location to
  `Microsoft::Devices::Sensors` under any circumstances (including as `NOXNA`) — see
  `docs/location-future-plan.md`.
- Do not implement `ACCEL-008` or `COMPASS-009` without either real hardware or an
  explicit maintainer decision to proceed without it — both would be breaking changes
  to already-tested behavior based on single-source or no evidence.
- Do not claim Android/iOS physical-hardware verification, or a real-device run, unless
  it was actually done in the current session.
- Do not edit anything under `third_party/SDL` — vendored, has its own `CLAUDE.md`
  forbidding AI-authored contributions.
- Do not fix bugs found in `sharp-runtime` (e.g. `SDL-SENSOR-004`) by editing files
  there directly from a `cna_devices`-scoped session.
- Do not trust a single passing `ctest` run as proof a new concurrency/lifetime change
  is correct — loop it (20-60+ iterations) and/or run it under a sanitizer preset first.
- Do not open yet another new plan file — `plan_devices.md` is current, use it.
- Do not rename or refer to `VibrateController` as "VibrationController" anywhere.
- Do not push to the remote unless the user explicitly asks.

---

## 10. Resume prompt

```
Read NEXT.md first. plan_devices.md (74 tasks) is effectively DONE — all tasks CLOSED
except three (ACCEL-008, COMPASS-009, SDL-SENSOR-004), all intentionally left open
because they need real hardware, a second source, or a sharp-runtime-repo session (see
NEXT.md Section 4 for exactly what each needs). Do not implement any of the three
without that missing input arriving, or an explicit maintainer decision.

There is no more default `Microsoft::Devices` plan work to pick up. If the user hasn't
given new direction, check Section 8, then ask what to work on next rather than
inventing new scope.

If new hardware/source/sharp-runtime access does become available, use
docs/devices_sensor_hardware_qa_template.md to record real hardware results, and
plan_devices.md's per-task Resolution notes for full historical context before touching
any of the three remaining open items.
```
