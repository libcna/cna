# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS.

**Main goal (current phase):** implement the full `Microsoft::Devices`
namespace — `Microsoft::Devices::Sensors` (Accelerometer, Compass, Gyroscope,
Motion, and their reading/event-args/exception types) plus
`Microsoft::Devices::VibrateController` — matching the Windows Phone 7 XNA
API spec. Branch: `feature/devices`.

**Important architectural decisions:**
- Public API names/signatures must match XNA 4.0 (or, for `Microsoft::Devices`,
  the documented WP7 SDK) exactly; C# properties become `getXProperty()` /
  `setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the
  shared base for all sensor classes (`CurrentValue`, `IsDataValid`,
  `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`).
- `VibrateController` is a singleton reached via `getDefaultProperty()`
  (matches the real WP7 `VibrateController.Default` instance API, fixed
  2026-07-02 in Task P2-14 — it used to be static-only, which was wrong).
  It does not derive `SensorBase<T>`/`IDisposable` — it does not follow the
  sensor pattern.
- FNA (the usual local reference tree for XNA behavior) implements **no**
  equivalent of `Microsoft::Devices` at all (it's WP7-only) — this namespace
  has no local reference tree to diff against; API completeness is judged
  from documented WP7 SDK knowledge instead.
- Tests live under `tests/` mirroring the `include`/`src` namespace path
  1:1, using Google Test, one file per class.

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly with the `EASYGL` backend
(`cmake-build-debug`) as of the last verified build (during Task P2-5 work).
Note: between Task P2-14 and P2-15, `sharp-runtime`
(sibling repo, under concurrent work elsewhere) grew `System::IAsyncResult`
to 4 pure-virtual members and broke
`src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp` (unrelated to
`Microsoft::Devices`); fixed incidentally during P2-15 to unblock the build
— see Section 3.

**Tests:** last full `ctest` run: **1964 tests total, 97% passing.** The only
failures are a fixed set of **64 pre-existing `EasyGL_*` graphics tests**
that cannot run headless (no display/GPU in this dev environment) — present
before this phase began and unrelated to `Microsoft::Devices` work. No
regressions have been introduced across the whole phase.

**Working:**
- Full `Microsoft::Devices::Sensors` namespace: `Accelerometer` (real,
  SDL3-backed — `SDL_SENSOR_ACCEL`, Android landscape axis remap),
  `AccelerometerReading`, `AccelerometerReadingEventArgs` (WP7 7.0 legacy),
  `AccelerometerFailedException`, `SensorFailedException`, `SensorBase<T>`,
  `SensorReadingEventArgs<T>`, `ISensorReading`, `SensorState`,
  `CalibrationEventArgs`, `CompassReading`/`Compass` (stub, see below),
  `GyroscopeReading`/`Gyroscope` (real, SDL3-backed — `SDL_SENSOR_GYRO`),
  `AttitudeReading`, `MotionReading`, `Motion` (stub, see below). All have
  passing test suites.
- `Microsoft::Devices::VibrateController` — singleton (`getDefaultProperty()`),
  SDL3 haptic-backed (`SDL_GetHaptics`/`SDL_OpenHaptic`/`SDL_InitHapticRumble`/
  `SDL_PlayHapticRumble`/`SDL_StopHapticEffects`). Now filters out haptic
  devices that are also connected gamepads, so it can't compete with
  `GamePad::SetVibration` (different SDL3 API path). Full tests.

**Does not work / not done yet:**
- `Compass` and `Motion` are permanent stubs — SDL3 exposes no magnetometer
  API on any platform, so both are always `SensorState::NotSupported` and
  `Start()` always throws. This is by design, not a gap, until SDL3 gains
  magnetometer support.
- Android/iOS cross-compilation has **not** been verified — no Android NDK
  / iOS toolchain is available in this dev container (Task P2-7, blocked).
  Vulkan/BGFX desktop backends **were** verified 2026-07-02 (Task P2-6): all
  139 Devices/Sensors/VibrateController tests pass under both.

---

## 3. Recent changes

- `plan_devices.md` (31 tasks: full `Microsoft::Devices::Sensors` +
  `VibrateController`) — **all 31 tasks complete**; a Status column was
  added to its Task Summary table.
- `AUDIT.md` — added a `Microsoft::Devices::Sensors` / `Microsoft::Devices`
  section (previously entirely missing, since FNA has no equivalent to diff
  against for this namespace).
- `plan_devices_phase2.md` (new) — follow-up plan: API-completeness audit,
  known-bug fixes, `CHECKLIST.md` compliance spot-check, cross-platform
  build verification, and a `VibrateController` review + proposed `NOXNA`
  vibration-API extensions.
- **Task P2-8 (done):** fixed a confirmed real bug —
  `VibrateController::Start()` could open and buzz a connected haptic-capable
  gamepad on desktop instead of safely no-opping, because `SDL_GetHaptics()`
  enumerates such controllers independently of the
  `GamePad::SetVibration`/`SDL_RumbleGamepad` path (confirmed by reading the
  vendored SDL3 Linux haptic backend,
  `third_party/SDL/src/haptic/linux/SDL_syshaptic.c`). Fix: added
  `IsConnectedGamepadHapticDevice()` in
  `src/Microsoft/Devices/VibrateController.cpp`, which skips haptic devices
  whose name matches a connected joystick before opening one. Files changed:
  `include/Microsoft/Devices/VibrateController.hpp` (doc comment),
  `src/Microsoft/Devices/VibrateController.cpp`,
  `tests/Microsoft/Devices/VibrateControllerTests.cpp` (explanatory note —
  no new test possible without real gamepad hardware).
- Bug found (not yet fixed): `Compass`, `Gyroscope`, and `Motion` all needed
  a `using SensorBase<T>::Dispose;` declaration added, because declaring
  `Dispose(bool) override` hides the inherited public no-arg `Dispose()`
  (C++ name-hiding). The identical bug exists in `Accelerometer.hpp` and is
  still unfixed (see Section 4/5).
- **Task P2-2 (done, 2026-07-02):** independent verification of
  `Microsoft::Devices::Sensors`/`VibrateController` API completeness against
  archived Microsoft Learn "previous-versions" MSDN pages (high confidence)
  plus one MonoGame cross-check for `SensorState`'s enum values (medium
  confidence). Confirmed the vast majority of the existing implementation
  matches the real WP7 Mango SDK exactly (full per-class verdicts in
  `AUDIT.md`). Found four real gaps, each promoted to a new task in
  `plan_devices_phase2.md` Phase 7: `VibrateController`'s static-only shape
  should be an instance API via a `Default` singleton (P2-14);
  `Accelerometer.ReadingChanged` (legacy WP7 7.0 event) was never wired up
  (P2-15); `SensorFailedException` is missing `ErrorId` (P2-16); and
  `getStateProperty()` on `Compass`/`Gyroscope`/`Motion` isn't in the
  documented API for those three classes and likely needs a `NOXNA` tag
  (P2-17). Research-only task — no source files changed, only
  `plan_devices_phase2.md`, `AUDIT.md`, and this file.
- **Task P2-3 (done, 2026-07-02):** fixed `Accelerometer.hpp`'s `Dispose()`
  C++ name-hiding bug (added `using SensorBase<AccelerometerReading>::Dispose;`,
  copying the pattern already used in `Compass`/`Gyroscope`/`Motion`) and
  wrote `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` (7 tests,
  modeled on `GyroscopeTests.cpp`) — `Accelerometer` was the last sensor
  class with zero test coverage. Files changed:
  `include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
  `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` (new). Build +
  full `ctest` verified: 1942 tests total, same 64 pre-existing headless
  `EasyGL_*` failures, no regressions.
