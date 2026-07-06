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

**Current development phase:** a brand-new, from-scratch 70-task audit/implementation
plan — `plan_devices.md`, rewritten completely on 2026-07-05 — is now open. It replaces
every prior version of that file (the original 31-task plan, `plan_devices_phase2.md`
through `plan_devices_phase9.md`, and a later 143-task/Phases-0-10 plan). **None of that
plan's 70 tasks have been started yet** — this file was written immediately after the
plan itself, before any implementation work began.

**Important architectural decisions (unchanged by the plan rewrite):**
- Public API names/signatures match XNA 4.0 (or, for `Microsoft::Devices`, the archived
  WP7 SDK docs — FNA has no equivalent) exactly; C# properties become
  `getXProperty()`/`setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration — **but see Section 4:
  this tagging is currently inconsistent for at least one confirmed member.**
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the shared base
  for `Accelerometer`/`Compass`/`Gyroscope`/`Motion` — see Section 6.
- `VibrateController` (correct XNA name — not "VibrationController") is a singleton
  (`getDefaultProperty()`), lives directly in `Microsoft::Devices` (not `::Sensors`),
  does not derive `SensorBase<T>`/`IDisposable`. It has exactly one backend today: SDL3
  `SDL_Haptic` — there is no separate phone-vibrator abstraction yet (see plan tasks
  `VIB-002`/`VIB-003`).
- `Compass`/`Motion` each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`. Every other
  platform keeps the original permanent-stub behavior.
- Tests live under `tests/` mirroring the `include`/`src` namespace path 1:1, Google Test.

---

## 2. Current status

All facts below were last directly verified during the "tiny final correctness patch"
pass earlier in this same working session (commit `1b81bea2`..`9b6e856c`), immediately
before `plan_devices.md` was rewritten. **No build or test command was re-run as part of
writing this file** — per the request that produced it, this handoff is based on
already-observed results, not a fresh verification pass.

**Build:** `CNA` and `CnaTests` built cleanly under `EASYGL` (`cmake-build-debug`) as of
that pass. Android cross-compile of the `CNA` library target (`arm64-v8a`, NDK r30, API
24) was clean, confirmed via `llvm-nm` symbol inspection. **`cna_demo_devices`'s Android
cross-compile was blocked** — see Section 4. iOS: no toolchain in this container.

**Tests:** full `ctest` was 3268 tests, 3266 passed, 2 expected `GTEST_SKIP()`s
(no accelerometer/gyroscope hardware in this container). The Devices-only filter (see
Section 7) was 280/280 (278 passed + the same 2 expected skips). The
concurrency-relevant subset was looped 40/40 clean.

**Sanitizers:** `devices-asan` — 0 issues. `devices-tsan` — warnings present, but every
one individually confirmed (via its own `Location is global ...` line) to be the same
single pre-existing, out-of-scope race in `sharp-runtime`'s
`System::TimeSpan::TimeSpan(const TimeSpan&)` copy constructor. `devices-ubsan` — 3
pre-existing findings, all in `Vector3::GetHashCode()`/`Matrix::GetHashCode()` (signed
integer overflow), 0 in any `Microsoft::Devices` file.

**2026-07-06 re-verification (`DEV-BUILD-002`), with the corrected exact-suite-name
filter (see Section 7):** 283 tests (up from the 280/280 figure above, which used the
old, buggy substring filter — see Section 3), 281 passed, 2 expected hardware skips, on
plain `cmake-build-debug`, and again identically on all three of
`devices-asan`/`devices-tsan`/`devices-ubsan`. Sanitizer findings unchanged from the
paragraph above (0/41-all-known/3-all-known respectively) — re-confirmed, not just
carried forward.

