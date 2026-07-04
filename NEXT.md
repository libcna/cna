# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS. Branch: `feature/devices`.

**`Microsoft::Devices` hardening is now complete.** `Microsoft::Devices::Sensors`
(`Accelerometer`, `Compass`, `Gyroscope`, `Motion`, their reading/event-args/
exception types) plus `Microsoft::Devices::VibrateController` have a complete
API surface, real thread-safety, real event-path test coverage, a fixed
timestamp bug, verified Android cross-compilation, and a manual hardware
checklist + demo screen for whatever this dev container itself cannot verify.
There is no more open plan for this namespace as of 2026-07-04 — see Section 8
for what a next session should actually pick up.

**Plan history:**
- `plan_devices.md` (31 tasks) — closed.
- `plan_devices_phase2.md` (17 tasks) — closed. Its one open item (Task P2-7,
  Android/iOS build verification) is superseded by `plan_devices_phase4.md`
  Tasks P4-11 (Android, done) / P4-12 (iOS, confirmed still blocked).
- `plan_devices_phase3.md` (12 tasks) — closed.
- `plan_devices_phase4.md` (14 tasks, user-authored hardening plan) —
  **closed, all 14 tasks done** (P4-1–P4-7 on 2026-07-03; P4-8 through
  P4-14 on 2026-07-04). Full task-by-task detail and every Resolution note
  lives in that file; this document only summarizes.

**Important architectural decisions:**
- Public API names/signatures must match XNA 4.0 (or, for `Microsoft::Devices`,
  the documented WP7 SDK) exactly; C# properties become `getXProperty()` /
  `setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the
  shared base for all sensor classes (`CurrentValue`, `IsDataValid`,
  `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`, and an
  `isSupported_` flag gating `CurrentValue`'s `InvalidOperationException`).
- `VibrateController` is a singleton reached via `getDefaultProperty()`. It
  does not derive `SensorBase<T>`/`IDisposable` — it does not follow the
  sensor pattern.
- FNA (the usual local reference tree for XNA behavior) implements **no**
  equivalent of `Microsoft::Devices` (it's WP7-only) — API completeness was
  judged from archived Microsoft Learn "previous-versions" WP7 SDK docs.
- Tests live under `tests/` mirroring the `include`/`src` namespace path
  1:1, using Google Test, one file per class.

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly with the `EASYGL` backend
(`cmake-build-debug`), HEAD at Task P4-14's commit on `feature/devices`
(2026-07-04, not yet pushed). A full top-level build (`cmake --build .`, no
target argument — every example/test target) also builds clean.

**`CNA` (static lib only) also builds clean for Android** (arm64-v8a, NDK
r30, API 24, `SDL_RENDERER` backend, `cmake-build-android/` — new local,
gitignored build dir), verified for the first time in this project's history
(Task P4-11). This surfaced and required fixing 3 pre-existing, unrelated
bugs in the sibling `sharp-runtime` repo — see Section 3.

**iOS cross-compilation confirmed still blocked** (Task P4-12) — no
toolchain of any kind in this Linux container, and unlike Android's missing
NDK (just needed installing), Apple's toolchain fundamentally needs
macOS/Xcode. Don't expect this to spontaneously unblock the way Android did.

**`VULKAN`/`BGFX` have not been re-verified since 2026-07-02** (commit
`8092f6e`) — 14 commits of `Microsoft::Devices` changes have landed since.
`Microsoft::Devices` has never interacted with the graphics backend
(confirmed empirically pre-2026-07-03), so risk is low, but this is asserted,
not re-confirmed. See Section 8, item 1.

**Tests:** last full `ctest` run (`EASYGL`): **1995 tests total, 97%
passing.** The only failures are a fixed set of **64 pre-existing `EasyGL_*`
graphics tests** that cannot run headless (no display/GPU in this dev
environment) — present before `Microsoft::Devices` work began, unrelated to
it. No regressions across the whole session's work (started at 1992 tests,
+3 from this session's new test files).

**Working:**
- Full `Microsoft::Devices::Sensors` namespace: `Accelerometer`/`Gyroscope`
  (real, SDL3-backed), `Compass`/`Motion` (permanent stubs — SDL3 exposes no
  magnetometer API on any platform). All thread-safe, all with real
  event-path test coverage via `NOXNA` synthetic-injection hooks
  (`InjectSyntheticSensorUpdate()`/`SetStartedForTesting()`).
- `Accelerometer`/`Gyroscope` no longer bypass SDL3's own `SDL_INIT_SENSOR`
  ref-counting — each instance's own init/quit calls are balanced 1:1,
  closing a premature cross-class subsystem-teardown race (Task P4-8).
- `Microsoft::Devices::VibrateController` — thread-safe (Task P4-9), with a
  definitive ID-based (not name-matching) gamepad-exclusion filter (Task
  P4-10, `SDL_OpenHapticFromJoystick()`).
- Reading `Timestamp` values are correct wall-clock time (Task P4-7).
- Android cross-compilation of `CNA` verified clean, zero warnings,
  including `Accelerometer.cpp`/`Gyroscope.cpp`'s `#ifdef __ANDROID__`
  landscape-remap code (Task P4-11).