- **Task P2-4 (done, 2026-07-02):** fixed `Accelerometer.cpp`'s
  `GetTypeNameCPP` to use the dot-separated .NET name convention
  (`"Microsoft.Devices.Sensors.Accelerometer"`, was `::`-separated);
  added a `GetTypeName` test case to `AccelerometerTests.cpp`. Files changed:
  `src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
  `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`. Build + full
  `ctest` verified: 1943 tests total, same 64 pre-existing headless
  `EasyGL_*` failures, no regressions.
- **Task P2-14 (done, 2026-07-02):** fixed `VibrateController`'s API shape
  from fully static to the real WP7 instance-via-singleton pattern.
  `getDefaultProperty()` (new, mirrors
  `Microsoft::Xna::Framework::Audio::Microphone::getDefaultProperty()`'s
  existing pattern in this codebase) returns a never-null singleton pointer;
  `Start(const System::TimeSpan&)`/`Stop()` are now instance methods
  (`VibrateController::getDefaultProperty()->Start(...)`); the default
  constructor is private (`= default`, was `= delete`d). `Start()` now
  throws `System::ArgumentOutOfRangeException` for `duration` outside the
  closed interval `[TimeSpan::Zero, TimeSpan::FromSeconds(5)]`, instead of
  silently clamping. Files changed:
  `include/Microsoft/Devices/VibrateController.hpp`,
  `src/Microsoft/Devices/VibrateController.cpp`,
  `tests/Microsoft/Devices/VibrateControllerTests.cpp` (rewritten for the
  new shape, 10 tests, up from 6). Build + full `ctest` verified: 1947 tests
  total, same 64 pre-existing headless `EasyGL_*` failures, no regressions.
- **Task P2-15 (done, 2026-07-02):** wired up `Accelerometer.ReadingChanged`,
  the real (if `[Obsolete]`-tagged) WP7 7.0 legacy event using the
  already-built `AccelerometerReadingEventArgs`. Added
  `System::EventHandler<AccelerometerReadingEventArgs> ReadingChanged;` to
  `Accelerometer.hpp`; raised it from the end of `ProcessSensorUpdateEvent()`
  right after the existing `CurrentValueChanged`-raising call, same
  X/Y/Z/Timestamp data, same `getIsDataValidProperty()`/`.Empty()` guard
  pattern. Added `AccelerometerTests.cpp::ReadingChangedSubscriptionDoesNotThrow`
  (can only confirm no-crash + `Start()`/`Stop()` behavior with a subscriber
  attached — actually observing the event fire needs real hardware, same
  limitation as `VibrateControllerTests`' gamepad-conflict scenario). Files
  changed: `include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
  `src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
  `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`.
  **Also fixed incidentally (unrelated to `Microsoft::Devices`):**
  `sharp-runtime` grew `System::IAsyncResult` to 4 pure-virtual members
  since the P2-14 session, breaking
  `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp`'s internal
  `SelectorResult`/`ContainerResult` helpers (`CNA` is one static-library
  target, so this blocked verifying P2-15 at all). Fixed minimally: both
  classes' `void* asyncState` became `std::any asyncState`, plus a new
  pre-signaled `System::Threading::EventWaitHandle` member and the two new
  `IAsyncResult` overrides — no behavior change, these results were already
  always synchronously completed. No `Storage` tests exist to regression-check
  against this specific fix. Build + full `ctest` verified: 1948 tests total,
  same 64 pre-existing headless `EasyGL_*` failures, no regressions.
- **Task P2-16 (done, 2026-07-02):** added `ErrorId` to `SensorFailedException`,
  matching the real API (MSDN `hh239104`). Added `SharpRuntime::intcs errorId_`
  + `getErrorIdProperty()` + a new `(const char* str, SharpRuntime::intcs
  errorId)` constructor overload; existing message-only constructor kept
  unchanged (defaults `ErrorId` to `0`) for source compat. Confirmed
  `AccelerometerFailedException` does inherit `SensorFailedException`
  (visible directly in its `.hpp` — the earlier "unverified" note from
  Task P2-2 was overly cautious) and added the matching overload there too.
  Existing throw sites across the sensor classes intentionally left
  unchanged (still message-only, so `ErrorId` reads `0`) — no real WP7
  error-code values are documented anywhere; inventing numbers would be
  guesswork. Files changed:
  `include/Microsoft/Devices/Sensors/SensorFailedException.hpp`,
  `src/Microsoft/Devices/Sensors/SensorFailedException.cpp`,
  `include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp`,
  `src/Microsoft/Devices/Sensors/AccelerometerFailedException.cpp`,
  `tests/Microsoft/Devices/Sensors/SensorFailedExceptionTests.cpp`,
  `tests/Microsoft/Devices/Sensors/AccelerometerFailedExceptionTests.cpp`
  (3 new tests each). Build + full `ctest` verified: 1954 tests total, same
  64 pre-existing headless `EasyGL_*` failures, no regressions.
- **Task P2-17 (done, 2026-07-02):** resolved `getStateProperty()` on
  `Compass`/`Gyroscope`/`Motion`. Per the task's own step 1 ("spend a little
  more effort"), fetched each class's exact authoritative "type exposes the
  following members" page directly (`Compass` `hh220912`, `Gyroscope`
  `hh239201`, `Motion` `hh239189`) instead of relying on the earlier
  absence-based inference — all three list exactly the same 4 properties
  (`CurrentValue`/`IsDataValid`/`IsSupported`/`TimeBetweenUpdates`), no
  `State`, definitively confirming the gap (upgraded from "medium-high
  confidence" to confirmed). Tagged `getStateProperty()` `NOXNA` on all
  three headers with a Doxygen note (CNA symmetry extension mirroring
  `Accelerometer`'s real `State`) — kept, not removed, since existing
  code/tests depend on it. `Gyroscope.hpp` needed a new
  `#include "CNA/CNAHelper.hpp"` for the `NOXNA` macro (`Compass.hpp`/
  `Motion.hpp` already had it). No behavior change, no new tests needed.
  Files changed: `include/Microsoft/Devices/Sensors/Compass.hpp`,
  `include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
  `include/Microsoft/Devices/Sensors/Motion.hpp`. Build + full `ctest`
  verified: 1954 tests total (unchanged), same 64 pre-existing headless
  `EasyGL_*` failures, no regressions. **This closes out all four gaps
  found by Task P2-2's audit** — P2-14/P2-15/P2-16/P2-17 are all done.
- **Tasks P2-10 through P2-13 (done, 2026-07-02):** implemented all of Phase
  6's `VibrateController` NOXNA extensions, as instance methods on the
  `getDefaultProperty()` singleton (per the Phase 7 ordering note — not
  free-standing statics). `Start(const System::TimeSpan&, float intensity)`
  (P2-10): existing `Start(TimeSpan)` now delegates to it with `1.0f`, zero
  behavior change; `intensity` clamped via `std::clamp`.
  `getIsSupportedProperty()`/`getDeviceNameProperty()` (P2-11): share a new
  private `AcquireHapticDeviceForProbe()` helper that reuses `g_haptic` if
  already open, otherwise opens+closes a temporary device — never holds one
  open as a side effect of probing; empirically confirmed (via a standalone
  throwaway probe program, not committed) that both report
  "unsupported"/`""` in this dev container, i.e. genuinely no haptic
  hardware here. `StartLeftRight(float largeMotor, float smallMotor, const
  System::TimeSpan&)` (P2-12): new file-static `g_leftRightEffectId`
  (SDL's own `-1` "no effect" sentinel) tracks the uploaded
  `SDL_HAPTIC_LEFTRIGHT` effect; gated on `SDL_GetHapticFeatures()` support
  (silent no-op otherwise); `Stop()` now also calls
  `SDL_DestroyHapticEffect()` on it (`SDL_StopHapticEffects()` alone stops
  playback but doesn't free the uploaded effect slot). Added 10 new tests
  (P2-13) — one deliberate deviation from the plan's exact text: rather than
  hardcoding `EXPECT_FALSE(getIsSupportedProperty())`, the test only asserts
  no-crash (mirrors `Accelerometer`/`Gyroscope`'s branch-on-live-result
  convention, since haptic availability is a genuine hardware question like
  theirs, not a permanent stub-by-design like `Compass`/`Motion`'s missing
  magnetometer API — hardcoding `false` would be a latent flake on any
  environment that does have haptic hardware). Files changed:
  `include/Microsoft/Devices/VibrateController.hpp`,
  `src/Microsoft/Devices/VibrateController.cpp`,
  `tests/Microsoft/Devices/VibrateControllerTests.cpp`. Build + full `ctest`
  verified: 1964 tests total (up from 1954), same 64 pre-existing headless
  `EasyGL_*` failures, no regressions. Task P2-9 also closed as a natural
  byproduct — the reasoning it asked to confirm is now stated directly in
  P2-10's Doxygen comment, no separate code change needed. **This closes
  out all of Phase 6.**
- **Task P2-5 (done, 2026-07-02):** `CHECKLIST.md` compliance spot-check
  across all 32 files in `Microsoft::Devices` (17 headers + 15 `.cpp`).
  SPDX headers, `CNAHelper.hpp` presence, no-bare-`///`, Doxygen
  completeness, and `GetTypeName()` dot-convention were all verified
  compliant — no functional bugs found, matching this task's own prediction
  ("expect it to mostly confirm compliance"). Two minor, pre-existing style
  issues fixed: `ISensorReading.hpp`/`SensorState.hpp` had a stale
  `@note Status: Partial.` doc-comment line (removed — both were already
  confirmed ✅ complete by Task P2-2); `AccelerometerFailedException.hpp`
  used a bare relative include and a stray `};` instead of
  `} // namespace ...` (normalized to match every sibling file). Files
  changed: `include/Microsoft/Devices/Sensors/ISensorReading.hpp`,
  `include/Microsoft/Devices/Sensors/SensorState.hpp`,
  `include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp`.
  Build + full `ctest` verified: 1964 tests total (unchanged), same 64
  pre-existing headless `EasyGL_*` failures, no regressions.
