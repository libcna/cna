# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS. Branch: `feature/devices`.

**Main goal (current phase):** hardening the `Microsoft::Devices` namespace —
`Microsoft::Devices::Sensors` (`Accelerometer`, `Compass`, `Gyroscope`,
`Motion`, their reading/event-args/exception types) plus
`Microsoft::Devices::VibrateController`. The API surface itself is complete
and has been for several plans now; current work is thread-safety, real
event-path test coverage, a confirmed timestamp bug, and cross-platform
verification — not API completeness.

**Plan history:**
- `plan_devices.md` (31 tasks) — closed.
- `plan_devices_phase2.md` (17 tasks) — closed except Task P2-7
  (Android/iOS build verification), blocked by missing toolchain in this
  dev container.
- `plan_devices_phase3.md` (12 tasks) — closed. All 3 confirmed real bugs
  fixed, 1 decision task resolved, all 6 test-coverage tasks filled, 1
  low-priority research task partially resolved (no known bug either way).
- `plan_devices_phase4.md` (14 tasks, user-authored hardening plan) — **open**.
  Tasks P4-1 through P4-7 done (2026-07-03). Tasks P4-8 through P4-14 not
  started.

**Important architectural decisions:**
- Public API names/signatures must match XNA 4.0 (or, for `Microsoft::Devices`,
  the documented WP7 SDK) exactly; C# properties become `getXProperty()` /
  `setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the
  shared base for all sensor classes (`CurrentValue`, `IsDataValid`,
  `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`, and — since
  `plan_devices_phase3.md` Task P3-1 — an `isSupported_` flag gating
  `CurrentValue`'s `InvalidOperationException`).
- `VibrateController` is a singleton reached via `getDefaultProperty()`. It
  does not derive `SensorBase<T>`/`IDisposable` — it does not follow the
  sensor pattern.
- FNA (the usual local reference tree for XNA behavior) implements **no**
  equivalent of `Microsoft::Devices` (it's WP7-only) — API completeness was
  judged from archived Microsoft Learn "previous-versions" WP7 SDK docs,
  fetched directly per-class, not from memory.
- Tests live under `tests/` mirroring the `include`/`src` namespace path
  1:1, using Google Test, one file per class.

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly with the `EASYGL` backend
(`cmake-build-debug`) as of the last verified build, HEAD `ed6c931`
(2026-07-03, pushed). **`VULKAN`/`BGFX` have not been re-verified since
2026-07-02 (commit `8092f6e`)** — 9 commits of `Microsoft::Devices` changes
have landed since, across all of `plan_devices_phase3.md` and Tasks
P4-1–P4-7. Nothing in those changes should affect backend-specific
compilation (`Microsoft::Devices` has never interacted with the graphics
backend, confirmed empirically pre-2026-07-03), but this is asserted, not
re-confirmed since. See Section 8.

**Tests:** last full `ctest` run (`EASYGL`): **1992 tests total, 97%
passing.** The only failures are a fixed set of **64 pre-existing
`EasyGL_*` graphics tests** that cannot run headless (no display/GPU in
this dev environment) — present before `Microsoft::Devices` work began,
unrelated to it. No regressions across the whole session's work.

**Working:**
- Full `Microsoft::Devices::Sensors` namespace: `Accelerometer` (real,
  SDL3-backed — `SDL_SENSOR_ACCEL`, Android landscape axis remap, untested
  on real Android hardware), `Gyroscope` (real, SDL3-backed —
  `SDL_SENSOR_GYRO`), `Compass`/`Motion` (permanent stubs — see below),
  plus their reading/event-args/exception types. All have passing test
  suites, including (since Tasks P4-3–P4-6) real event-path tests that
  actually observe `CurrentValueChanged`/`ReadingChanged` fire with
  correct data via a synthetic-injection test hook (`InjectSyntheticSensorUpdate()`/
  `SetStartedForTesting()`, `NOXNA`, Task P4-2).
- `Microsoft::Devices::VibrateController` — singleton
  (`getDefaultProperty()`), SDL3 haptic-backed. XNA-compliant
  `Start(TimeSpan)`/`Stop()`, plus `NOXNA` extensions: variable intensity,
  capability introspection, dual-motor rumble (`StartLeftRight`). Filters
  out haptic devices that are also connected gamepads (currently via
  fragile device-name matching — see Section 5) so it can't compete with
  `GamePad::SetVibration`. `Start()`/`StartLeftRight()` correctly cancel
  each other's SDL effect (Task P3-5).
- `Accelerometer`/`Gyroscope`'s shared static sensor state is guarded
  against the SDL event-watch callback running off-thread (Task P3-4), and
  the callback-vs-`Dispose()` use-after-free window that fix left open is
  now also closed (Task P4-2).
- Reading `Timestamp` values are now correct wall-clock time (Task P4-7) —
  previously always landed near `0001-01-01` due to a monotonic-vs-epoch
  ticks mixup.

**Does not work / not done yet:**
- `Compass` and `Motion` are permanent stubs — SDL3 exposes no magnetometer
  API on any platform, so both are always `SensorState::NotSupported` and
  `Start()` always throws. By design, not a gap, until SDL3 gains
  magnetometer support.
- Android/iOS cross-compilation has **never** been verified — no toolchain
  in this dev container.
- `VULKAN`/`BGFX` builds not re-run since before this session's Devices
  hardening work (see above).
- SDL sensor subsystem ownership between `Accelerometer`/`Gyroscope` has a
  confirmed, unfixed bug (Task P4-8).
- `VibrateController` is not yet thread-safe (Task P4-9), and its
  gamepad-exclusion filter still uses name matching despite a concrete
  fix being known (Task P4-10).
- No demo/manual-verification screen exists for `Microsoft::Devices` yet
  (Task P4-14).

---

## 3. Recent changes

- **`plan_devices_phase3.md` fully closed (2026-07-03):** all 3 confirmed
  real bugs fixed (`SensorBase<T>.CurrentValue` throw when unsupported;
  `Accelerometer`/`Gyroscope` shared-state thread-safety mutex;
  `VibrateController` `Start()`/`StartLeftRight()` mutual exclusion); 1
  decision task resolved (the 5 reading types' setters made `private` +
  `friend`, matching real WP7 `internal set` — user picked this over the
  zero-code-change default when asked); 6 test-coverage tasks filled
  (`CurrentValueChanged`/`Calibrate` subscription tests, `GetTypeName()`
  gaps, dispose-decrement verification, `GetHashCode()` different-hash
  cases, and a bundle of smaller gaps); 1 documentation-only task
  confirmed already satisfied; 1 low-priority research task partially
  resolved (`CalibrationEventArgs` confirmed correct against its real MSDN
  page; `SensorFailedException`'s exact constructor signature stays an
  educated guess — likely an archival gap in Microsoft's docs, not
  evidence the constructor doesn't exist).
- **`CLAUDE.md` gained a "Git Commits" rule:** commit immediately after
  finishing each task rather than waiting for an explicit request, staging
  only that task's files by name.
- **`plan_devices_phase4.md` created (2026-07-03):** user-authored
  8-priority hardening plan, formalized into 14 tasks across 8 phases.
  Two SDL3 facts confirmed during research changed the shape of 2 tasks:
  SDL3 already ref-counts `SDL_InitSubSystem`/`SDL_QuitSubSystem`
  internally (Task P4-8's real fix is to stop bypassing that, not build a
  second counter); SDL3 has `SDL_OpenHapticFromJoystick()` for direct
  ID-based haptic↔gamepad correlation (Task P4-10).
- **Tasks P4-1 through P4-7 done (2026-07-03):**
  - **P4-1** — doc cleanup: confirmed `plan_devices_phase2.md`/
    `plan_devices_phase3.md` were not actually stale; added a clear
    "baseline complete" statement and regrouped known limitations.
  - **P4-2** — closed the `Accelerometer`/`Gyroscope` callback-lifetime
    use-after-free window Task P3-4 explicitly left open, via a
    per-instance `inFlightCallback_` flag + `condition_variable` that
    `Dispose()` waits on. Added the `NOXNA` test-only synthetic-event hooks.
    Known accepted limitation: a handler that reentrantly `Dispose()`s its
    own sender from within its own callback would deadlock — documented,
    not solved.
  - **P4-3–P4-6** — first tests in this codebase to actually observe
    `CurrentValueChanged`/`ReadingChanged` fire with real data, using the
    new hooks. `Stop()`'s effect on further dispatch also verified.
  - **P4-7** — fixed the confirmed `Timestamp` bug: `Accelerometer`/
    `Gyroscope` readings' `Timestamp` was built from `SDL_GetTicksNS()`
    (monotonic ns since SDL init) fed into a `DateTime(ticks)` constructor
    expecting ticks since the .NET epoch — always produced a bogus
    near-`0001-01-01` value. Now uses
    `System::DateTimeOffset::getUtcNowProperty()`; the now-dead
    `timestampNs` parameter was removed entirely from the call chain.
- All work committed and pushed. Last pushed commit: `ed6c931` on
  `feature/devices`.

---

## 4. Current blocker / main problem

**No blocker.** Build and tests are green, nothing is broken.

The most significant known gap: **`VULKAN`/`BGFX` backends haven't been
re-verified since commit `8092f6e` (2026-07-02)**, and 9 commits of
`Microsoft::Devices` changes have landed since (all of
`plan_devices_phase3.md` plus Tasks P4-1–P4-7). `Microsoft::Devices` has
never interacted with the graphics backend in any way this project has
observed, so the risk of an actual break is low — but it's asserted from
prior-session evidence, not re-confirmed against current `HEAD`. No command
has been run against those build directories since.

**What has been tried:** nothing yet — this is a re-verification gap, not
an active failure.

---

## 5. Known bugs and limitations

- **Confirmed bug, not yet fixed:** SDL sensor subsystem ownership conflict
  between `Accelerometer`/`Gyroscope`. `EnsureSensorSubsystemInitialized()`
  bypasses SDL3's own built-in subsystem ref-counting via an
  `SDL_WasInit()` guard, so one class's last-instance `Dispose()` can
  prematurely `SDL_QuitSubSystem()` the sensor subsystem while the other
  class still has active instances expecting it alive. See
  `plan_devices_phase4.md` Task P4-8 for the full root-cause writeup and
  recommended fix.
- **Confirmed gap, not yet fixed:** `VibrateController`'s
  `g_haptic`/`g_leftRightEffectId` have zero thread-safety (no mutex),
  unlike the sensor classes (`Accelerometer`/`Gyroscope` fixed in Tasks
  P3-4/P4-2). See Task P4-9.
- **Confirmed gap, not yet fixed:** `VibrateController`'s gamepad-exclusion
  filter matches by device *name* string, which can misidentify distinct
  controllers reporting identical product names. A concrete fix exists —
  `SDL_OpenHapticFromJoystick()`, confirmed present in the vendored SDL3 —
  but is not yet implemented. See Task P4-10.
- **Accepted, documented limitation:** `Accelerometer`/`Gyroscope`'s
  `Dispose()` would deadlock if a `CurrentValueChanged`/`ReadingChanged`
  handler reentrantly calls `Dispose()` on its own sender from within the
  handler itself (same thread, mid-callback). Judged an unusual enough
  pattern to accept rather than solve further; see Task P4-2's Resolution
  in `plan_devices_phase4.md`.
- **Needs verification:** `VULKAN`/`BGFX` builds — see Section 4.
- **Needs verification:** Android/iOS cross-compilation — no NDK/toolchain
  in this dev container. `Accelerometer.cpp`/`Gyroscope.cpp`'s
  `#ifdef __ANDROID__` branches have never been compiled by any compiler.
  See `plan_devices_phase2.md` Task P2-7 (blocked) and
  `plan_devices_phase4.md` Tasks P4-11/P4-12.