- `docs/devices-hardware-checklist.md` (Task P4-13) and
  `examples/demo_devices/` (Task P4-14, `cna_demo_devices` target) exist for
  whoever eventually runs this on real hardware — see Section 5.

**Does not work / not done yet:**
- `Compass`/`Motion` — permanent `NotSupported` stubs, by design, until SDL3
  gains magnetometer support.
- iOS cross-compilation — see above, likely permanent for this environment.
- `VULKAN`/`BGFX` builds — unverified since 2026-07-02 (see above).
- The Android axis-remap math (`ConvertAndroidAccelerometerToXnaLandscape()`/
  `ConvertAndroidGyroscopeToXnaLandscape()`) compiles clean but is still
  **physically unverified** — no real device/emulator run this session. This
  is exactly what `docs/devices-hardware-checklist.md` and
  `examples/demo_devices/` (`cna_demo_devices`) exist for.

---

## 3. Recent changes

**2026-07-03 — `plan_devices_phase3.md` closed, `plan_devices_phase4.md`
created and Tasks P4-1–P4-7 done.** 3 confirmed real bugs fixed
(`SensorBase<T>.CurrentValue` throw-when-unsupported; `Accelerometer`/
`Gyroscope` shared-state thread-safety; `VibrateController` mutual
exclusion), the `Accelerometer`/`Gyroscope` callback-lifetime use-after-free
window closed via `inFlightCallback_` + the `NOXNA` synthetic-injection test
hooks (Task P4-2), first-ever real event-path tests using those hooks (Tasks
P4-3–P4-6), and the confirmed `Timestamp` bug fixed — readings previously
landed near `0001-01-01` from feeding monotonic `SDL_GetTicksNS()` into a
ticks-since-epoch constructor; now uses
`System::DateTimeOffset::getUtcNowProperty()` (Task P4-7).

**2026-07-04 — Tasks P4-8 through P4-14 done, closing `plan_devices_phase4.md`:**
- **P4-8** — `EnsureSensorSubsystemInitialized()` no longer bypasses SDL3's
  own `SDL_INIT_SENSOR` ref-counting via an `SDL_WasInit()` guard; a
  per-instance `subsystemHeld_` flag balances each instance's own
  init/quit calls 1:1, regardless of what the other sensor class is doing.
  New `tests/Microsoft/Devices/Sensors/SensorSubsystemOwnershipTests.cpp`.
- **P4-9** — `VibrateController`'s file-static `g_haptic`/
  `g_leftRightEffectId` gained a `std::mutex`, locked for the entire body of
  every public method (helpers that touch either variable require the
  caller already holds it, avoiding a non-recursive-mutex deadlock). Skipped
  the task's optional RAII-cleanup half — closing `g_haptic` from a
  destructor at process exit risks touching an already-`SDL_Quit()`-torn-down
  device, which the task explicitly allowed skipping. New
  `ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock` test (8 threads).
