# Phase 9 — Physical verification and release gate

Scope: `Microsoft::Devices`/`Microsoft::Devices::Sensors`, Devices tests, `examples/demo_devices`,
and Devices documentation only. Branch `feature/devices`. This phase is explicitly **not**
about adding more code-level hardening — `plan_devices_phase8.md` is treated as useful
history, re-verified rather than assumed, and this phase's job is to build an honest
release gate: reproduce the test/sanitizer matrix, attempt real hardware/Android
packaging, and produce a precise, qualifier-honest status report.

## Audit findings (P9-1)

Re-read every file listed in the brief and verified six specific claims directly
against the current code (not against what Phase 8's own plan doc says) before making
any edit.

1. **`dispatchToken_` prevents the dispatch cleanup guard from touching `this` after a
   self-destroying callback — CONFIRMED, unchanged.** `Detail::SdlSensorSubsystem<TSensor>::
   DispatchToInstances()` (`SdlSensorSubsystem.hpp`) copies `instance->dispatchToken_`
   into a local `token` while `instance` is confirmed alive+started under the lock, and
   the `ScopeExit` cleanup guard captures `[this, token, thisThreadId]` — `this` here is
   the `SdlSensorSubsystem` itself (a process-lifetime singleton, not the `TSensor`
   instance), never the raw instance pointer. `Accelerometer.hpp`/`Gyroscope.hpp` both
   declare `std::shared_ptr<std::vector<std::thread::id>> dispatchToken_;`. Matches
   Task P8-1 exactly as documented.

2. **The documented unsupported boundary for `Accelerometer::CurrentValueChanged`
   self-destroy is still accurate — CONFIRMED, unchanged.** Re-read
   `Accelerometer::DispatchSensorReading()` line by line:
   `setCurrentValueProperty(accelerometerReading);` (raises `CurrentValueChanged`) is
   immediately followed by `if (getIsDataValidProperty() && !ReadingChanged.Empty())` —
   `getIsDataValidProperty()` is evaluated unconditionally (left operand of `&&`),
   touching `this` again regardless of whether `ReadingChanged` has any subscriber. A
   handler that destroys the instance during `CurrentValueChanged` still hits this
   second touch on freed memory. The boundary Task P8-1 documented has not been
   silently fixed or silently reintroduced as a claim — it's exactly as described.

3. **`SensorBase<T>::TimeBetweenUpdates` getter/setter guarded and return by value —
   CONFIRMED, unchanged.** `getTimeBetweenUpdatesProperty()` returns `System::TimeSpan`
   (by value) under `std::lock_guard<std::mutex> lock(mutex_);`; `setTimeBetweenUpdatesProperty()`
   locks around the compare-and-write, releasing before `TimeBetweenUpdatesChanged.Raise()`.

4. **SDL sensor calls still serialized through `Detail::GetGlobalSdlSensorMutex()` —
   CONFIRMED, unchanged.** `Accelerometer::getIsSupportedProperty()`/`Start()`/
   `Dispose(bool)` all acquire it (three call sites grepped and confirmed); the
   lock-proof-parameter overloads from Task P8-3
   (`EnsureSubsystemInitialized`/`OpenDefaultSensorLocked`/`ProbeIsSupported`) are still
   in place in `SdlSensorSubsystem.hpp`.

5. **`VibrateController` does not hold probe-only haptic devices open accidentally —
   CONFIRMED, unchanged.** `getIsSupportedProperty()`/`getDeviceNameProperty()` both
   call `AcquireHapticDeviceForProbe(openedTemporary)` and both call
   `SDL_CloseHaptic(device)` when `openedTemporary` is true, before returning.

6. **The ZIP/submodule caveat is documented honestly — CONFIRMED, unchanged.** Present
   verbatim (Task P7-6) at the top of `docs/devices-build.md` and in `NEXT.md`'s
   Section 7.

**Additional note from reading `NOXNA.md`:** that file is entirely about the
*graphics*/rendering NOXNA extension layer (`CNA_NOXNA` CMake option, `CNA::Graphics`
namespace, PBR/HDR/shadows/Nova-3D) — a completely separate initiative from
`Microsoft::Devices`. It confirms the shared `NOXNA` marker convention
(`CNAHelper.hpp`'s documentation-only macro) but is otherwise irrelevant to this phase's
scope. No conflict: `Microsoft::Devices`'s `NOXNA`-tagged members (e.g.
`SetStartedForTesting()`) are always-compiled XNA-layer additions tagged for
documentation purposes, not gated behind `#ifdef CNA_NOXNA` the way the graphics
extension layer's types are — these are two different, non-conflicting uses of the same
marker, both consistent with `CLAUDE.md`'s definition.

**Conclusion: no concrete bug found in this audit pass.** No code edits made as a
result of P9-1 — matches the task's own instruction ("only edit code if you find a
concrete bug").

---

## Tasks

- P9-1 — Fresh audit before editing (this section)
- P9-2 — Reproduce the normal Devices test matrix locally
- P9-3 — Re-run sanitizer gates
- P9-4 — Android build and APK/demo path
- P9-5 — Physical hardware checklist execution
- P9-6 — DevicesDemo usability pass
- P9-7 — Final API/status table
- P9-8 — Native backend design boundary document
- P9-9 — Final acceptance decision

Each task gets its own `### Resolution` subsection below, plus its own commit where it
produces a file change.

## P9-2: Reproduce the normal Devices test matrix locally

### Resolution

**Blocker check:** `git submodule update --init --recursive` timed out after 2 minutes
(likely a slow remote-verification network round-trip in this container, not a real
problem) — but `git submodule status` immediately confirms all four submodules
(`third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer`,
`vendor/googletest`) are already initialized and checked out at their expected commits
(no `-`/`+` prefix). This repo was never in a fresh-clone state this session — no actual
blocker.

**Exact commands run, in order, exact results:**
```bash
cmake -S . -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
# Configuring done, Generating done — clean, no errors

cmake --build cmake-build-debug --target CNA -j"$(nproc)"
# Built target CNA — clean

cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
# Built target CnaTests — clean

./cmake-build-debug/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
# [==========] 226 tests from 17 test suites ran.
# [  PASSED  ] 224 tests.
# [  SKIPPED ] 2 tests: AccelerometerTests.GetCurrentValuePropertyDoesNotThrowWhenSupported,
#              GyroscopeTests.GetCurrentValuePropertyDoesNotThrowWhenSupported
```

The 2 skips are expected and correct: both `GTEST_SKIP()` themselves specifically
*because* this container has no real accelerometer/gyroscope hardware — that is the
right outcome here, not a failure to investigate.

**No environment blocker found.** Matches Phase 8's final state exactly (226/224/2
skipped) — no drift, no regression, nothing to fix.