- **By design, not a bug:** `Compass` and `Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform.
- **Unverified, low priority, no evidence of an actual bug:**
  `SensorFailedException`'s exact constructor overload signature remains
  an educated guess — its MSDN doc page (and its subclass
  `AccelerometerFailedException`'s) consistently lacks a Constructors
  table across 2 doc-family generations, more consistent with a systematic
  archival gap than proof the constructor doesn't exist. See
  `plan_devices_phase3.md` Task P3-12.
- **Incomplete:** no demo/manual-verification screen exists yet for
  `Microsoft::Devices` (no way to visually/interactively confirm sensor
  axis conventions or actual vibration on real hardware). See
  `plan_devices_phase4.md` Task P4-14.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/   ← XNA WP7 sensor API headers
src/Microsoft/Devices/Sensors/       ← sensor implementations (SDL3-backed)
tests/Microsoft/Devices/Sensors/     ← Google Test suites per class
include/Microsoft/Devices/           ← VibrateController.hpp
src/Microsoft/Devices/               ← VibrateController.cpp
tests/Microsoft/Devices/             ← VibrateControllerTests.cpp
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
letting the object's lifetime end — this closes the use-after-free window
between the (possibly off-thread) SDL event-watch callback and
concurrent disposal (Tasks P3-4 + P4-2). `ProcessSensorUpdateEvent()`
validates the event belongs to this instance's open device, then delegates
to `DispatchSensorReading()` for the actual conversion+dispatch — this
split (Task P4-2) lets the `NOXNA` test-only
`InjectSyntheticSensorUpdate()`/`SetStartedForTesting()` hooks exercise the
real `CurrentValueChanged`/`ReadingChanged` dispatch path without a real,
opened SDL sensor. `Timestamp` on dispatched readings is always
`System::DateTimeOffset::getUtcNowProperty()` (Task P4-7) — real
wall-clock time, not derived from any SDL-supplied value.

**Known unfixed issue in this pattern:** `EnsureSensorSubsystemInitialized()`
bypasses SDL3's own subsystem ref-counting (see Section 5 / Task P4-8) —
do not "fix" this by adding a separate hand-rolled reference counter; the
correct fix is to stop working around SDL's own mechanism.

**Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`;
`Start()` always throws `SensorFailedException`; still expose the
`Calibrate` event for API completeness even though it's never raised.