**Working:** `Accelerometer`/`Gyroscope` — real, SDL3-backed. `Compass`/`Motion` — real
on Android only (`Detail::AndroidCompassBackend`/`AndroidMotionBackend`, pure NDK, no
JNI), permanent stub on every other platform. `VibrateController` — real, SDL3
haptic-backed. `examples/demo_devices` (`cna_demo_devices`) built and ran as a real
Android APK once, in an earlier session (historical result, not currently reproducible
here — see Section 4).

**Does not work / not implemented:** iOS backend for anything in this scope (`Compass`,
`Motion`, or vibration). `Compass.TrueHeading` permanently equals `MagneticHeading` (no
declination source). `Motion`'s coordinate-remap question is unresolved. **The new
`plan_devices.md` audit found that `TimeBetweenUpdates` is not honored by the SDL-backed
sensors (`Accelerometer`, `Gyroscope`) at all** — confirmed by grep, zero references to
the property in either `.cpp` file — and is only applied once, at `Start()` time, for
the Android-backed sensors. No physical Android/iOS hardware has ever been used to
verify anything in this scope, in any session.

---

## 3. Recent changes

**2026-07-06 — `ANDROID-BRIDGE-002` closed: Android-backed `TimeBetweenUpdates` now
changes live.** `Detail::AndroidSensorBridge::Start()` used to convert
`timeBetweenUpdates` to `ASensorEventQueue_setEventRate()`'s microsecond parameter only
once, at `Start()` time. Added `AndroidSensorBridge::SetSampleInterval(TimeSpan)`: stores
the new value and sets an atomic flag, waking the looper (`ALooper_wake()`, same
technique `Stop()` already uses); `Run()`'s own poll loop — the only code that touches
`queue_`/`sensor_` — picks it up once per iteration and re-applies
`ASensorEventQueue_setEventRate()` on the live queue from its own thread. Added
`SetSampleInterval()` to `ICompassBackend`/`IMotionBackend` (pure virtual — updated both
real implementers, `AndroidCompassBackend`/`AndroidMotionBackend`, forwarding to every
bridge they own, plus the two test-only fakes in `CompassTests.cpp`/`MotionTests.cpp`,
the only other implementers in the codebase). Wired `Compass`/`Motion`'s constructors to
subscribe to the inherited `SensorBase<T>::TimeBetweenUpdatesChanged` event and forward
to `backend_`. Added 6 new tests (2 `AndroidSensorBridgeTests.cpp` safe-no-op-on-this-host
tests, 2 `CompassTests.cpp` + 2 `MotionTests.cpp` forwards-to-fake-backend tests).
Verified: 296/296 tests (up from 290) on plain `cmake-build-debug` and all three
sanitizer presets (0 ASan; TSan/UBSan findings unchanged from previously known); also
cross-compiled `CNA` for Android (arm64-v8a, NDK r30, API 24) and confirmed via
`llvm-nm` that all three new `SetSampleInterval()` symbols are real, compiled-in Android
code — **not runtime-verified**, no Android device/emulator run this session. This
closes `SENSORBASE-001`'s remaining gap for all four sensor classes except
minimum/maximum `TimeBetweenUpdates` value validation, which is still open (no
dedicated task exists for it yet).

**2026-07-06 — `DEV-API-001` closed: public API matrix re-verified.** Read every public
header under `include/Microsoft/Devices/` end-to-end and cross-checked every public
member against `docs/devices-api-coverage.md`'s existing content. Result: **0 Missing,
0 Extra-unmarked, 2 Wrong-visibility (unverified)** — `AccelerometerReadingEventArgs`'s
and `SensorReadingEventArgs<T>`'s setters are fully `public`, unlike every reading
struct's `private`+`friend` convention (Task P3-2); recorded as findings for the
already-existing `READINGS-002` task, not fixed here. Explicitly re-confirmed this
task's named example case — `getStateProperty()`'s `NOXNA` asymmetry — is not a bug
(already resolved by `DEV-API-003`, see below), not new drift this pass needed to catch.
Added "Cross-cutting members" tables (destructor/`Dispose()`/`Dispose(bool)`/
`GetTypeName()` for the four sensor classes; constructor/getter/setter/equality/
`ToString()`/`GetHashCode()`/`GetTypeName()` for the five reading structs) and extended
the `Detail::` internals table with `SdlSensorSubsystem<TSensor>`/
`GetGlobalSdlSensorMutex()`/`ScopeExit<F>`. No production code changed — doc-only.

