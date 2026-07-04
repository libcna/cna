# NEXT.md — CNA Project Handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3
with a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It preserves
XNA-style public APIs (`Microsoft::Xna::Framework`, `Microsoft::Devices`) while using
modern C++ internally. Targets desktop Linux/Windows/macOS and Android; iOS is planned
but has no toolchain in this environment. Branch: `feature/devices`.

Current development phase: `Microsoft::Devices`/`Microsoft::Devices::Sensors` has been
through nine hardening/audit passes (`plan_devices.md` through `plan_devices_phase9.md`,
all closed). Phase 9 ended with an explicit decision
(`plan_devices_phase9.md` Task P9-9): **Accepted for merge as SDL-backed Devices
baseline.** There is no open plan file for this namespace right now.

**Important architectural decisions:**
- Public API names/signatures match XNA 4.0 (or, for `Microsoft::Devices`, the
  archived WP7 SDK docs — FNA has no equivalent) exactly; C# properties become
  `getXProperty()`/`setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the shared
  base for `Accelerometer`/`Compass`/`Gyroscope`/`Motion` — see Section 6.
- `VibrateController` is a singleton (`getDefaultProperty()`), lives directly in
  `Microsoft::Devices` (not `::Sensors`), does not derive `SensorBase<T>`/`IDisposable`.
- Tests live under `tests/` mirroring the `include`/`src` namespace path 1:1, Google Test.

**Plan history (all closed, no open plan file):** `plan_devices.md` (31 tasks),
`plan_devices_phase2.md` (17), `plan_devices_phase3.md` (12), `plan_devices_phase4.md`
(14), `plan_devices_phase5.md` (14), `plan_devices_phase6.md` (10),
`plan_devices_phase7.md` (7), `plan_devices_phase8.md` (8), `plan_devices_phase9.md`
(9). Each phase's premise was to *not* trust the previous phase's "done" claims and
re-audit from the actual code — several real concurrency/lifetime bugs were found and
fixed this way across Phases 5-8 (see `AUDIT.md` and each plan file's own "Audit
findings" section for the full detail; not repeated here to keep this file concise).

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly under `EASYGL` (`cmake-build-debug`),
`VULKAN`, and `BGFX`. Android cross-compiles cleanly (`arm64-v8a`, NDK r30, API 24,
compile-only — no APK packaging, `CnaTests` itself not cross-compiled). iOS: no
toolchain in this Linux container, confirmed blocked every phase to date.

**Tests:** Devices-only filter is 226 tests — 224 passing, 2 expected `GTEST_SKIP()`s
on hardware-dependent paths (this container has no accelerometer/gyroscope hardware).
Full `ctest` suite has 2 pre-existing, unrelated `EasyGL`/`easy-gl` graphics-backend
failures (not caused by, or fixed by, any Devices work). See Section 7 for commands.

**Sanitizers:** `devices-asan`/`devices-ubsan` presets clean. `devices-tsan`'s only
finding is one pre-existing, out-of-scope race in `sharp-runtime`'s
`System::TimeSpan::TimeSpan(const TimeSpan&)` copy constructor (an unsynchronized debug
counter in a sibling repo) — confirmed via the actual stack trace, not just the summary
line, to touch no `Microsoft::Devices` field or lock.

**Working:** `Accelerometer`/`Gyroscope` — real, SDL3-backed, thread-safe (global SDL
sensor mutex + per-class subsystem mutex), lifetime-safe (dispatch-token fix survives
self-destroy-during-dispatch, verified under ASan). `VibrateController` — real, SDL3
haptic-backed. `examples/demo_devices` (`cna_demo_devices`) builds and runs without
crashing on a real X11/OpenGL display (rendered content itself not visually confirmed
in this session's restricted display environment — see Section 5).

**Not working / not implemented:** `Compass`/`Motion` are permanent, honest
`SensorState::NotSupported` stubs — SDL3 has no magnetometer/fused-orientation API on
any platform. A native-backend design (not implementation) exists at
`docs/devices-native-backend-design.md`. `System.Device.Location` (GPS) is not
implemented and is explicitly out of scope for `Microsoft::Devices::Sensors` — see
`docs/location-future-plan.md`. **No physical hardware verification has ever been done,
in any session** — everything above is verified by code reading, unit tests,
cross-compilation, and sanitizers only.

---

## 3. Recent changes

**`plan_devices_phase9.md` (2026-07-04) — physical verification and release gate, all
9 tasks closed:**
- Fresh audit of Phase 8's six specific claims against current code — none had
  regressed (Task P9-1).
- Reproduced the Devices test matrix and sanitizer gates directly, not from memory
  (Tasks P9-2/P9-3).
- Investigated Android APK packaging/emulator run: SDL3's vendored `android-project`
  Gradle template exists but has no CNA build-system integration; the one configured
  AVD (`Medium_Phone`, x86_64) fails immediately — `/dev/kvm` absent in this container
  (Task P9-4).
- Physically confirmed (via `/proc/bus/input/devices`) this container has zero
  sensor/haptic/gamepad hardware; ran the hardware-dependent unsupported-path tests
  live as evidence (Task P9-5).
- `DevicesDemo`: added `IsDataValid`/`VibrateController` device-name/sensor start-stop
  toggle keys (`A`/`G`) to the window-title diagnostics (Task P9-6,
  `examples/demo_devices/src/DevicesDemo.{hpp,cpp}`).
- Wrote a precise per-component status table into `AUDIT.md`/`NEXT.md` (Task P9-7).
- Consolidated the Compass/Motion native-backend architecture into
  `docs/devices-native-backend-design.md` — design only, nothing implemented
  (Task P9-8).
- Final acceptance decision: **Accepted for merge as SDL-backed Devices baseline**
  (Task P9-9) — normal tests and sanitizers clean, docs honest, only
  hardware/native-backend/Android-packaging/iOS-toolchain gaps remain.

Full task-by-task detail (including exact commands run and every audit finding) is in
`plan_devices_phase9.md`; Phases 5-8's detail is in their own plan files. This section
intentionally does not restate that history.

---

## 4. Current blocker / main problem

**No code-level blocker.** All 9 Devices plan files are closed; Phase 9's own fresh
audit found no regressions and no new bugs. What remains is exclusively
environment/scope-limited:
- No physical Android/iOS device or rumble-capable gamepad in this session.
- No `/dev/kvm` in this container, so the one configured Android emulator AVD cannot
  boot (`x86_64 emulation currently requires hardware acceleration!`).
- No Apple toolchain in this Linux container (iOS cross-compilation impossible here).
- No native (non-SDL) backend implemented for `Compass`/`Motion` — a design exists,
  not code.

None of these are bugs to "fix" in this repo; they're gaps to close only when the
environment or scope changes (see Section 8).

---

## 5. Known bugs and limitations

- **By design, not a bug:** `Compass`/`Motion` are permanent `NotSupported` stubs —
  SDL3 exposes no magnetometer API on any platform.
- **Deliberate, documented limitation:** concurrent `Dispose()` calls on the *same*
  sensor instance from two threads is not guaranteed to give the losing caller a clean
  exception — it blocks until the winner's cleanup finishes, then returns as a silent
  no-op. Shared state is never corrupted; this matches the conventional .NET
  `IDisposable` contract (not required to be thread-safe against concurrent callers).
- **Deliberate, unfixed by design:** destroying (not just `Dispose()`-ing)
  `Accelerometer` specifically from within its own `CurrentValueChanged` handler is
  unsafe — `DispatchSensorReading()` unconditionally touches `this` again afterward to
  decide whether to also raise the legacy `ReadingChanged` event. This is a
  class-design property (`ReadingChanged` is itself a member of `this`), not a
  dispatch-bookkeeping gap; fixing it needs a larger redesign, out of scope so far.
  `Gyroscope` has no second event and is fully safe.
- **Needs verification, likely permanent:** iOS cross-compilation — no Apple toolchain
  possible in this Linux container.
- **Needs physical hardware verification (never done):** Android axis-remap
  tilt-direction correctness beyond the semantic tests already in place;
  `VibrateController::Start()`/`StartLeftRight()` actually actuating a real motor;
  gamepad-exclusion not competing with `GamePad::SetVibration()` on a real controller.
- **Unverified, low priority, no evidence of an actual bug:** `SensorFailedException`'s
  exact constructor overload signature is an educated guess (its MSDN doc page lacks a
  Constructors table).
- **Out of scope, not this repo's bug:** `sharp-runtime`'s `TimeSpan` copy-constructor
  TSan race (see Section 2) — a sibling repo issue, do not fix it from here.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/          ← XNA WP7 sensor API headers
include/Microsoft/Devices/Sensors/Detail/   ← internal-only, never in public headers
src/Microsoft/Devices/Sensors/              ← sensor implementations (SDL3-backed)
tests/Microsoft/Devices/Sensors/            ← Google Test suites per class
include/Microsoft/Devices/                  ← VibrateController.hpp
src/Microsoft/Devices/                      ← VibrateController.cpp
examples/demo_devices/                      ← DevicesDemo (cna_demo_devices target)
docs/devices-hardware-checklist.md          ← manual real-hardware verification steps
docs/devices-build.md                       ← reproducible build/test commands
docs/devices-native-backend-design.md       ← Compass/Motion native backend design (not implemented)
docs/location-future-plan.md                ← why GPS/location isn't here
```

