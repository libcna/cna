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

**Current development phase:** working through `plan_devices.md`, a 72-task audit/
implementation plan rewritten from scratch on 2026-07-05. **21 of 72 tasks are closed,
1 is in progress, 50 are open.** Tasks are picked up one at a time, each with its own
build+test+sanitizer verification and its own commit.

**Important architectural decisions:**
- Public API names/signatures match XNA 4.0 (or, for `Microsoft::Devices`, the archived
  WP7 SDK docs — FNA has no equivalent) exactly; C# properties become
  `getXProperty()`/`setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration. This session found
  and fixed 3 real cases where a genuine extension had been left unmarked — see Section
  3. Whether a check now *enforces* this automatically is still open (`DEV-API-002`).
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the shared base
  for `Accelerometer`/`Compass`/`Gyroscope`/`Motion` — see Section 6.
- `VibrateController` (correct XNA name — not "VibrationController") is a singleton
  (`getDefaultProperty()`), lives directly in `Microsoft::Devices` (not `::Sensors`),
  does not derive `SensorBase<T>`/`IDisposable`. Its only backend is SDL3 `SDL_Haptic`.
- `Compass`/`Motion` each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`. Every other
  platform keeps the original permanent-stub behavior.
- Tests live under `tests/` mirroring the `include`/`src` namespace path 1:1, Google Test.

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly under `EASYGL` (`cmake-build-debug`).
Android cross-compile of the `CNA` library target (`arm64-v8a`, NDK r30, API 24) is
clean, confirmed via `llvm-nm` symbol inspection. A full, untargeted Android build of
*every* target still fails on `cna_demo_input` (see Section 4) — not a gate blocker,
just unfixed. iOS: no toolchain in this container.

**Tests:** the Devices-only filter (Section 7) is **313 tests, 311 passed, 2 expected
`GTEST_SKIP()`s** (no accelerometer/gyroscope hardware in this container). Last run on
plain `cmake-build-debug` and `devices-asan`/`devices-ubsan`, all clean.

**Sanitizers:** `devices-asan` — 0 issues. `devices-ubsan` — 3 pre-existing findings, all
in `Vector3::GetHashCode()`/`Matrix::GetHashCode()` (signed integer overflow), 0 in any
`Microsoft::Devices` file. `devices-tsan` — last run reported the same single
pre-existing, out-of-scope race in `sharp-runtime`'s
`System::TimeSpan::TimeSpan(const TimeSpan&)` copy constructor; not re-run this pass
(pure header/annotation changes only, no concurrency-relevant code touched).

**Working:** `Accelerometer`/`Gyroscope` — real, SDL3-backed. `Compass`/`Motion` — real
on Android only (`Detail::AndroidCompassBackend`/`AndroidMotionBackend`, pure NDK, no
JNI), permanent stub on every other platform. `VibrateController` — real, SDL3
haptic-backed. `examples/demo_devices` (`cna_demo_devices`) built and ran as a real
Android APK once, in an earlier session (historical result, not currently reproducible
here — see Section 4).

**Does not work / not implemented:** iOS backend for anything in this scope (`Compass`,
`Motion`, or vibration). `Compass.TrueHeading` permanently equals `MagneticHeading` (no
declination source). `Motion`'s coordinate-remap question is unresolved
(`MOTION-002`). No compile-time or test-time check yet enforces "every `NOXNA`-worthy
extension is actually tagged" (`DEV-API-002`, in progress). No physical Android/iOS
hardware has ever been used to verify anything in this scope, in any session.

**Working tree:** clean, up to date with `origin/feature/devices`, HEAD at `61ef35c0`.

---

## 3. Recent changes

Most recent first. Full detail (citations, exact test names, sanitizer output) is in
git history/commit messages — this section is a factual summary, not the full log.

- **`DEV-API-002` (IN PROGRESS, not closed):** found and fixed a third real
  "Extra-unmarked" bug — `AccelerometerReadingEventArgs`'s `operator==`/`operator!=`/
  `ToString()`/`GetHashCode()` had no `NOXNA` tag (real base is `System.Object` per its
  own archived MSDN page `ff707998`; no such members exist in the real API). Fixed
  identically to the `DEV-API-004` pattern. Re-confirmed clean: `VibrateController.hpp`,
  `SensorReadingEventArgs.hpp`, `CalibrationEventArgs.hpp`. **Left open deliberately** at
  the user's request to stop and report status — the acceptance criterion "a test fails
  when a future extension is left unmarked" has no such check yet (compile-time or
  test-time), and `SensorFailedException`/`AccelerometerFailedException`'s constructor
  signatures were not re-verified against MSDN. Verified: 313/313 tests, ASan/UBSan 0
  issues. Commit `61ef35c0`.