- **Task P2-6 (done, 2026-07-02):** verified Vulkan/BGFX desktop builds pick
  up `Microsoft::Devices` correctly. Configured `cmake-build-vulkan`
  (`find_package(Vulkan)` found the system SDK, 1.4.309) and
  `cmake-build-bgfx` (`FetchContent`-pulled `bgfx.cmake` from GitHub — a
  genuinely slow ~6-minute configure + long build, since bgfx is a large
  codebase; both ran in the background). `CNA` + `CnaTests` build clean
  under both. Targeted `ctest -R
  "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController"`:
  **139/139 pass under both backends** — identical to the `EASYGL` baseline,
  zero regressions. Full-suite counts differ from `EASYGL` as
  expected/unrelated (Vulkan 1899/1912, BGFX 1903/1906 — every failure in
  both is a missing graphics demo/smoke-test executable not built by the
  `CNA`/`CnaTests` targets, e.g. `cna_demo_2d`, `cna_test_vulkan_instanced`,
  `cna_test_bgfx_render_target_usage` — none `Microsoft::Devices`-related).
  `cmake-build-vulkan`/`cmake-build-bgfx` left on disk (gitignored, like
  `cmake-build-debug`). **This closes out Task P2-6** — only Task P2-7
  (Android/iOS, blocked — no toolchain in this environment) remains in
  Phase 5.