- **`SensorBase<T>`** owns `CurrentValue`, `IsDataValid`, `TimeBetweenUpdates`,
  `CurrentValueChanged`, `Dispose()`. Every field is mutex-guarded; getters return by
  value. Has `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` (protected) — derived
  `Dispose(bool)` overrides must use these, never call the base `Dispose(bool)`
  directly on a losing concurrent call. **Do not restructure further** — stable,
  hardened across 5 phases.
- **Invariant:** any class overriding `Dispose(bool)` must add
  `using SensorBase<T>::Dispose;`, or C++ name-hiding breaks the inherited public
  no-arg `Dispose()`.
- **`Accelerometer`/`Gyroscope`** share `Detail::SdlSensorSubsystem<TSensor>` for
  subsystem/event-watch machinery. Every real SDL sensor call is serialized by a
  process-wide `Detail::GetGlobalSdlSensorMutex()`, nested inside each class's own
  `subsystem.mutex_` (per-class mutex first, global mutex second, never reversed) —
  **not optional**: SDL3 documents `SDL_InitSubSystem()`/`SDL_QuitSubSystem()` as
  main-thread-only/not-thread-safe, and concurrent calls have reproducibly corrupted
  the heap in testing. `EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()`/
  `ProbeIsSupported()` require a `const std::lock_guard<std::mutex>&` parameter as
  compile-time proof the lock is held. Dispatch uses `dispatchToken_` (a
  `std::shared_ptr<std::vector<std::thread::id>>`), copied into the cleanup guard
  *before* invoking the user callback — required so a callback that destroys its own
  instance mid-dispatch doesn't leave the guard touching freed memory (confirmed via
  ASan). `Timestamp` on readings is real wall-clock time
  (`System::DateTimeOffset::getUtcNowProperty()`).