- **`DEV-API-004` (closed):** found a systemic Extra-unmarked bug across **all five**
  reading structs (`AccelerometerReading`/`GyroscopeReading`/`CompassReading`/
  `MotionReading`/`AttitudeReading`) — their `operator==`/`operator!=`/`ToString()`/
  `GetHashCode()` are CNA-only conveniences (the real WP7 API inherits these unmodified
  from `System.ValueType`, whose `ToString()` returns only the type name) but had no
  `NOXNA` tag, and `docs/devices-api-coverage.md` incorrectly listed them as `Real`.
  Fixed: tagged `NOXNA` on all four members × all five structs, corrected the coverage
  doc. Commit `823150c7`.
- **`DEV-API-005` (closed, doc-only):** verified the `AccelerometerFailedException` vs.
  shared `SensorFailedException` split against direct MSDN citations — already correct,
  no code change. Commit `6bd20c67`.
- **`SENSORBASE-007` (closed):** `SensorBase<T>::TimeBetweenUpdatesChanged` had no
  `NOXNA` tag and its doc comment made an unsourced claim; the real API's own MSDN page
  lists only `CurrentValueChanged`. Fixed the tag and doc comment, corrected the
  coverage doc. **All of `SENSORBASE-001`–`008` are now closed.**
- **`SENSORBASE-006` (closed):** verified `Dispose()` semantics are consistent across
  all four sensor classes; found and closed two real test gaps (nothing had actually
  exercised `Dispose(bool)`'s `wasStarted`-true branch, or asserted the fake backend's
  `Stop()` was actually called by `Dispose()`).
- **`SENSORBASE-005` (closed):** verified `CurrentValue`/`IsDataValid` behavior is
  identical across all four classes (both getters are defined once, on `SensorBase<T>`
  itself, never overridden); closed two real coverage gaps (behavior after `Stop()`,
  behavior after `Dispose()`).
- **`SENSORBASE-004` (closed):** found and fixed a **real data race** in `Compass`/
  `Motion` — unlike `Accelerometer`/`Gyroscope`, their `Start()`/`Stop()`/
  `getStateProperty()` had no locking on `state_`/`started_`. Confirmed via a new test
  run under `devices-tsan` before the fix (race reported) and after (clean). Fixed by
  adding a per-instance mutex to both classes. Wrote `docs/devices-thread-safety.md`,
  the consolidated thread-safety contract for all four sensor classes.
- **`SENSORBASE-001`–`003`, `DEV-API-001`/`003`, `ANDROID-BRIDGE-002`, `READINGS-002`,
  `DEV-BUILD-001`/`002`/`004` (all closed, earlier in this session):** `TimeBetweenUpdates`
  now actually throttles event rate for all four sensor classes (SDL-backed via a new
  software throttle in `SensorBase<T>`, Android-backed via
  `AndroidSensorBridge::SetSampleInterval()`); the Devices-only `ctest` filter was
  corrected to match exact suite names (21 suites, 313 cases now, was missing
  `CalibrationEventArgsTests` and picking up 2 false positives before); a genuinely
  fresh clone was verified and two real bootstrap gaps fixed (undocumented sibling-repo
  dependency on `sharp-runtime`/`easy-gl`/`meta-gl`, and harmful `--recursive` submodule
  guidance); `cna_demo_devices`'s Android build gap was root-caused and fixed (SDL3
  headers weren't reaching the demo target, and a plain `add_executable()` is not a
  valid Android app format for this demo — the real Android app lives under
  `examples/demo_devices/android/`).

Full task-by-task detail, including exact MSDN citations, exact test names, and exact
sanitizer report counts for every task above, is preserved in `plan_devices.md`'s
per-task closing notes and in git commit messages — not duplicated here.

---

## 4. Current blocker / main problem

**No code-level blocker exists.** The `cna_demo_devices` Android build gap that used to
be tracked here is fixed (`DEV-BUILD-004`).