- **P4-10** — replaced `VibrateController`'s device-*name*-string gamepad
  exclusion (couldn't distinguish two controllers with identical product
  names) with an ID-based one via `SDL_OpenHapticFromJoystick()`. Confirmed
  safe against `GamePad`'s own SDL usage (`SdlInputBridge.cpp` only opens
  gamepads via `SDL_OpenGamepad()`, never a separate `SDL_OpenJoystick()`).
- **P4-11** — re-checked the environment before assuming still blocked (per
  the task's own instruction) and found `~/Android/Sdk/ndk/` now present.
  Built `CNA` clean for Android (arm64-v8a, API 24) for the first time ever,
  confirmed via `nm` that the `#ifdef __ANDROID__` remap functions are
  actually compiled in. This surfaced 3 unrelated pre-existing bugs in the
  sibling `sharp-runtime` repo (invisible on this project's Linux/GCC build,
  fatal under Android/Clang's stricter warnings-as-errors), fixed there
  (commit `2c49474` on `sharp-runtime`'s `develop` branch — all 8467 tests
  passing): missing `override` on `DateTime`/`DateTimeOffset::GetHashCode()`;
  a missing `<thread>`/`<chrono>` include in `Task.hpp`; a genuinely dead,
  shadowed file-scope `MsPerDay` constant in `TimeOnly.cpp`.
- **P4-12** — confirmed (not assumed) iOS is still blocked: no
  `xcodebuild`/`xcrun`/`osxcross`/anything Apple-toolchain-shaped anywhere on
  this filesystem. Documentation-only.
- **P4-13** — wrote `docs/devices-hardware-checklist.md`: 5 items nothing in
  this headless container can verify (accelerometer/gyroscope axis
  correctness in both landscape rotations, `VibrateController::Start()`
  actually vibrating a phone motor with working intensity scaling,
  `StartLeftRight()` driving two distinct motors, the P4-10 gamepad-exclusion
  filter not competing with `GamePad::SetVibration()`).
- **P4-14** — new `examples/demo_devices/` (`cna_demo_devices` CMake
  target, registered in root `CMakeLists.txt` mirroring `cna_demo_input`'s
  block exactly). `DevicesDemo` constructs all 4 sensor classes, subscribes
  `CurrentValueChanged` on each to track the latest reading + an event
  counter, and draws per-sensor supported/state/event-flash indicators plus
  signed bars for each reading's key vector fields — same colored-rectangle
  style as `InputDemo` (no `SpriteFont`/Content dependency), plus the window
  title updated every 10 frames with exact numeric values (precision
  `InputDemo`'s bars alone can't give, which matters for verifying axis-sign
  correctness against the Task P4-13 checklist). Keyboard bindings `D1`–`D6`
  exercise `Start(TimeSpan)`/the `NOXNA` intensity overload/`StartLeftRight()`
  large-only/small-only/both; `Space` calls `Stop()`. Verified: builds and
  links clean (zero warnings); running it headless fails with the exact same
  `SDL_CreateWindow`/no-GPU error as the pre-existing `cna_demo_input` under
  the same invocation — this dev container's existing limitation, not a bug
  in the new demo.

All of Tasks P4-8–P4-14 individually re-ran the full `ctest` suite after
their change: consistently 1994–1995 tests (growing as new test files were
added), 97% passing, the same 64 pre-existing headless `EasyGL_*` failures
throughout — no regressions at any point in this session.

All work committed on `feature/devices`, not yet pushed. Task-by-task commit
detail (and the exact reasoning behind every non-obvious choice) lives in
`plan_devices_phase4.md`'s per-task Resolution notes — read there first if
`NEXT.md`'s summary raises a question this document doesn't answer.

---

## 4. Current blocker / main problem

**No blocker.** Build and tests are green, nothing is broken, and
`plan_devices_phase4.md` — the plan that was driving all recent work — is
now fully closed.

The most significant known gap is the same one from before this session:
**`VULKAN`/`BGFX` backends haven't been re-verified since commit `8092f6e`
(2026-07-02)**, and 14 commits of `Microsoft::Devices` changes have landed
since. Low risk (this namespace has never touched the graphics backend) but
unconfirmed against current `HEAD`. See Section 8, item 1.

---

## 5. Known bugs and limitations

- **By design, not a bug:** `Compass`/`Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform.
- **Needs verification, low risk:** `VULKAN`/`BGFX` builds — see Section 4.
- **Needs verification, likely permanent:** iOS cross-compilation — no
  Apple toolchain possible in this Linux container (Task P4-12).
- **Needs physical verification:** Android's axis-remap math
  (`ConvertAndroidAccelerometerToXnaLandscape()`/
  `ConvertAndroidGyroscopeToXnaLandscape()`) compiles clean (Task P4-11) but
  the actual tilt-direction correctness has never been observed on real
  hardware. Use `docs/devices-hardware-checklist.md` + `cna_demo_devices`
  (Tasks P4-13/P4-14) when real hardware is available.
- **Needs physical verification:** `VibrateController::Start()`/
  `StartLeftRight()` actually actuating a real phone motor / two distinct
  motors, and the Task P4-10 gamepad-exclusion filter not competing with
  `GamePad::SetVibration()` on a real connected gamepad — same checklist.
- **Accepted, documented limitation:** `Accelerometer`/`Gyroscope`'s
  `Dispose()` would deadlock if a `CurrentValueChanged`/`ReadingChanged`
  handler reentrantly calls `Dispose()` on its own sender from within the
  handler itself. Judged an unusual enough pattern to accept; see Task
  P4-2's Resolution in `plan_devices_phase4.md`.
- **Unverified, low priority, no evidence of an actual bug:**
  `SensorFailedException`'s exact constructor overload signature remains an
  educated guess — its MSDN doc page consistently lacks a Constructors
  table, more consistent with an archival gap than proof it doesn't exist.
  See `plan_devices_phase3.md` Task P3-12.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/   ← XNA WP7 sensor API headers
src/Microsoft/Devices/Sensors/       ← sensor implementations (SDL3-backed)
tests/Microsoft/Devices/Sensors/     ← Google Test suites per class
include/Microsoft/Devices/           ← VibrateController.hpp
src/Microsoft/Devices/               ← VibrateController.cpp
tests/Microsoft/Devices/             ← VibrateControllerTests.cpp
examples/demo_devices/               ← DevicesDemo (Task P4-14, cna_demo_devices target)
docs/devices-hardware-checklist.md   ← manual real-hardware verification steps (Task P4-13)
```

**`SensorBase<T>`** (header-only template) owns `CurrentValue`,
`IsDataValid`, `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`, and
an `isSupported_` flag gating `CurrentValue`'s `InvalidOperationException`.
Concrete sensors override `Start()`, `Stop()`, `Dispose(bool)`, and must
call `setIsSupportedProperty()` once from their constructor. **Do not
restructure this class** — stable, used by production code.

**Invariant:** any class overriding `Dispose(bool)` **must** add `using
SensorBase<T>::Dispose;`, or C++ name-hiding silently breaks the inherited
public no-arg `Dispose()`. This bug has already been found and fixed 4
times across the project's history — don't reintroduce it in any new
sensor class.

**Sensor pattern (real, SDL3-backed — `Accelerometer`/`Gyroscope`):** static
`g_sensor_`/`g_sensorId_` hold the single open SDL sensor handle; static
`instanceCount_` enforces a ≤10 simultaneous-instance limit; static
`eventWatchRegistered_` guards the SDL event filter lifecycle; static
`startedInstances_` is the list the event-watch callback iterates, guarded
by a `static std::mutex mutex_`. Each instance also has a
`bool inFlightCallback_` (guarded by the same mutex) that `Dispose()` waits
on via a shared `static std::condition_variable callbackFinished_` before
letting the object's lifetime end. `ProcessSensorUpdateEvent()` validates
the event belongs to this instance's open device, then delegates to
`DispatchSensorReading()` for the actual conversion+dispatch — this split
lets the `NOXNA` test-only `InjectSyntheticSensorUpdate()`/
`SetStartedForTesting()` hooks exercise the real dispatch path without a
real, opened SDL sensor. `Timestamp` on dispatched readings is always
`System::DateTimeOffset::getUtcNowProperty()` — real wall-clock time.
Each instance's own `subsystemHeld_` flag (Task P4-8) balances its
`SDL_InitSubSystem()`/`SDL_QuitSubSystem()` calls 1:1, independent of
`instanceCount_` — SDL's own internal ref-count aggregates correctly across
instances *and* across `Accelerometer`/`Gyroscope`. **Do not** "fix" this by
building a separate hand-rolled reference counter; SDL already provides one.

**Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`;
`Start()` always throws `SensorFailedException`; still expose the
`Calibrate` event for API completeness even though it's never raised.

**`VibrateController`:** singleton (private default constructor, reached
via `getDefaultProperty()`), no `SensorBase<T>`, no `IDisposable`, lives
directly in `Microsoft::Devices` (not `::Sensors`). Drives SDL3's haptic API
directly; file-static `g_haptic`/`g_leftRightEffectId` guarded by a
`std::mutex` locked for the entire body of every public method (Task P4-9).
Excludes haptic devices that are also connected joysticks/gamepads from
device selection via ID correlation (`SDL_OpenHapticFromJoystick()`, Task
P4-10), not name matching. `Start()`/`StartLeftRight()` correctly stop each
other's SDL effect before starting (Task P3-5).

**`GetTypeName()` invariant:** must return `.`-separated fully-qualified
.NET names (e.g. `"Microsoft.Devices.Sensors.Compass"`), tagged `NOXNA`.
Classes deriving `System::Object` (via `SensorBase<T>`) use the
`GetTypeNameHPP()`/`GetTypeNameCPP(Class, "Name")` macro pair; classes that
don't (e.g. `AccelerometerReading`-style value types) declare a plain
`NOXNA std::string GetTypeName() const;` method instead.

**Boundaries — do not cross:**
- `third_party/SDL` is vendored and has its **own `CLAUDE.md` forbidding
  AI-authored code contributions**. Safe to *read* for research, never edit.
- `sharp-runtime` is a sibling repo under separate, concurrent development —
  its public API can change without notice mid-session (has happened
  before). If a build breaks in a file `Microsoft::Devices` work didn't
  touch, check there first before assuming you broke it. It has its own
  `CLAUDE.md`/`NEXT.md` and its own git history — commits there are
  separate from `cna_devices`'s.
- Do not expand `Microsoft::Devices` scope to camera, radio, or
  phone-call/photo-picker APIs — explicitly out of scope.
- Do not implement sensor fusion in `Motion` — it stays a `NotSupported`
  stub until SDL3 itself gains magnetometer access.

---

## 7. Useful commands

```bash
# Configure (only needed once, or if CMakeCache.txt is stale/points elsewhere):
cmake -S /rv/data/development/github.com/openeggbert/cna_devices \
      -B /rv/data/development/github.com/openeggbert/cna_devices/cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build library:
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build tests:
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Run only Devices/Sensors + VibrateController tests:
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership"

# Run one suite directly:
./cmake-build-debug/CnaTests --gtest_filter="GyroscopeTests*"

# Build the Devices demo screen (Task P4-14):
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices   # needs a real display; SDL_VIDEODRIVER=dummy fails headless (no GPU here), same as cna_demo_input

# Android cross-compile check (Task P4-11's exact repro — NDK now present in
# this container at ~/Android/Sdk/ndk/):
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Cross-platform build verification (Vulkan/BGFX; not re-run since
# 2026-07-02 — see Section 4/8. BGFX's configure step fetches bgfx.cmake
# from GitHub — takes several minutes; both build dirs already exist in
# this checkout under cmake-build-vulkan/cmake-build-bgfx):
cmake --build cmake-build-vulkan --target CNA --target CnaTests -j$(nproc)
cmake --build cmake-build-bgfx   --target CNA --target CnaTests -j$(nproc)
```

No dedicated lint/format tooling is configured for this project as of this
writing.

---

## 8. Next smallest tasks

With `plan_devices_phase4.md` fully closed, there is no standing plan file
driving further `Microsoft::Devices` work. Pick one of these, or ask the user
what the next priority actually is — do not invent new `Microsoft::Devices`
scope without a plan or explicit request (see Section 6's boundaries).

1. **Re-verify `VULKAN`/`BGFX` builds.** Not done since 2026-07-02 (commit
   `8092f6e`); 14 commits of `Microsoft::Devices` changes have landed since.
   Low risk but unverified — see Section 4.
   - Files: none (build-only task).
   - Verify: the "Cross-platform build verification" commands in Section 7;
     spot-run the targeted Devices/Sensors/VibrateController suite on each
     backend afterward.

2. **Physical hardware verification**, if real Android/iOS hardware or a
   rumble-capable gamepad ever becomes available in a session: work through
   `docs/devices-hardware-checklist.md` using `cna_demo_devices` (Task
   P4-14). Not attemptable in this headless container — don't attempt it
   here, just note if the environment changes.

3. **Anything outside `Microsoft::Devices`.** This namespace's hardening
   work is done; the next task is likely a different subsystem entirely.
   Ask before assuming scope.

---

## 9. Do not do yet

- Do not "fix" the SDL sensor subsystem ownership pattern by building a
  separate hand-rolled reference counter — SDL3 already provides one; the
  fix (already applied, Task P4-8) is to stop bypassing it (see Section 6).
- Do not add a synthetic concurrency/thread test for the
  `Accelerometer`/`Gyroscope` event-watch-callback thread-safety fix (Task
  P3-4) or its lifetime-safety follow-up (Task P4-2) — neither can be
  meaningfully exercised without real concurrent hardware events;
  confirming the existing full-suite pass is the only verification this
  environment can give.
- Do not attempt to solve Task P4-2's documented reentrant-`Dispose()`
  deadlock limitation — accepted as out of scope; don't add complexity for
  an unusual pattern without a concrete need.
- Do not refactor or restructure `SensorBase<T>` or `ISensorReading`
  further — stable, used by production code.
- Do not expand `Microsoft::Devices` to camera, radio, or phone-hardware
  APIs (`PhotoCamera`, `CameraButtons`, `PhotoChooserTask`, etc.) — not
  sensor/vibration functionality, explicitly out of scope.
- Do not implement real sensor fusion in `Motion` — keep it a
  `NotSupported` stub until SDL3 gains magnetometer access.
- Do not edit anything under `third_party/SDL` — vendored, has its own
  `CLAUDE.md` forbidding AI-authored contributions; read-only for research.
- Do not assume iOS cross-compilation is still blocked without checking
  first each time — but Android's NDK situation (present as of Task P4-11,
  after being absent repeatedly across this project's history) is a poor
  prior for iOS: Apple's toolchain fundamentally needs macOS/Xcode, which no
  amount of package installation fixes on a Linux container.
- Do not re-attempt to configure `cmake-build-android/` from scratch to
  re-verify Task P4-11 unless something in `Microsoft::Devices` actually
  changed Android-relevant code — it's a verified-clean, one-time compile
  check, not something that needs re-running per unrelated task.
- Do not perform the cross-cutting `GetTypeNameCPP` dot/colon cleanup
  outside `Microsoft::Devices` (`Cue.cpp`, `AudioEngine.cpp`, etc.) — a
  separate, larger, unrelated cleanup outside this plan's scope.
- Do not run `cmake --build` without first checking `CMakeCache.txt` points
  at the correct source directory (this repo has hit stale-cache issues
  before).
- Do not fix bugs discovered in `sharp-runtime` by editing files there
  without also verifying `sharp-runtime`'s own build/tests independently
  (`cd /rv/data/development/github.com/openeggbert/sharp-runtime/build &&
  cmake --build . && ./SharpRuntimeTests`) — it's a separate repo with its
  own `CLAUDE.md` requiring zero warnings and all tests passing before any
  commit there, and its own git history (do not conflate its commits with
  `cna_devices`'s).

---

## 10. Resume prompt

```
Read NEXT.md first.
plan_devices_phase4.md is fully closed — there is no standing Microsoft::Devices
plan left to work through. Ask the user what to work on next, or pick one of
Section 8's items, before inventing new scope.
If given a new task, make one small, verified improvement at a time.
Run the relevant build/test command from Section 7 after each change.
Update NEXT.md after finishing.
```
