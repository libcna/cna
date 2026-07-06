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
declination source). `Motion`'s coordinate-remap question is unresolved. ~~**The new
`plan_devices.md` audit found that `TimeBetweenUpdates` is not honored by the SDL-backed
sensors (`Accelerometer`, `Gyroscope`) at all** — confirmed by grep, zero references to
the property in either `.cpp` file — and is only applied once, at `Start()` time, for
the Android-backed sensors.~~ **This whole paragraph is now stale (as of 2026-07-06) —
`SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/`ANDROID-BRIDGE-002` (all closed, see Section 3)
fixed both halves: SDL-backed sensors now throttle via `ShouldAcceptUpdateAt()`, and the
Android backend now re-applies a live change via `AndroidSensorBridge::SetSampleInterval()`
instead of only at `Start()` time. Left the original text struck through, not deleted,
per this section's own "frozen historical snapshot" framing above; kept for anyone
diffing this file's history.** No physical Android/iOS hardware has ever been used to
verify anything in this scope, in any session.

---

## 3. Recent changes

**2026-07-06 — `DEV-BUILD-004` closed: root-caused and fixed `cna_demo_devices`'s
Android build gap.** Root cause: `CNA` links `SDL3::SDL3` `PRIVATE` (a deliberate choice
— `CNA` hides its SDL backend behind `IGraphicsBackend`), so `cna_demo_devices` (which
only links `CNA`, never SDL3 directly) never received SDL3's include directories —
`Main.cpp`'s `#include <SDL3/SDL_main.h>` (added for Android's
`SDLActivity.java`/`dlsym("SDL_main")` requirement) has therefore always been broken;
it only ever "worked" on desktop by accident, because this container's host compiler
default include path (`/usr/local/include`) happens to carry a coincidentally-installed
system SDL3 dev package, masking the missing project-level include path — confirmed via
`make VERBOSE=1`'s actual compiler invocation on both platforms. Fixing the include (an
explicit `target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3)`) then surfaced a
second, deeper problem: the Android link then fails with `undefined symbol: main`,
because SDL's own `<SDL3/SDL_main.h>` `#define`s `main` to `SDL_main` on Android, leaving
no literal `main` for a plain ELF executable's C runtime startup to link against — a
plain `add_executable()` was never a valid Android app target format for this demo at
all; the real, working Android build (confirmed launched/screenshotted previously) is
the entirely separate Gradle/CMake project under `examples/demo_devices/android/`,
never this top-level target. Fixed by wrapping `cna_demo_devices`'s target definition in
`if(NOT ANDROID)`, matching this project's existing precedent, with the full
root-cause chain documented directly in the `CMakeLists.txt` comment. The prior "this is
a regression" framing in this file's old Section 4 was a mix-up between this top-level
target and the separate Gradle-based build — not an actual regression in either one.
Verified: desktop `cna_demo_devices` still builds/links clean (unaffected); Android
`--target CNA` still builds clean (unaffected); Android `--target cna_demo_devices` now
reports "no work to do" instead of failing. **New finding, not fixed:** a full,
untargeted Android build now fails on `cna_demo_input` instead, for the identical
underlying reason — see Section 4.

**2026-07-06 — `READINGS-002` closed: resolved both `DEV-API-001` wrong-visibility
findings against real archived MSDN pages.** Fetched the archived "previous-versions"
docs directly (classic `msdn.microsoft.com/en-us/library/<member>(v=VS.105)` URLs
301-redirect to the current `learn.microsoft.com` archive page even without knowing the
numeric ID) rather than assuming either way:
- `AccelerometerReadingEventArgs.X`/`Y`/`Z`: real API is `public double X/Y/Z { get; }`
  (MSDN `ff707568`/`ff707712`/`ff708055`) — **no setter at all**. `.Timestamp`: `public
  DateTimeOffset Timestamp { get; private set; }` (MSDN `ff707430`).
- `SensorReadingEventArgs<T>.SensorReading`: real API is `public T SensorReading { get;
  set; }` (MSDN `hh203225`) — genuinely fully public both ways.