**`VibrateController`:** singleton (private default constructor, reached
via `getDefaultProperty()`), no `SensorBase<T>`, no `IDisposable`, lives
directly in `Microsoft::Devices` (not `::Sensors`). Drives SDL3's haptic
API directly, file-static `g_haptic`/`g_leftRightEffectId` state
(currently unsynchronized — Task P4-9). Excludes haptic devices that are
also connected joysticks/gamepads from device selection, currently via
device-name string matching (Task P4-10 has a concrete, better fix).
`Start()`/`StartLeftRight()` correctly stop each other's SDL effect before
starting (Task P3-5).

**`GetTypeName()` invariant:** must return `.`-separated fully-qualified
.NET names (e.g. `"Microsoft.Devices.Sensors.Compass"`), tagged `NOXNA`.
Classes deriving `System::Object` (via `SensorBase<T>`) use the
`GetTypeNameHPP()`/`GetTypeNameCPP(Class, "Name")` macro pair; classes that
don't (e.g. `AccelerometerReading`-style value types) declare a plain
`NOXNA std::string GetTypeName() const;` method instead.

**Boundaries — do not cross:**
- `third_party/SDL` is vendored and has its **own `CLAUDE.md` forbidding
  AI-authored code contributions**. Safe to *read* for research (this is
  how every SDL-behavior claim in this document and in
  `plan_devices_phase3.md`/`plan_devices_phase4.md` was verified), never edit.