**Open, out-of-scope finding, not yet actioned:** a full, untargeted
`cmake --build cmake-build-android` (building every target, not just `CNA`) still fails
on `cna_demo_input` (`MouseCursor.hpp:8:10: fatal error: 'SDL3/SDL.h' file not found`) —
the identical root cause `DEV-BUILD-004` found and fixed for `cna_demo_devices` (a demo
target links only `CNA`, which links `SDL3::SDL3` as `PRIVATE`, so SDL3's include paths
never propagate). Likely affects every other demo executable too
(`cna_demo_2d`/`cna_demo_sound`/`cna_demo_xact`), unconfirmed. Does not block the actual
verification gate (Section 7 only builds `--target CNA` for Android). No plan task
exists for it yet.

**`DEV-API-002`'s open half** (see Section 3) is the natural resume point: designing a
compile-time or test-time check that fails if a future public extension is left
untagged. No design work has started on this — it needs its own scoping pass (likely:
either a script that diffs public headers against `docs/devices-api-coverage.md`, or a
test that greps for suspicious patterns). Not attempted yet because three real bugs of
exactly this shape were still being found by manual audit when the user asked to stop.

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
- **Deliberate, unfixed by design:** destroying `Accelerometer` from within its own
  `CurrentValueChanged` handler is unsafe (the legacy `ReadingChanged` check touches
  `this` again afterward). `Detail::AndroidSensorBridge`'s analogous boundary
  (destroying the *outer* `Compass`/`Motion` object from within its own callback) is
  similarly accepted-unsupported. Tracked by open tasks `COMPASS-008`/`MOTION-010`.
- **Deliberate, documented limitation:** two or more distinct *external* threads calling
  `Detail::AndroidSensorBridge::Stop()` concurrently on the same bridge can still race
  on `join()` — not fully serialized, since doing so would risk a deadlock against the
  already-accepted reentrant self-stop case.
- **Needs verification:** whether `Motion`'s `Gravity`/`DeviceAcceleration`/
  `DeviceRotationRate`/`Attitude` need the same Android-landscape axis remap
  `Accelerometer`/`Gyroscope` use — left as raw sensor-frame axes, pending real
  hardware (`MOTION-002`).