Full per-class implementation history (constructors, tests added, etc.) for
all 31 `plan_devices.md` tasks is in `git log` and the plan files themselves
— not repeated here to keep this document short.

---

## 4. Current blocker / main problem

No blocker prevents work from continuing.

**Resolved (2026-07-02, Task P2-3):** `Accelerometer.hpp`'s `Dispose()`
name-hiding bug (declaring `Dispose(bool) override` without `using
SensorBase<AccelerometerReading>::Dispose;`, which hid the inherited public
no-arg `Dispose()`) is fixed — the one-line `using` declaration was added,
copying the exact pattern already used in `Compass.hpp`/`Gyroscope.hpp`/
`Motion.hpp`. `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` was
also written (7 tests, modeled on `GyroscopeTests.cpp`): `getIsSupportedProperty()`
doesn't crash, constructor succeeds, `getStateProperty()` reflects support
status, `Start()` throws `AccelerometerFailedException` when unsupported,
`Stop()` doesn't crash, `Dispose()`/second-`Dispose()`-throws, and the 11th
simultaneous instance throws `SensorFailedException`. All 7 pass; the
`DisposeSucceedsAndSecondDisposeThrows` test specifically exercises the
no-arg `Dispose()` call that would not have compiled before this fix. Full
`ctest` confirms no regressions: 1942 tests total (up from 1935), same 64
pre-existing headless `EasyGL_*` failures.