Confirmed one real bug (fixed) and one non-bug (no change): removed
`AccelerometerReadingEventArgs`'s `setXProperty()`/`setYProperty()`/`setZProperty()`/
`setTimestampProperty()` entirely — all four were unused dead code (`Accelerometer`
only ever constructs this type via its 4-arg constructor) and had no real counterpart
to preserve access to. `SensorReadingEventArgs<T>::setSensorReadingProperty()` needed
no change — CNA's existing public setter already matches the real API exactly. Removed
4 now-dead tests from `AccelerometerReadingEventArgsTests.cpp` (292 tests, down from
296 — no tests added, since construction is already covered elsewhere). Verified: 292/292
on plain `cmake-build-debug` and ASan/UBSan (0 ASan; UBSan's 3 pre-existing findings
unchanged). TSan not re-run — no concurrency-relevant code touched.

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
Android backend (`ANDROID-BRIDGE-002`, only applies the interval once at `Start()`) —
**fixed later the same day, see the `ANDROID-BRIDGE-002 closed` entry below; this line
is historical, kept as-is rather than rewritten.**
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

**No code-level blocker exists.** The `cna_demo_devices` Android build gap this section
used to track is now fixed (`DEV-BUILD-004`, 2026-07-06) — see Section 3.

**New, out-of-scope finding from that fix, not yet actioned:** a full, untargeted
`cmake --build cmake-build-android` (building every target, not just `CNA`) still fails
— now on `cna_demo_input` (`MouseCursor.hpp:8:10: fatal error: 'SDL3/SDL.h' file not
found`), the identical root cause `DEV-BUILD-004` found and fixed for `cna_demo_devices`
(only links `CNA`, which links `SDL3::SDL3` `PRIVATE`, never SDL3 directly). Likely
affects every other demo executable (`cna_demo_2d`/`cna_demo_sound`/`cna_demo_xact`),
unconfirmed. **Does not block anything documented** — the actual Android verification
gate (Section 7, `--target CNA` only) never builds any demo target — but worth a
dedicated task if Android cross-compilation of examples beyond `cna_demo_devices` is
ever needed. No plan task exists for it yet.

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
(SDL-backed `TimeBetweenUpdates` throttling), `DEV-API-001` (the public API matrix),
`ANDROID-BRIDGE-002` (Android-backed `TimeBetweenUpdates` while running),
`READINGS-002` (the two event-args wrong-visibility findings), and `DEV-BUILD-004`
(the `cna_demo_devices` Android build gap) are now closed — see Section 3.

No further "next smallest task" is queued from a quick pass over `plan_devices.md` —
read that file's remaining open tasks (grep for section headers without a "— CLOSED"/
"— PARTIALLY CLOSED" suffix) and pick one, or ask the user to prioritize, per Section 9's
existing rule. One concrete lead if a task is wanted: the newly-found `cna_demo_input`
Android build failure (Section 4) — not yet scoped as its own plan task.

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

## 10. Stabilization pass summary (2026-07-06)

A small, verification-focused pass, not new feature work — requested explicitly as "a
small stabilization and verification pass after the latest Devices/Sensors changes," not
a continuation of `plan_devices.md`'s task list.

**What was changed:**
- **Stale docs fixed** (Section 3/5 entries below plus `docs/devices-api-coverage.md`,
  `include/Microsoft/Devices/Sensors/SensorBase.hpp`) — several places still said
  `Compass`/`Motion` only apply `TimeBetweenUpdates` once at `Start()` time, or that
  `ANDROID-BRIDGE-002` was open. Both are false as of the prior session's work; text
  updated to match current code (`AndroidSensorBridge::SetSampleInterval()` exists,
  `Compass`/`Motion` forward `TimeBetweenUpdatesChanged` to the backend). Historical
  dated entries in this file were annotated, not rewritten, per this file's own
  established convention.