**Remaining risk:** none — pure reproduction, no code touched.

## P9-3: Re-run sanitizer gates

### Resolution

All three presets from `CMakePresets.json` (added Task P8-4) configured, built, and ran
successfully against the exact Devices-only filter from the task brief.

**ASan (`devices-asan`):**
```bash
cmake --preset devices-asan && cmake --build --preset devices-asan
./cmake-build-devices-asan/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
```
Result: **226 tests, 224 passed, 2 expected skips, zero ASan reports.**

**TSan (`devices-tsan`):**
```bash
cmake --preset devices-tsan && cmake --build --preset devices-tsan
./cmake-build-devices-tsan/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
```
Result: **226 tests, 224 passed, 2 expected skips; 41 TSan warnings, all reporting the
identical single location:**
```
SUMMARY: ThreadSanitizer: data race /rv/.../sharp-runtime/src/System/TimeSpan.cpp:55 in System::TimeSpan::TimeSpan(System::TimeSpan const&)
```
Read the actual stack trace (not just the summary line) to confirm the classification
rather than assuming it: both racing accesses are at frame `#0`,
`System::TimeSpan::TimeSpan(const TimeSpan&)`, `sharp-runtime/src/System/TimeSpan.cpp:55`
— this is `sharp-runtime`'s own `copy_count++` debug/instrumentation counter, a plain
global incremented on every `TimeSpan` copy with no synchronization of its own. Frame
`#1` in the sample trace is `SensorBase<AccelerometerReading>::SensorBase()` (calling
`setTimeBetweenUpdatesProperty(TimeSpan::FromMilliseconds(2.0))`, which copy-constructs
a `TimeSpan`) and frame `#2` is `Accelerometer::Accelerometer()` — but the *racing
variable itself* is `sharp-runtime`'s own counter, not any `Microsoft::Devices` field or
lock. **Classified as outside `Microsoft::Devices`, confirmed by the stack, not assumed
from the file path alone** — a separate repo with its own `CLAUDE.md`/git history, per
this project's established boundary (`NEXT.md`'s "do not fix bugs discovered in
sharp-runtime..." rule). Identical finding, same count (41, vs. Phase 8's own 41 after
its atomic-counter fix), same single location as Task P8-4's own final TSan run — no
drift, no new race.