**Resolved (2026-07-02, Task P2-14):** `VibrateController`'s API shape —
static-only, wrong — is now a singleton reached via `getDefaultProperty()`,
with `Start`/`Stop` as instance methods, matching the real WP7
`VibrateController.Default.Start(...)` pattern. Out-of-range `Start()`
duration now throws `System::ArgumentOutOfRangeException` instead of
silently clamping. See Section 5.

**Resolved (2026-07-02, Task P2-15):** `Accelerometer.ReadingChanged` (the
real, `[Obsolete]`-tagged WP7 7.0 legacy event) is now wired up, raised
alongside `CurrentValueChanged`.

**Resolved (2026-07-02, Task P2-16):** `SensorFailedException` (and
`AccelerometerFailedException`) now has `getErrorIdProperty()`, matching
the real API.

**Resolved (2026-07-02, Task P2-17):** `getStateProperty()` on
`Compass`/`Gyroscope`/`Motion` is now tagged `NOXNA` — definitively
confirmed (each class's exact authoritative member-list page fetched
directly) that the real API has no `State` on those three. This closes out
all four gaps found by Task P2-2's audit.

**Resolved (2026-07-02, Tasks P2-10–P2-13, P2-9):** Phase 6's
`VibrateController` NOXNA extensions are all implemented —
variable-intensity `Start()` overload, `getIsSupportedProperty()`/
`getDeviceNameProperty()` capability introspection, and `StartLeftRight()`
dual-motor rumble, all as instance methods on the singleton.

**Resolved (2026-07-02, Task P2-5):** `CHECKLIST.md` compliance spot-check
across all 32 `Microsoft::Devices` files found no functional bugs — only two
minor pre-existing style issues, both fixed (see Section 3).

**Resolved (2026-07-02, Task P2-6):** Vulkan/BGFX desktop builds verified —
`Microsoft::Devices` compiles identically and all 139
Devices/Sensors/VibrateController tests pass under both, matching the
`EASYGL` baseline exactly.

The only remaining `Microsoft::Devices` work is Task P2-7 (Android/iOS
cross-compilation) — blocked in this environment, no NDK/iOS toolchain
available; flag for a different environment/CI. Every other task in
`plan_devices.md` and `plan_devices_phase2.md` is done.

---

## 5. Known bugs and limitations

- **Fixed (2026-07-02, Task P2-3):** `Accelerometer.hpp` `Dispose()`
  name-hiding (see Section 4) and the missing `AccelerometerTests.cpp` are
  both resolved.