**2026-07-06 — `SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/`SDL-SENSOR-002` closed
(Accelerometer/Gyroscope only): `TimeBetweenUpdates` now really throttles.** SDL3 has no
per-sensor polling-rate control API for `SDL_SENSOR_ACCEL`/`SDL_SENSOR_GYRO`, so added
software throttling instead: `SensorBase<T>` gained
`ShouldAcceptUpdateAt(now)`/`ResetUpdateThrottle()` — a per-instance, `mutex_`-guarded
decision that takes the current wall-clock time as an explicit parameter (kept as a pure
function of its inputs specifically so it's unit-testable with synthetic
`DateTimeOffset` values, no real-time sleeps). `Accelerometer`/`Gyroscope`'s
`ProcessSensorUpdateEvent()` (the real SDL event path only, **not** the `NOXNA`
synthetic-injection test hooks, which deliberately keep bypassing it) now call it before
`DispatchSensorReading()`; `Start()` calls `ResetUpdateThrottle()` so a fresh start
always delivers an immediate first sample. Changing `TimeBetweenUpdates` while running
takes effect on the very next event — no `Stop()`/`Start()` needed. Added 7 new
`SensorBaseTests.cpp` cases proving the throttle decision (independent per-instance
throttling, measuring from the last *accepted* call not the last *attempted* one, zero
interval never throttling, reset-on-restart). Verified: 290/290 tests (up from 283) on
plain `cmake-build-debug` and all three sanitizer presets, 40-iteration
`AccelerometerTests.*:GyroscopeTests.*` loop clean, no new TSan/UBSan findings beyond
the previously-known ones. **Still open, not touched by this pass:** `Compass`/`Motion`'s
Android backend (`ANDROID-BRIDGE-002`, only applies the interval once at `Start()`);
`TimeBetweenUpdates` minimum/maximum/negative-value validation (`setTimeBetweenUpdatesProperty()`
still accepts any value unchanged); the manual demo was not run to visually confirm a
reduced event rate (no display in this environment this session).

**2026-07-06 — `DEV-BUILD-002` closed: Devices-only test filter corrected.** Extracted
every `TEST(...)` suite name directly from `tests/Microsoft/Devices/` (21 suites, 283
cases, no `TEST_F`/`TEST_P` in this scope) and diffed that ground truth against the
filter this project had been using. Found the old substring filter (`Accelerometer`,
`Motion`, etc.) **silently dropped `CalibrationEventArgsTests`** (3 tests, no matching
substring) **and matched 2 unrelated false positives** outside `Microsoft::Devices`
(`GamePadTest.GetAccelerometerEXT...`, `SdlInputBridgeTouchGestureTest.FingerMotion...`).
Replaced it with an exact-suite-name filter in `docs/devices-build.md` (Sections 2 and
6) and this file's Section 7 — verified to match all 283 cases, nothing more. Re-ran the
corrected filter on plain `cmake-build-debug` and all three sanitizer presets
(`devices-asan`/`-tsan`/`-ubsan`): 283/283 (281 passed + 2 expected skips) on all four,
sanitizer findings unchanged from previously known (0 ASan, 41-all-known-`TimeSpan`-race
TSan, 3-all-known-`Vector3`/`Matrix`-hash UBSan). No production code changed.

**2026-07-06 — `DEV-API-003` investigated and closed, no code change.** The
`getStateProperty()` `NOXNA` split (`Accelerometer` unmarked, `Gyroscope`/`Compass`/
`Motion` all marked) was re-checked against the archived MSDN citations already recorded
in `AUDIT.md`/`docs/devices-api-coverage.md` (`plan_devices_phase2.md` Task P2-17,
2026-07-02): `Accelerometer.State` is real WP7 API (`ff707930`); `Gyroscope.State`
(`hh239201`), `Compass.State` (`hh220912`), `Motion.State` (`hh239189`) are not real on
those three classes. The current code already matches the authoritative reference —
this was a stale re-flagging in the fresh plan's Section 1, not an actual bug. See
`plan_devices.md`'s `DEV-API-003` closing note. Devices-only `ctest` filter re-run
clean (280/280 + 2 expected hardware skips) as a sanity check; no header/source edited.

**2026-07-05 — `plan_devices.md` rewritten from scratch.** Replaces every prior plan
generation with 70 new tasks across 16 sections, grounded in a fresh code inspection
rather than old plan claims. Concrete findings from that inspection, not yet fixed:
- `Accelerometer.cpp`/`Gyroscope.cpp` never read `getTimeBetweenUpdatesProperty()` —
  the SDL backends ignore the requested update interval entirely (`SENSORBASE-001`,
  `ACCEL-005`, `GYRO-004`, `SDL-SENSOR-002`).
- `Accelerometer` throws its own `AccelerometerFailedException`; `Gyroscope`/`Compass`/
  `Motion` all throw the generic `SensorFailedException` — split not yet verified
  against an authoritative XNA/WP7 reference (`DEV-API-005`).
- No CI infrastructure exists in this repository at all (`DEV-BUILD-003`).

**Prior passes, already closed, most-recent first** (full detail in git history and the
prior plan generations, not repeated here):
- **Tiny final correctness patch** (4 commits): fixed `Impl::Run()`'s two early-failure
  paths leaking a stale `ALooper*` (RAII guard added); replaced non-standard `M_PI` with
  a local constant in `AndroidCompassMath.hpp`/its tests; considered and explicitly
  rejected tightening `VibrateController::getIsSupportedProperty()` (would require
  calling `SDL_InitHapticRumble()`, which has a real, unverifiable-without-hardware
  device side effect).
- **Micro-cleanup pass** (2 commits): added `Impl::stateMutex_` guarding
  `AndroidSensorBridge::Start()`/`Stop()`'s shared state; reset stale `looper_` on the
  normal exit path (later found incomplete — fixed above); handled
  `ASensorEventQueue_setEventRate()`'s return value explicitly; clamped
  `ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds()` at `INT32_MAX` (a real,
  if obscure, reachable UB path via `static_cast` of an out-of-range `double`).
- **Stabilization pass** (3 commits): fixed `AndroidSensorBridge::Start()` calling
  `std::terminate()` on a repeated call (reassigning an already-joinable
  `std::thread`); added a condition-variable startup handshake so `Start()` reports
  real success/failure instead of optimistic `true`; changed `impl_` to `shared_ptr`
  to close a use-after-free on reentrant `Stop()`; made the worker-thread callback
  exception policy explicit (catch-and-discard, matching the SDL subsystem's existing
  policy); added `Compass`/`Motion` `Start()` already-started guards and
  `SetBackendForTesting()` enforcement.
- Before that: two full plan generations (31 tasks, then 143 tasks across Phases 0-10)
  gave `Compass`/`Motion` their real Android backends (previously permanent stubs) and
  produced this project's first working Android APK. See git history / old plan files
  for that detail — not restated here.

---

## 4. Current blocker / main problem

**No code-level blocker prevents starting the new plan's tasks** — the vast majority
(API audits, unit fixes, test additions) need only the desktop build.

**One concrete, reproducible build gap, not yet root-caused:**
- **Symptom:** `cna_demo_devices`'s Android cross-compile fails:
  `examples/demo_devices/src/Main.cpp:1:10: fatal error: 'SDL3/SDL_main.h' file not
  found`.
- **Failing command:** `cmake --build cmake-build-android --target cna_demo_devices`
  (using the Android toolchain file, `arm64-v8a`, API 24 — see Section 7).
- **Affected files/modules:** `examples/demo_devices/src/Main.cpp` only. The `CNA`
  library target itself (which does not include `SDL_main.h`) cross-compiles for
  Android without issue.
- **Suspected cause (unconfirmed):** the Android build's include paths for the
  `cna_demo_devices` target likely don't reference an Android-architecture SDL3 header
  set the way the `CNA` library target's own SDL discovery does. Host SDL3 headers
  exist at `/usr/local/include` in this container, but that's the host architecture,
  not `aarch64-none-linux-android24`.
- **What has been tried:** this exact failure has now been reproduced identically
  across three separate verification passes (stabilization, micro-cleanup, tiny final
  correctness patch) — confirmed stable/reproducible, not flaky. No attempt has yet
  been made to actually fix the include path or CMake target configuration; each pass
  only re-confirmed the symptom was unchanged and out of that pass's scope.
- **Historical context:** this same demo target *did* successfully cross-compile,
  install, and launch as a real APK once, in an earlier session (before this specific
  gap appeared) — so this is a regression in the build environment/configuration
  somewhere between then and now, not a gap that was always present.

---

## 5. Known bugs and limitations

- **Not a bug, closed 2026-07-06 (`DEV-API-003`):** `Accelerometer::getStateProperty()`
  correctly lacks a `NOXNA` marker (real WP7 API, MSDN `ff707930`) while
  `Gyroscope`/`Compass`/`Motion`'s equivalents correctly have it (no real `State` on
  those three, MSDN `hh239201`/`hh220912`/`hh239189`) — the four classes were already
  consistent with the authoritative reference; no code change made.
- **Fixed 2026-07-06 (`SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/`SDL-SENSOR-002`/
  `ANDROID-BRIDGE-002`):** `TimeBetweenUpdates` now actually affects event rate for all
  four sensor classes: `Accelerometer`/`Gyroscope` via a new per-instance
  `SensorBase<T>::ShouldAcceptUpdateAt()` software throttle in `ProcessSensorUpdateEvent()`;
  `Compass`/`Motion` via `Detail::AndroidSensorBridge::SetSampleInterval()` re-applying
  `ASensorEventQueue_setEventRate()` on the live queue. The Android side was only
  confirmed to compile (cross-compile + `llvm-nm`), not runtime-verified — no Android
  device/emulator run this session. **Still open (not this fix's scope, no dedicated
  task yet):** `TimeBetweenUpdates` minimum/maximum value validation —
  `setTimeBetweenUpdatesProperty()` still accepts any value, including negative,
  unchanged.