- **Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`; `Start()`
  always throws `SensorFailedException`; still expose `Calibrate` for API completeness.
- **`VibrateController`:** file-static `g_haptic`/`g_leftRightEffectId` guarded by one
  mutex locked for each public method's entire body; RAII destructor closes the haptic
  device and releases `SDL_INIT_HAPTIC`. Excludes haptic devices that are also
  connected gamepads via ID correlation, not name matching.
- **`GetTypeName()` invariant:** returns `.`-separated fully-qualified .NET names
  (e.g. `"Microsoft.Devices.Sensors.Compass"`), tagged `NOXNA`.
- **Boundaries — do not cross:**
  - `third_party/SDL` is vendored with its own `CLAUDE.md` forbidding AI-authored
    contributions — read-only for research.
  - `sharp-runtime` is a sibling repo under separate development, its own git history —
    if a build breaks in a file Devices work didn't touch, check there first.
  - Do not expand `Microsoft::Devices` scope to camera, radio, or
    phone-call/photo-picker APIs.
  - Do not implement sensor fusion in `Motion`, and do not add GPS/location to
    `Microsoft::Devices::Sensors` under any circumstances (including as `NOXNA`) —
    see `docs/location-future-plan.md`.

---

## 7. Useful commands

**ZIP-export caveat:** every command below assumes a real `git clone` with submodules
initialized (`git submodule update --init --recursive`) — a bare source export has
empty `third_party/SDL`/`SDL_image`/`SDL_mixer`/`vendor/googletest` and will not
configure. See `docs/devices-build.md` Section 0.

```bash
# Configure (only if CMakeCache.txt is stale/missing):
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build:
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Devices-only filter (226 tests; see docs/devices-build.md Section 2 for the
# loop-it-20-60x convention before trusting a single pass on new concurrency tests):
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase|ScopeExit"

# Build and run the Devices demo (needs a real display):
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices

# Android cross-compile check (NDK at ~/Android/Sdk/ndk/):
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Vulkan/BGFX:
cmake --build cmake-build-vulkan --target CNA --target CnaTests -j$(nproc)
cmake --build cmake-build-bgfx   --target CNA --target CnaTests -j$(nproc)

# Sanitizer builds (see docs/devices-build.md Section 6 for findings history):
cmake --preset devices-asan && cmake --build --preset devices-asan
cmake --preset devices-tsan && cmake --build --preset devices-tsan
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan
```

No dedicated lint/format tooling is configured for this project.

---

## 8. Next smallest tasks

No open plan file drives further `Microsoft::Devices` work. Pick one, or ask the user
first — do not invent new scope, and do not open a "Phase 10" solely to keep
re-auditing code that four consecutive audits (Phases 6-9) found nothing new in.

1. **Physical hardware verification**, if real Android/iOS hardware or a
   rumble-capable gamepad becomes available. Goal: work through
   `docs/devices-hardware-checklist.md` using `cna_demo_devices`, mark each item
   verified/failed with evidence. Files: none changed unless a real bug is found.
   Verification: the checklist itself, updated with results.
2. **Android APK packaging + CNA CMake integration**, if `/dev/kvm` or a physical
   device becomes available. Goal: connect SDL3's vendored `android-project` Gradle
   template to a CNA build target, package `examples/demo_devices`, run it on a
   device/emulator. Files: new build glue under `examples/demo_devices/` or a new
   `android/` project dir; `docs/devices-build.md` updated with the exact steps.
   Verification: an installed, launchable APK.
3. **Native Compass/Motion backend implementation**, only once explicitly scoped as
   its own task. Goal: implement `ICompassBackend`/`IMotionBackend` for one platform
   per `docs/devices-native-backend-design.md`. Files: new `.hpp`/`.cpp` under
   `include/src/Microsoft/Devices/Sensors/Detail/`, new tests under
   `tests/Microsoft/Devices/Sensors/`. Verification: new unit tests plus the existing
   Devices-only `ctest` filter still green.
4. **Anything outside `Microsoft::Devices`.** Ask before assuming scope.

---

## 9. Do not do yet

- Do not claim `Microsoft::Devices` is "complete" as a flat statement — use Section 2's
  layered breakdown (API vs. SDL runtime vs. native backend vs. hardware-verified).
- Do not restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem<TSensor>`, or the
  `dispatchToken_`/global-mutex locking scheme without a concrete, newly-found bug —
  five phases of hardening already closed the gaps that were actually there.
- Do not fake `Compass`/`Motion` from `Accelerometer`/`Gyroscope` data, and do not add
  GPS/location to `Microsoft::Devices::Sensors` under any circumstances (including as
  `NOXNA`) — see `docs/location-future-plan.md`.
- Do not claim Android/iOS hardware support, or an APK/emulator run, unless it was
  actually done in the current session.
- Do not edit anything under `third_party/SDL` — vendored, has its own `CLAUDE.md`
  forbidding AI-authored contributions.
- Do not fix bugs found in `sharp-runtime` by editing files there directly — it's a
  separate repo with its own build/test/commit process.
- Do not trust a single passing `ctest` run as proof a new concurrency/lifetime change
  is correct — loop it (20-60+ iterations) and/or run it under a sanitizer preset
  first; this exact gap has caused real, previously-undetected bugs in this namespace's
  history (see `docs/devices-build.md` Section 2/6).
- Do not open a new Devices plan/phase file just to keep iterating — only for a
  concrete, newly-found code-level issue.

---

## 10. Resume prompt

```
Read NEXT.md first. Microsoft::Devices is accepted for merge as an SDL-backed
baseline (plan_devices_phase9.md Task P9-9) — there is no open plan file for this
namespace. Do not summarize it as flatly "complete"; use Section 2's layered status.

Inspect only the files needed for the first task you pick from Section 8 (or ask the
user what to prioritize). Do not refactor unrelated code. Make one small, verified
improvement at a time.

Run the relevant build/test command from Section 7 after each change — and if the
change touches concurrency or object lifetime, also loop the test (20-60+ iterations,
docs/devices-build.md Section 2) and/or run it under a sanitizer preset
(devices-asan/devices-tsan/devices-ubsan, Section 6) before trusting it, per Section 9.

Update NEXT.md after finishing, keeping it concise — this file should stay a short,
current-state handoff, not a full phase-by-phase history (that detail belongs in each
plan_devices_phaseN.md file).
```