- **Partially fixed:** `GetTypeNameCPP(...)` NAME-string convention was
  inconsistent across the codebase — some files use dot-separated .NET-style
  names (the documented invariant, see Section 6), others use `::`. **Fixed
  in `Microsoft::Devices` scope (2026-07-02, Task P2-4):** `Accelerometer.cpp`
  now uses `"Microsoft.Devices.Sensors.Accelerometer"`; every class in this
  namespace now uses the dot convention correctly. **Still inconsistent
  outside `Microsoft::Devices`** (`Cue.cpp`, `AudioEngine.cpp`,
  `SoundBank.cpp`, `WaveBank.cpp`, `DateTime.cpp`, `DateTimeOffset.cpp` —
  grep `GetTypeNameCPP` to find all) — a separate, larger, cross-cutting
  cleanup outside this phase's scope, not started.
- **By design, not a bug:** `Compass` and `Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform.
- **By design, not a bug:** `VibrateController::Start(const System::TimeSpan&)`
  (the XNA-compliant overload) always rumbles at full strength (`1.0f`) —
  matches the real WP7 API, which has no intensity concept (WP7-era
  vibration motors were single-intensity on/off). **Fixed/extended
  (2026-07-02, Task P2-10):** a `NOXNA Start(const System::TimeSpan&, float
  intensity)` overload now exists for variable intensity; the XNA-compliant
  overload delegates to it with `1.0f`, so its own behavior is unchanged.
- **Accepted limitation:** `VibrateController`'s gamepad-exclusion filter
  (Task P2-8, Section 3) matches by device name; two physically distinct
  controllers reporting an identical product name would both be
  excluded/included together. Judged too rare to justify a more invasive
  fix (would require opening every connected joystick just to probe it).
- **Verified (2026-07-02, Task P2-6):** Vulkan/BGFX desktop backends —
  `cmake-build-vulkan` and `cmake-build-bgfx` configured fresh (neither
  existed before, so no stale-cache issue to worry about), `CNA` +
  `CnaTests` build clean under both, all 139
  Devices/Sensors/VibrateController tests pass under both (identical count
  to the `EASYGL` baseline). Full-suite failure counts differ from `EASYGL`
  as expected (Vulkan: 13 failures, BGFX: 3 — both entirely
  missing-executable failures for graphics demo/smoke-test binaries not
  built by the `CNA`/`CnaTests` targets, unrelated to `Microsoft::Devices`).
- **Still blocked:** Android/iOS cross-compilation (Task P2-7) — no NDK/iOS
  toolchain available in this dev container; flag for a different
  environment/CI. `Accelerometer.cpp`/`Gyroscope.cpp`'s `#ifdef __ANDROID__`
  branches remain unverified by any compiler.
- **Fixed (2026-07-02, Task P2-14):** `VibrateController` was implemented
  fully static; the real WP7 API is an instance API — static `Default`
  singleton property + instance `Start`/`Stop`
  (`VibrateController.Default.Start(...)`). Now fixed: `getDefaultProperty()`
  returns a never-null singleton pointer, `Start`/`Stop` are instance
  methods. Also fixed: `Start(TimeSpan)` now throws
  `System::ArgumentOutOfRangeException` for `duration` outside the closed
  interval `[TimeSpan::Zero, TimeSpan::FromSeconds(5)]`, instead of silently
  clamping. Phase 6 (P2-10–P2-13, not yet implemented) should add its NOXNA
  extensions as instance methods on this corrected singleton.
- **Fixed (2026-07-02, Task P2-15):** `Accelerometer.ReadingChanged` (the
  real, `[Obsolete]`-tagged WP7 7.0 legacy event using the already-built
  `AccelerometerReadingEventArgs`) was never wired onto `Accelerometer`. Now
  raised alongside `CurrentValueChanged` from `ProcessSensorUpdateEvent()`.
- **Incidental, unrelated (2026-07-02, found/fixed during Task P2-15):**
  `sharp-runtime` grew `System::IAsyncResult` to 4 pure-virtual members,
  breaking `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp`'s
  internal `SelectorResult`/`ContainerResult` helpers. Fixed minimally
  (`std::any asyncState` + a pre-signaled `EventWaitHandle` member) just to
  unblock the `CNA` build — see Section 3. `StorageDevice.cpp` is the only
  `IAsyncResult` implementer in this codebase (confirmed via grep), so this
  was the only site affected; no `Storage` tests exist, so nothing would
  have otherwise caught this.
- **Fixed (2026-07-02, Task P2-16):** `SensorFailedException` (and by
  extension `AccelerometerFailedException`) was missing the real API's
  `ErrorId` property. Now has `getErrorIdProperty()` and a
  `(const char*, SharpRuntime::intcs)` constructor overload; existing throw
  sites still use the message-only constructor (`ErrorId` reads `0` there),
  since no real WP7 error-code values are documented.