- **By design, not a bug:** `Compass.TrueHeading` always equals `MagneticHeading` — real
  declination needs a location source, out of scope for `Microsoft::Devices::Sensors`
  (see `docs/location-future-plan.md`). `Motion.Calibrate` is never raised by any
  backend.
- **Deliberate, documented limitation:** concurrent `Dispose()` calls on the *same*
  sensor instance from two threads block the losing caller until the winner's cleanup
  finishes, then return as a silent no-op — matches the conventional .NET
  `IDisposable` contract.
- **Deliberate, unfixed by design:** destroying `Accelerometer` from within its own
  `CurrentValueChanged` handler is unsafe (the legacy `ReadingChanged` check touches
  `this` again afterward). `Detail::AndroidSensorBridge`'s analogous boundary
  (destroying the *outer* `Compass`/`Motion` object from within its own callback) is
  similarly accepted-unsupported, `shared_ptr`-hardened only at the `Impl` level.
- **Deliberate, documented limitation:** two or more distinct *external* threads calling
  `Detail::AndroidSensorBridge::Stop()` concurrently on the same bridge can still race
  on `join()` — not fully serialized, since doing so would risk a deadlock against the
  already-accepted reentrant self-stop case.
- **Needs verification:** whether `Motion`'s `Gravity`/`DeviceAcceleration`/
  `DeviceRotationRate`/`Attitude` need the same Android-landscape axis remap
  `Accelerometer`/`Gyroscope` use — left as raw sensor-frame axes, pending real
  hardware (plan task `MOTION-002`).