- **Incomplete (`DEV-API-002`):** no compile-time or test-time check yet enforces that
  a newly-added public extension gets a `NOXNA` tag. Three real instances of exactly
  this bug were found and fixed by manual audit this session
  (`TimeBetweenUpdatesChanged`, all five reading structs' equality/`ToString`/hash
  members, `AccelerometerReadingEventArgs`'s equivalent) — manual audit is not a
  substitute for an automated check.
- **Needs verification, likely permanent:** iOS cross-compilation — no Apple toolchain
  in this Linux container.
- **Needs physical hardware verification (never done, for anything, ever):** every
  axis-sign question above, `VibrateController` actually actuating a real motor,
  `Compass` heading accuracy/calibration, `Motion` attitude tracking. See
  `docs/devices-hardware-checklist.md` for what an emulator run cannot substitute for.
- **Out of scope, not this repo's bug:** `sharp-runtime`'s `TimeSpan` copy-constructor
  TSan race, and a separately-noted `System::EventHandler<T>::Raise()` reentrancy
  fragility (iterates its live handler list directly, not a snapshot) — both are
  sibling-repo (`sharp-runtime`) concerns, do not fix from here.

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
examples/demo_devices/                      ← DevicesDemo (cna_demo_devices target)
examples/demo_devices/android/              ← Android Gradle/CMake app project
docs/devices-hardware-checklist.md          ← manual real-hardware verification steps
docs/devices-build.md                       ← reproducible build/test commands
docs/devices-native-backend-design.md       ← Compass/Motion native backend design
docs/devices-api-coverage.md                ← per-member API coverage table
docs/devices-thread-safety.md               ← consolidated thread-safety contract
docs/devices-android.md                     ← consolidated Android-specific reference
docs/location-future-plan.md                ← why GPS/location isn't here
plan_devices.md                             ← the open, 72-task plan (2026-07-05)
```

- **`SensorBase<T>`** owns `CurrentValue`, `IsDataValid`, `TimeBetweenUpdates`,
  `CurrentValueChanged`, `Dispose()`. Every field is mutex-guarded; getters return by
  value. Has `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` (protected) — derived
  `Dispose(bool)` overrides must use these. **Do not restructure without a concrete,
  newly-found bug** — stable across many prior hardening passes.
- **Invariant:** any class overriding `Dispose(bool)` must add
  `using SensorBase<T>::Dispose;`, or C++ name-hiding breaks the inherited public
  no-arg `Dispose()`.
- **`Accelerometer`/`Gyroscope`** share `Detail::SdlSensorSubsystem<TSensor>`.
- **`Compass`/`Motion`** each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`. Both interfaces
  compile and are mockable on every platform. The concrete `AndroidCompassBackend`/
  `AndroidMotionBackend` are entirely `#ifdef __ANDROID__`-gated, both built on the
  shared `Detail::AndroidSensorBridge` (one instance per Android sensor type, owns its
  own worker thread + `ALooper` — thread-affine, cannot be pumped externally). Each now
  has its own instance mutex guarding `state_`/`started_` (`SENSORBASE-004`).
  `SetBackendForTesting()` (`NOXNA`, both classes) lets tests inject a fake backend.
- **`VibrateController`:** SDL3 haptic-backed only; no native Android bridge exists yet
  (`VIB-002`/`VIB-003` re-examine whether one is actually needed).
- **`GetTypeName()` invariant:** returns `.`-separated fully-qualified .NET names,
  tagged `NOXNA`.
- **Boundaries — do not cross:**
  - `third_party/SDL` is vendored with its own `CLAUDE.md` forbidding AI-authored
    contributions — read-only for research.
  - `sharp-runtime` is a sibling repo under separate development — do not fix its bugs
    (the `TimeSpan` race, the `EventHandler<T>` reentrancy fragility) by editing files
    there directly.
  - Do not expand `Microsoft::Devices` scope to camera, radio, or
    phone-call/photo-picker APIs.
  - Do not fake `Compass`/`Motion` data from other sensors, and do not add GPS/location
    to `Microsoft::Devices::Sensors` under any circumstances (including as `NOXNA`) —
    see `docs/location-future-plan.md`.
  - The XNA 4.0 class name is `VibrateController`, not "VibrationController" — never
    rename it or let the wrong name appear as if it were correct (`VIB-001`).

---

## 7. Useful commands

**ZIP-export caveat:** every command below assumes a real `git clone` with submodules
initialized (`git submodule update --init` — non-recursive; `--recursive` additionally
tries to clone ~19 unneeded nested codec submodules this project doesn't need) plus the
two sibling repos this project depends on (`sharp-runtime`, `easy-gl`, which itself
needs `meta-gl`) checked out next to this one.

```bash
# Configure (only if CMakeCache.txt is stale/missing):
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build:
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Devices-only filter (exact suite names, 21 suites / 313 cases):
cd cmake-build-debug && ctest --output-on-failure -R "AccelerometerFailedExceptionTests|AccelerometerReadingEventArgsTests|AccelerometerReadingTests|AccelerometerTests|AndroidSensorOrientationTests|AttitudeReadingTests|CalibrationEventArgsTests|CompassReadingTests|CompassTests|AndroidCompassMathTests|AndroidMotionMathTests|AndroidSensorBridgeTests|GyroscopeReadingTests|GyroscopeTests|MotionReadingTests|MotionTests|ScopeExitTests|SensorBaseTests|SensorFailedExceptionTests|SensorSubsystemOwnershipTests|VibrateControllerTests"

# Build and run the Devices demo (needs a real display):
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices

# Android cross-compile check (NDK at ~/Android/Sdk/ndk/) — CNA library only, known clean:
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Reproduce Section 4's cna_demo_input finding (expected to currently fail):
cmake --build cmake-build-android --target cna_demo_input -j$(nproc)

# Sanitizer builds:
cmake --preset devices-asan && cmake --build --preset devices-asan
cmake --preset devices-tsan && cmake --build --preset devices-tsan
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan
```

No dedicated lint/format tooling is configured for this project. No CI infrastructure
exists in this repo (`DEV-BUILD-003`) — the commands above are the current gate.

---

## 8. Next smallest tasks

Ordered smallest/cheapest first, not strictly by `plan_devices.md`'s own priority
labels. Read that file for full context on each (grep for the task ID).

1. **Finish `DEV-API-002`** — design and add a compile-time or test-time check that
   fails when a public extension is left without a `NOXNA` tag. Files likely involved:
   a new test under `tests/Microsoft/Devices/`, or a small standalone script/CI step
   that diffs headers against `docs/devices-api-coverage.md`. Verification: the new
   check must itself be demonstrated to fail on a deliberately-unmarked extension
   (temporarily remove a `NOXNA` tag, confirm red, put it back).
2. **Re-verify `SensorFailedException`/`AccelerometerFailedException` constructor
   signatures against MSDN** — the remaining half of `DEV-API-002`'s scope. Files:
   `include/Microsoft/Devices/Sensors/SensorFailedException.hpp`,
   `AccelerometerFailedException.hpp`. Verification: `ctest -R
   SensorFailedExceptionTests\|AccelerometerFailedExceptionTests`.
3. **Scope the `cna_demo_input` Android build failure** (Section 4) as its own plan
   task, or fix it the same way `DEV-BUILD-004` fixed `cna_demo_devices`. Files:
   `examples/demo_input/CMakeLists.txt` (or wherever `cna_demo_input` is defined).
   Verification: `cmake --build cmake-build-android --target cna_demo_input`.
4. Pick up the next open `plan_devices.md` task — see the list below — or ask the user
   to prioritize.

50 tasks remain open (`DEV-API-002` counted separately as in-progress), spanning: the
entire `VibrateController` block (`VIB-001`–`VIB-010`, deliberately untouched so far),
most Accelerometer/Gyroscope/Compass/Motion API- and hardware-verification audits
(`ACCEL-001`–`004`/`006`/`007`, `GYRO-001`–`003`/`005`, `COMPASS-001`–`008`,
`MOTION-001`–`007`/`009`/`010`), `DEV-BUILD-003` (CI), `ANDROID-BRIDGE-001`/`003`/`004`,
`SDL-SENSOR-001`/`003`, `READINGS-001`/`003`, `DEMO-001`/`002`, and `VERIFY-001`–`003`.

---

## 9. Do not do yet

- Do not start implementing multiple `plan_devices.md` tasks in one pass — one task, one
  small verified change, one commit, per this repository's established convention.
- Do not assume which task to start without checking Section 8 first, and ask the user
  if priority is unclear.
- Do not restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem<TSensor>`, or
  `Detail::AndroidSensorBridge`'s locking scheme without a concrete, newly-found bug —
  both have been through multiple hardening passes already.
- Do not fake `Compass`/`Motion` data from other sensors, and do not add GPS/location to
  `Microsoft::Devices::Sensors` under any circumstances (including as `NOXNA`) — see
  `docs/location-future-plan.md`.
- Do not build a native Android vibration (JNI) backend without first completing plan
  task `VIB-003`'s re-verification step.
- Do not claim Android/iOS physical-hardware verification, or a real-device run, unless
  it was actually done in the current session.
- Do not edit anything under `third_party/SDL` — vendored, has its own `CLAUDE.md`
  forbidding AI-authored contributions.
- Do not fix bugs found in `sharp-runtime` by editing files there directly.
- Do not trust a single passing `ctest` run as proof a new concurrency/lifetime change
  is correct — loop it (20-60+ iterations) and/or run it under a sanitizer preset first.
- Do not open yet another new plan file — `plan_devices.md` is current, use it.
- Do not rename or refer to `VibrateController` as "VibrationController" anywhere.

---

## 10. Resume prompt

```
Read NEXT.md first. plan_devices.md (72 tasks, rewritten 2026-07-05) is the open plan
for Microsoft::Devices/Microsoft::Devices::Sensors/VibrateController. 21 tasks are
closed, DEV-API-002 is in progress (see NEXT.md Sections 3/4/8 for exactly what
remains), 50 are open. Do not open another new plan file. Do not claim anything is
"done" beyond what NEXT.md Section 2 states as already-observed, and do not claim
physical Android/iOS hardware verification unless a real device was actually used this
session.

Inspect only the files needed for the first task you pick from Section 8 (or ask the
user which to prioritize if unclear). Do not refactor unrelated code. Make one small,
verified improvement, then stop and report before moving to the next task.

Run the relevant build/test command from Section 7 after the change — and if the change
touches concurrency or object lifetime, also loop the test (20-60+ iterations) and/or
run it under a sanitizer preset (devices-asan/devices-tsan/devices-ubsan) before
trusting it.

Update NEXT.md after finishing, keeping it concise — this file should stay a short,
current-state handoff, not a full phase-by-phase history (that detail belongs in
plan_devices.md itself or git history).
```