- **Fixed (2026-07-02, Task P2-17):** `getStateProperty()` on
  `Compass`/`Gyroscope`/`Motion` isn't real WP7 API — definitively confirmed
  via each class's exact authoritative member-list page (upgraded from
  Task P2-2's "medium-high confidence" absence-based inference). Now tagged
  `NOXNA` on all three (kept, not removed — existing code/tests depend on
  it; it's a harmless symmetry extension mirroring `Accelerometer`'s real
  `State`).
- **Resolved (2026-07-02):** independent verification of
  `Microsoft::Devices::Sensors`'s API completeness against archived WP7
  Mango SDK docs (Task P2-2) is done — see `AUDIT.md` for full per-class
  verdicts and the four gaps above. A few details remain unverified (no
  authoritative source found) but are not treated as bugs: `ISensorReading.
  Timestamp`/`SensorReadingEventArgs<T>.SensorReading` exact naming,
  `AccelerometerFailedException : SensorFailedException` inheritance, and
  CNA's own `MaxSensorCount = 10` cap (not documented either way in the
  real API).

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
`IsDataValid`, `TimeBetweenUpdates`, `CurrentValueChanged`, and `Dispose()`.
Concrete sensors override `Start()`, `Stop()`, and `Dispose(bool)`.

**Invariant — must not be forgotten again:** any class overriding
`Dispose(bool)` **must** add `using SensorBase<T>::Dispose;`, or C++
name-hiding silently breaks the inherited public no-arg `Dispose()`. This
exact bug has been found (and fixed) three times already (`Compass`,
`Gyroscope`, `Motion`) and is still present, unfixed, in `Accelerometer.hpp`.