- **Needs verification:** `AccelerometerFailedException` vs. generic
  `SensorFailedException` split across the four sensor classes has not been checked
  against an authoritative XNA/WP7 reference (`DEV-API-005`).
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
docs/devices-api-coverage.md                ← per-member API coverage table (predates the new plan; DEV-API-001 will supersede/verify it)
docs/devices-android.md                     ← consolidated Android-specific reference
docs/location-future-plan.md                ← why GPS/location isn't here
plan_devices.md                             ← the open, 70-task plan (2026-07-05)
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
  own worker thread + `ALooper` — thread-affine, cannot be pumped externally).
  `SetBackendForTesting()` (`NOXNA`, both classes) lets tests inject a fake backend.
- **`VibrateController`:** SDL3 haptic-backed only; no native Android bridge exists yet
  (the plan's `VIB-002`/`VIB-003` re-examine whether one is actually needed).
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
    rename it or let the wrong name appear as if it were correct (plan task `VIB-001`).

---

## 7. Useful commands

**ZIP-export caveat:** every command below assumes a real `git clone` with submodules
initialized (`git submodule update --init --recursive`) — a bare source export has empty
`third_party/SDL`/`SDL_image`/`SDL_mixer`/`vendor/googletest` and will not configure.

```bash
# Configure (only if CMakeCache.txt is stale/missing):
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build:
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Devices-only filter — corrected 2026-07-06 (DEV-BUILD-002) to use exact suite names;
# the old substring filter silently dropped CalibrationEventArgsTests (3 tests) and
# picked up 2 unrelated false positives (GamePadTest/SdlInputBridgeTouchGestureTest).
# Verified to match exactly the 283 current TEST() cases under tests/Microsoft/Devices/,
# no more, no less. Last run: 283 tests, 281 passing + 2 expected hardware skips.
# Full detail, including the sanitizer variants, in docs/devices-build.md Section 2/6.
cd cmake-build-debug && ctest --output-on-failure -R "AccelerometerFailedExceptionTests|AccelerometerReadingEventArgsTests|AccelerometerReadingTests|AccelerometerTests|AndroidSensorOrientationTests|AttitudeReadingTests|CalibrationEventArgsTests|CompassReadingTests|CompassTests|AndroidCompassMathTests|AndroidMotionMathTests|AndroidSensorBridgeTests|GyroscopeReadingTests|GyroscopeTests|MotionReadingTests|MotionTests|ScopeExitTests|SensorBaseTests|SensorFailedExceptionTests|SensorSubsystemOwnershipTests|VibrateControllerTests"

# Build and run the Devices demo (needs a real display):
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices

# Android cross-compile check (NDK at ~/Android/Sdk/ndk/) — CNA library only, known clean:
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Reproduce Section 4's blocker (expected to currently fail):
cmake --build cmake-build-android --target cna_demo_devices -j$(nproc)

# Sanitizer builds:
cmake --preset devices-asan && cmake --build --preset devices-asan
cmake --preset devices-tsan && cmake --build --preset devices-tsan
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan
```

No dedicated lint/format tooling is configured for this project. No CI infrastructure
exists in this repo (plan task `DEV-BUILD-003`) — the commands above are the current
gate.

---

## 8. Next smallest tasks

These are pulled directly from the newly-rewritten `plan_devices.md` — read that file
for full context on each. Ordered smallest/cheapest first, not strictly by the plan's
own priority labels.

`DEV-API-003` (the `getStateProperty()` `NOXNA` question), `DEV-BUILD-002` (the
Devices-only test filter), `SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/`SDL-SENSOR-002`
(SDL-backed `TimeBetweenUpdates` throttling), `DEV-API-001` (the public API matrix), and
`ANDROID-BRIDGE-002` (Android-backed `TimeBetweenUpdates` while running) are now closed
— see Section 3. Next smallest remaining tasks:

1. **Resolve the two `DEV-API-001` wrong-visibility findings** (plan task
   `READINGS-002`, already existed before `DEV-API-001`). Goal: check an authoritative
   WP7 7.0 reference for whether `AccelerometerReadingEventArgs`'s and
   `SensorReadingEventArgs<T>`'s setters are genuinely public in the real API, or should
   be `private`+`friend` like every reading struct (Task P3-2). Files:
   `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`,
   `include/Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp`,
   `docs/devices-api-coverage.md` (update the "Flagged findings" section once resolved).
2. **Investigate the `cna_demo_devices` Android `SDL3/SDL_main.h` build gap**
   (Section 4 above; not yet a plan task — scope it as one first). Goal: find why this
   specific target's Android include paths lack an Android-arch SDL3 header set the
   `CNA` library target itself doesn't need. Files: whichever `CMakeLists.txt` defines
   `cna_demo_devices`'s Android include paths. Verify with:
   `cmake --build cmake-build-android --target cna_demo_devices`.

---

## 9. Do not do yet

- Do not start implementing multiple `plan_devices.md` tasks in one pass — one task, one
  small verified change, one commit, per this repository's established convention.
- Do not assume which task to start without checking this file's Section 8 first, and
  ask the user if priority is unclear — the plan itself says "pick one, or ask the user
  first."
- Do not restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem<TSensor>`, or
  `Detail::AndroidSensorBridge`'s locking scheme without a concrete, newly-found bug —
  both have been through multiple hardening passes already.
- Do not fake `Compass`/`Motion` data from other sensors, and do not add GPS/location to
  `Microsoft::Devices::Sensors` under any circumstances (including as `NOXNA`) — see
  `docs/location-future-plan.md`.
- Do not build a native Android vibration (JNI) backend without first completing plan
  task `VIB-003`'s re-verification step — a prior pass already decided SDL3's own
  Android haptic backend was sufficient; don't silently redo that work or silently
  discard it without re-checking.
- Do not claim Android/iOS physical-hardware verification, or a real-device run, unless
  it was actually done in the current session.
- Do not edit anything under `third_party/SDL` — vendored, has its own `CLAUDE.md`
  forbidding AI-authored contributions.
- Do not fix bugs found in `sharp-runtime` by editing files there directly.
- Do not trust a single passing `ctest` run as proof a new concurrency/lifetime change
  is correct — loop it (20-60+ iterations) and/or run it under a sanitizer preset first.
- Do not open yet another new plan file — `plan_devices.md` was just rewritten from
  scratch; use it.
- Do not rename or refer to `VibrateController` as "VibrationController" anywhere.

---

## 10. Resume prompt

```
Read NEXT.md first. A brand-new 70-task plan_devices.md (rewritten from scratch on
2026-07-05) is open for Microsoft::Devices/Microsoft::Devices::Sensors/
VibrateController — none of its tasks have been started yet. Do not open another new
plan file. Do not claim anything is "done" beyond what NEXT.md Section 2 states as
already-observed, and do not claim physical Android/iOS hardware verification unless a
real device was actually used this session.

Inspect only the files needed for the first task you pick from Section 8 (or ask the
user which to prioritize if unclear). Do not refactor unrelated code. Make one small,
verified improvement at a time, then stop and report before moving to the next task.

Run the relevant build/test command from Section 7 after the change — and if the change
touches concurrency or object lifetime, also loop the test (20-60+ iterations) and/or
run it under a sanitizer preset (devices-asan/devices-tsan/devices-ubsan) before
trusting it.

Update NEXT.md after finishing, keeping it concise — this file should stay a short,
current-state handoff, not a full phase-by-phase history (that detail belongs in
plan_devices.md itself or git history).
```