- **`plan_devices.md` — `MOTION-008` closed.** Found already fully implemented (as a
  side effect of `ANDROID-BRIDGE-002`'s work) but never marked closed — a real, if
  small, stale-status bug in the plan file itself, not just prose.
- **`plan_devices.md` — new `SENSORBASE-008` task added**, documenting the still-missing
  `TimeBetweenUpdates` minimum/maximum/negative-value validation gap (confirmed still
  present; not fixed by this pass — out of its small, verification-focused scope).
- **Code change: `SensorBase<T>::ShouldAcceptUpdateAt()` switched from
  `System::DateTimeOffset` wall-clock time to `std::chrono::steady_clock`** for the
  throttle *decision* only (sensor reading timestamps, e.g.
  `AccelerometerReading::Timestamp`, are untouched — still wall-clock `DateTimeOffset`,
  matching the real WP7 API). Wall-clock time can step backward or jump (NTP correction,
  manual clock change), which would wedge a throttle *decision* open or defeat it for one
  event; `steady_clock` is standard-guaranteed never to be adjusted. Updated the two
  production call sites (`Accelerometer`/`Gyroscope::ProcessSensorUpdateEvent()`) and all
  `SensorBaseTests.cpp` throttle tests to use synthetic `steady_clock::time_point`
  values instead of `DateTimeOffset` ones — still zero real-time sleeps.
- **New test added:** `SensorBaseTests.ShouldAcceptUpdateAtWithNegativeTimeBetweenUpdatesNeverThrottles`
  — locks in that a negative `TimeBetweenUpdates` degrades safely to "never throttle"
  (same as `TimeSpan::Zero`), not a crash or a permanent-reject lockup. Confirmed the
  full checklist (first-update-accepted, too-soon-rejected, exact-interval-accepted,
  reset-on-`Start()`, independent-per-instance, zero-interval, negative-interval) is now
  covered — "reset on `Start()`" is covered at the `SensorBase<T>` mechanism level
  (`ResetUpdateThrottleForTestingMakesTheNextCallAlwaysAccept`) and confirmed by source
  inspection that `Accelerometer`/`Gyroscope::Start()` call `ResetUpdateThrottle()`, but
  has **no integration-level regression test** confirming that specific wiring — the
  same standing limitation as everything else reachable only through
  `ProcessSensorUpdateEvent()` (real SDL hardware event required, unavailable in this
  container).
- **`VibrateController` (`VIB-002`/`VIB-003`/related) — untouched**, as instructed.

**Commands run:**
- `git submodule update --init --recursive` — **did not complete** (timed out fetching
  ~19 nested codec submodules — `third_party/SDL_image`'s/`SDL_mixer`'s own
  `external/*` dependencies for AVIF/JXL/WebP/TIFF/libpng/GME/mod_xmp/mpg123/
  FluidSynth-MIDI/Opus/Vorbis — none of which this project's CMake actually needs;
  `cmake/ThirdPartySDL.cmake` explicitly passes `-DSDLIMAGE_AVIF=OFF` etc. for every one
  of them). Confirmed instead via `git submodule update --init` (non-recursive, exit 0,
  instant) that the actually-required top-level submodules (`SDL`, `SDL_image`,
  `SDL_mixer`, `vendor/googletest`) were already correctly initialized.
- `cmake --preset devices-ubsan` — succeeded.
- `cmake --build --preset devices-ubsan --target CnaTests` — succeeded.
- The Devices/Sensors `gtest_filter` documented in `docs/devices-build.md` — run against
  `cmake-build-debug`, `devices-ubsan`, `devices-asan`, and `devices-tsan`.
- 40-iteration `AccelerometerTests.*:GyroscopeTests.*` loop (concurrency-relevant, since
  the clock-source change touches the real dispatch path).

**What passed:** 293 tests (up from 292 before this pass), 291 passed + 2 expected
hardware skips (`AccelerometerTests`/`GyroscopeTests`'
`GetCurrentValuePropertyDoesNotThrowWhenSupported`), identically on plain
`cmake-build-debug` and all three sanitizer presets. ASan: 0 issues. TSan: 38 reports,
all confirmed the same pre-existing, out-of-scope `sharp-runtime`
`System::TimeSpan::copy_count` race (no new findings from the clock-source change).
UBSan: 3 reports, all confirmed the same pre-existing `Vector3`/`Matrix::GetHashCode()`
signed-overflow findings, none in `Microsoft::Devices`. 40/40 clean on the concurrency
loop.

**What failed or could not be run:** the plain, unqualified
`git submodule update --init --recursive` command (see above — not a build blocker, the
project doesn't need those submodules). No Android device/emulator was used this
session (all findings above are host-build-only, consistent with every prior session's
stated hardware limitation). The Android cross-compile itself was **not re-run** this
pass (only the desktop build/tests were, since the clock-source change compiles
identically on Android — `std::chrono::steady_clock` is fully standard, no
platform-specific `#ifdef` needed — but this was not independently re-confirmed via a
fresh Android build in this pass).

**Next recommended task:** `SENSORBASE-008` (validate `TimeBetweenUpdates` min/max —
check the real WP7 API's documented behavior for negative/zero/huge values via an
archived MSDN page, then match it) — small, well-scoped, and a direct, natural
continuation of what this pass already found and tested defensively. Alternatives, if
priority is unclear: the `READINGS-002`-era wrong-visibility question is fully closed,
and the `cna_demo_input` Android build finding from `DEV-BUILD-004` remains open but
unscoped (see Section 4) if Android example-app coverage is ever wanted.

---

## 11. Resume prompt

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