- `sharp-runtime` is a sibling repo under separate, concurrent development —
  its public API can change without notice mid-session (has happened
  before). If a build breaks in a file `Microsoft::Devices` work didn't
  touch, check there first before assuming you broke it.
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
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController"

# Run one suite directly:
./cmake-build-debug/CnaTests --gtest_filter="GyroscopeTests*"

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

1. **Re-verify `VULKAN`/`BGFX` builds.** Not done since 2026-07-02
   (commit `8092f6e`); 9 commits of `Microsoft::Devices` changes have
   landed since. Low risk but unverified — see Section 4.
   - Files: none (build-only task).
   - Verify: the "Cross-platform build verification" commands in Section
     7; spot-run the targeted Devices/Sensors/VibrateController suite on
     each backend afterward.

2. **Task P4-8 — SDL sensor subsystem ownership.** Stop
   `EnsureSensorSubsystemInitialized()` from bypassing SDL3's own
   subsystem ref-counting via an `SDL_WasInit()` guard; add a
   per-instance `bool subsystemHeld_` flag so each instance's own
   `SDL_InitSubSystem()`/`SDL_QuitSubSystem()` calls stay balanced 1:1,
   letting SDL's own ref-count correctly aggregate across instances and
   across `Accelerometer`/`Gyroscope`. Full design in
   `plan_devices_phase4.md` Task P4-8.
   - Files: `Accelerometer.hpp`/`.cpp`, `Gyroscope.hpp`/`.cpp`.
   - Verify: full `ctest --output-on-failure` (hard to assert on SDL's
     internal ref-count directly headless; focus on no regressions plus
     any test that can at least exercise the code path safely).