**UBSan (`devices-ubsan`):**
```bash
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan
./cmake-build-devices-ubsan/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
```
Result: **226 tests, 224 passed, 2 expected skips, zero UBSan reports.**

All three preset-generated build directories were deleted after verification, matching
Task P8-4's own established practice (they regenerate identically from the presets any
time they're needed again).

**Tests added:** none — pure re-verification.

**Remaining risk:** none. All three sanitizers reproduce Phase 8's final clean state
exactly; the one TSan finding is confirmed (via its actual stack, not assumed) to be the
same pre-existing, out-of-scope `sharp-runtime` issue, unrelated to any
`Microsoft::Devices` code.

## P9-4: Android build and APK/demo path

### Resolution

**Files changed:** `docs/devices-build.md` — added Section 4.1 documenting the exact
investigation and findings below.

**1. Library cross-compile:** re-ran
`cmake --build cmake-build-android --target CNA -j"$(nproc)"` — `ninja: no work to do`
(no source changes since Task P8-8, correctly nothing to rebuild). **Passed.**

**2. APK/demo packaging — investigated, does not exist, not built this task.** Checked
for existing packaging infrastructure: no `AndroidManifest.xml`, no `*.gradle*` file,
no Android-Studio-project directory anywhere in this repo outside the vendored
`third_party/SDL` submodule (confirmed via `find`). `third_party/SDL/android-project/`
*does* ship a complete, working Gradle/Android-Studio template with its own `gradlew`,
and `third_party/SDL/build-scripts/create-android-project.py` can adapt it for a
specific native app — real, usable infrastructure, but **CNA's own build system has no
wiring connecting any of it to any CNA CMake target**. Building that wiring (Gradle
`externalNativeBuild` pointing at CNA's `CMakeLists.txt`, an `AndroidManifest.xml`,
package name/permissions, `SDL_main`/JNI entry-point glue for `cna_demo_devices`
specifically) is real, multi-step engineering work — explicitly out of scope for this
phase per the task's own "do not invent a large Android app framework unless explicitly
scoped" instruction. **Not available; not attempted; documented precisely instead.**

**3. Emulator/device run — actually attempted, real hard failure, not inferred.**
Confirmed present: a JDK, Android SDK `build-tools`/`platforms`, the emulator binary,
and an existing AVD (`Medium_Phone`, x86_64). Launched it for real:
```bash
~/Android/Sdk/emulator/emulator -avd Medium_Phone -no-window -no-audio -gpu swiftshader_indirect -no-snapshot
```
Result: immediate hard failure —
```
ERROR | x86_64 emulation currently requires hardware acceleration!
CPU acceleration status: /dev/kvm is not found: VT disabled in BIOS or KVM kernel module not loaded
```
Confirmed via `ls /dev/kvm` (no such device) and `adb devices` (no device ever
attached) — not a slowness/timeout guess, a genuine, immediate, reproducible failure
specific to this AVD's x86_64 architecture requiring hardware virtualization this
container does not expose. **Failed — real attempt, real blocker, not a theoretical
one.**

**Summary:**
- Library cross-compile: **passed**.
- APK/demo packaging: **not available** (no integration exists; out of scope to build
  here).
- Emulator/device run: **failed** (KVM unavailable, confirmed via a real launch
  attempt).

**Tests added:** none — infrastructure investigation, not a code change.

**Remaining risk:** none introduced. The gap (no APK packaging, no working emulator)
is pre-existing and now precisely documented with real evidence, not just repeated as
an assumption.