**Sensor pattern (real, SDL3-backed — `Accelerometer`/`Gyroscope`):** static
`g_sensor_`/`g_sensorId_` hold the single open SDL sensor handle; static
`instanceCount_` enforces a ≤10 simultaneous-instance limit; static
`eventWatchRegistered_` guards the SDL event filter lifecycle. `Start()`
opens the sensor and registers the SDL event watch; `Stop()` unregisters;
`Dispose(bool)` stops, decrements the counter, and closes the sensor handle
when the last instance is disposed. `ProcessSensorUpdateEvent()` runs from
the SDL event filter on every `SDL_EVENT_SENSOR_UPDATE`, with an
Android-specific landscape axis remap (duplicated per-class, not shared —
see each `.cpp`'s `ConvertAndroid*ToXnaLandscape()`).

**Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`;
`Start()` always throws `SensorFailedException`; still expose the
`Calibrate` event for API completeness even though it's never raised.

**`VibrateController`:** singleton (private default constructor, reached via
`getDefaultProperty()`, fixed 2026-07-02 in Task P2-14 — used to be
static-only with a `= delete`d constructor, which didn't match the real WP7
API shape), no `SensorBase<T>`, no `IDisposable`, lives directly in
`Microsoft::Devices` (not `::Sensors`). Drives SDL3's haptic API directly
rather than the sensor pattern; underlying state (`g_haptic`) stays a
file-static implementation detail regardless of the public instance shape.
As of Task P2-8, deliberately excludes haptic devices that are also
connected joysticks/gamepads from device selection, to avoid competing with
`GamePad::SetVibration` (a separate SDL3 subsystem — `SDL_RumbleGamepad` on
an `SDL_Gamepad*`, unrelated to the generic `SDL_Haptic*` API). As of Tasks
P2-10–P2-13, also has `NOXNA` extensions — variable-intensity `Start()`,
`getIsSupportedProperty()`/`getDeviceNameProperty()` (share a private
`AcquireHapticDeviceForProbe()` helper), and `StartLeftRight()` dual-motor
rumble (tracks its uploaded `SDL_HAPTIC_LEFTRIGHT` effect via a new
file-static `g_leftRightEffectId`, destroyed in `Stop()`) — all instance
methods on the singleton, all following the same silent-no-op philosophy as
the XNA-compliant `Start`/`Stop`.

**`GetTypeName()` invariant:** must return `.`-separated fully-qualified
.NET names (e.g. `"Microsoft.Devices.Sensors.Compass"`), tagged `NOXNA`.
Classes deriving `System::Object` (via `SensorBase<T>`) use the
`GetTypeNameHPP()`/`GetTypeNameCPP(Class, "Name")` macro pair; classes that
don't (e.g. `AccelerometerReading`-style value types) declare a plain
`NOXNA std::string GetTypeName() const;` method instead. `GetHashCode()`
returns `std::size_t` for these value types (not the `int` used by
`System::Object::GetHashCode()`).

**Boundaries — do not cross:**
- `third_party/SDL` is vendored and has its **own `CLAUDE.md` forbidding
  AI-authored code contributions** to that project. It is safe (and useful)
  to *read* for research (this is how the P2-8 fix was verified), but never
  edit.
- Do not restructure `SensorBase<T>` or `ISensorReading` — stable, used by
  production code.
- Do not expand `Microsoft::Devices` scope to camera, radio, or
  phone-call/photo-picker APIs — explicitly out of scope (not sensor or
  vibration functionality).
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
```

---

## 8. Next smallest tasks

**Every locally-executable task in `plan_devices.md` and
`plan_devices_phase2.md` is done as of 2026-07-02.** The only open item in
either plan is `Task P2-7` (Android/iOS cross-compilation), which is
blocked in this environment — no Android NDK or iOS toolchain is available
here. There is nothing left to pick up locally from those two plans; P2-7
needs a different environment or CI that has the toolchains.

**`plan_devices_phase3.md` (new, 2026-07-02) has fresh open work.** A third,
deeper research pass (three parallel angles: API-completeness re-audit,
line-by-line implementation review cross-checked against vendored SDL3
source, and test-coverage gap analysis) found real issues the earlier
passes didn't catch:
- **P3-1**: `SensorBase<T>::CurrentValue` never throws when the sensor is
  unsupported — real API throws `InvalidOperationException`.
- **P3-2**: the 5 reading types' setters are public where the real API has
  `internal set` — a judgment-call task (fixing it properly breaks existing
  property-pair tests; recommended resolution is documenting it as an
  accepted C++ deviation, not a code change).
- **P3-4**: `Accelerometer`/`Gyroscope`'s shared sensor state
  (`startedInstances_` etc.) is read/written from the SDL event-watch
  callback with zero synchronization — SDL's own docs warn that callback
  may run on a different thread. Real race risk, especially on Android.
- **P3-5**: `VibrateController::Start()`/`StartLeftRight()` don't cancel
  each other's SDL effects — both can physically vibrate at once.
- **P3-6 through P3-11**: a cluster of test-coverage gaps (zero
  `CurrentValueChanged` coverage anywhere, missing `GetTypeName()` tests on
  3 of 4 sensor classes, untested `Calibrate` event, dispose-then-11th
  never verified, `GetHashCode()` never testing the different-hash case,
  plus smaller boundary gaps).
- **P3-3, P3-12**: documentation-only follow-ups / low-priority research.

See `plan_devices_phase3.md` for full task detail. Recommended starting
point: **P3-1** (clearest real bug, low risk) or **P3-4** (highest
severity, but no new tests possible — concurrency bugs can't be unit-tested
headless).

1. **Task P2-7 — Android/iOS cross-compilation**
   - Goal: `Accelerometer.cpp`/`Gyroscope.cpp` both have `#ifdef __ANDROID__`
     landscape axis-remap branches that have never been compiled — only the
     non-Android branch has been build-verified (across `EASYGL`, `VULKAN`,
     and `BGFX`, all Linux desktop). Confirm they compile cleanly under the
     Android NDK toolchain, and manually verify on a physical device or
     emulator with real accelerometer/gyroscope hardware that
     `ProcessSensorUpdateEvent()`'s orientation remap produces sensible
     values in both `sensorLandscape` rotations (`ROTATION_90` and
     `ROTATION_270`) — this can't be verified by unit tests alone since it
     depends on live sensor + display-orientation state.
   - Not actionable here; needs an environment/CI with the NDK/iOS toolchain.

---

## 9. Do not do yet

- Do not refactor or restructure `SensorBase<T>` or `ISensorReading` —
  stable, used by production code.
- Do not perform the cross-cutting `GetTypeNameCPP` dot/colon cleanup outside
  `Microsoft::Devices` (`Accelerometer.cpp` was already fixed, Task P2-4,
  done) — the rest touches unrelated files (`Cue.cpp`, `AudioEngine.cpp`,
  etc.) and needs its own scoped plan.
- Do not expand `Microsoft::Devices` to camera, radio, or phone-hardware
  APIs (`PhotoCamera`, `CameraButtons`, `PhotoChooserTask`, etc.) — not
  sensor/vibration functionality, explicitly out of scope.
- Do not implement real sensor fusion in `Motion` — keep it a
  `NotSupported` stub until SDL3 gains magnetometer access.
- Do not edit anything under `third_party/SDL` — vendored, has its own
  `CLAUDE.md` forbidding AI-authored contributions; read-only for research.
- Do not attempt Android/iOS cross-compilation in this environment — no
  NDK/toolchain is available; that verification needs a different
  environment or CI.
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