3. **Task P4-9 — `VibrateController` thread-safety.** Add a
   `static std::mutex` guarding `g_haptic`/`g_leftRightEffectId`,
   mirroring the sensor classes' Task P3-4 pattern. Consider RAII cleanup
   for `g_haptic` alongside (see plan for the destruction-order caveat).
   - Files: `src/Microsoft/Devices/VibrateController.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="VibrateControllerTests*"`,
     then full `ctest --output-on-failure`.

4. **Task P4-10 — Replace name-matching gamepad exclusion.** Use
   `SDL_OpenHapticFromJoystick()` (confirmed present in vendored SDL3) to
   correlate haptic devices with connected joysticks by ID instead of by
   name string.
   - Files: `src/Microsoft/Devices/VibrateController.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="VibrateControllerTests*"`.

5. **Tasks P4-11–P4-14 — cross-platform build and demo screen.** Likely
   still blocked for Android/iOS in this environment (re-check whether a
   toolchain has become available before assuming so). The demo screen
   (P4-14, mirroring `examples/demo_input`'s `Game`-subclass pattern) has
   no environment blocker and can be picked up independently at any time.

---

## 9. Do not do yet

- Do not attempt to "fix" Task P4-8 by building a separate hand-rolled
  reference counter — SDL3 already provides one; the fix is to stop
  bypassing it (see Section 6).
- Do not add a synthetic concurrency/thread test for the
  `Accelerometer`/`Gyroscope` event-watch-callback thread-safety fix
  (Task P3-4) or its lifetime-safety follow-up (Task P4-2) — neither can
  be meaningfully exercised without real concurrent hardware events;
  confirming the existing full-suite pass is the only verification this
  environment can give.
- Do not attempt to solve Task P4-2's documented reentrant-`Dispose()`
  deadlock limitation — accepted as out of scope; don't add complexity
  for an unusual pattern without a concrete need.
- Do not refactor or restructure `SensorBase<T>` or `ISensorReading`
  further — stable, used by production code.
- Do not expand `Microsoft::Devices` to camera, radio, or phone-hardware
  APIs (`PhotoCamera`, `CameraButtons`, `PhotoChooserTask`, etc.) — not
  sensor/vibration functionality, explicitly out of scope.
- Do not implement real sensor fusion in `Motion` — keep it a
  `NotSupported` stub until SDL3 gains magnetometer access.
- Do not edit anything under `third_party/SDL` — vendored, has its own
  `CLAUDE.md` forbidding AI-authored contributions; read-only for research.
- Do not attempt Android/iOS cross-compilation in this environment without
  first checking whether a toolchain has actually become available — it
  has not been, repeatedly, across this project's history.
- Do not perform the cross-cutting `GetTypeNameCPP` dot/colon cleanup
  outside `Microsoft::Devices` (`Cue.cpp`, `AudioEngine.cpp`, etc.) — a
  separate, larger, unrelated cleanup outside this plan's scope.
- Do not run `cmake --build` without first checking `CMakeCache.txt` points
  at the correct source directory (this repo has hit stale-cache issues
  before).

---

## 10. Resume prompt

```
Read NEXT.md first.
Then inspect only the files listed for the first task in section 8.
Do not refactor unrelated code.
Make one small, verified improvement (implement or test one class at a time).
Run the relevant build/test command from section 7 after each change.
Update NEXT.md after finishing.
```
