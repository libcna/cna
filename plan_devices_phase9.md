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

## P9-5: Physical hardware checklist execution

### Resolution

**Files changed:** `docs/devices-hardware-checklist.md` — added a new "Phase 9 execution
results" section (right after the existing Purpose section) marking each of the
brief's 6 hardware cases with an honest, evidence-backed status.

**Actually run, not assumed:**
- Confirmed this container has no accelerometer, gyroscope, haptic device, or
  joystick/gamepad attached by reading `/proc/bus/input/devices` (only keyboard/power/
  lid/sleep/video input nodes present) and confirming no `/dev/input/js*` exists.
- Ran 8 specific tests directly (not just as part of the full suite):
  `AccelerometerTests.GetIsSupportedPropertyDoesNotCrash`/
  `StartOnUnsupportedPlatformThrows`/`GetCurrentValuePropertyThrowsWhenUnsupported`,
  the identical three for `GyroscopeTests`, and
  `VibrateControllerTests.GetIsSupportedPropertyDoesNotCrash`/
  `UnsupportedImpliesEmptyDeviceName` — all 8 passed.
  `GetCurrentValuePropertyThrowsWhenUnsupported` contains its own `GTEST_SKIP()` guard
  that would have skipped itself had this machine genuinely had real hardware — it did
  not skip, which is itself live, positive confirmation of "no hardware here," not an
  assumption.

**Case-by-case result (full detail in the checklist file itself):**
1. Android phone accelerometer — **NOT RUN** (no device, no working emulator — Task P9-4).
2. Android phone gyroscope — **NOT RUN** (same blocker).
3. Android phone vibration — **NOT RUN** for the "buzzes a real motor" claim; the
   "no crash if unsupported" and duration-cap software guarantees ARE verified, on this
   desktop, this session.
4. Desktop without sensors — **VERIFIED**, live, this session (the one case this
   container can actually exercise).
5. Desktop with gamepad — **NOT RUN** (no gamepad/joystick connected to this container).
6. iOS device/toolchain — **NOT RUN**, confirmed unavailable (no toolchain, re-checked
   fresh).

**Net: 1 of 6 cases verified, 5 of 6 not run — each for a concrete, checked reason, not
a vague "can't do hardware here" blanket statement.**

**Tests added:** none — this task runs existing tests as live evidence, it doesn't add
new ones (there's nothing new to test; the existing suite already covers the
software-observable half of every case that's reachable in this environment).

**Remaining risk:** none. No claim of hardware verification beyond what was actually,
physically possible in this container.

## P9-6: DevicesDemo usability pass

### Resolution

**Files changed:** `examples/demo_devices/src/DevicesDemo.hpp`/`.cpp`.

**Gaps found against the brief's exact checklist, each fixed minimally (no
`SpriteFont`/`Content` dependency added — this demo's own header comment deliberately
avoids that, so all new diagnostics go through the existing window-title text channel,
same mechanism the demo already used for Accel/Gyro readings and event counts):**
- **`IsDataValid` was shown nowhere** for either Accelerometer or Gyroscope (only
  `IsSupported`/`State` had on-screen indicators). Added `valid=Y`/`valid=N` to each
  sensor's window-title segment via `getIsDataValidProperty()`.
- **`VibrateController`'s NOXNA device name was never queried or shown.** Added
  `getDeviceNameProperty()`'s result to the title's `Vibrate supported/unsupported (name)`
  segment.
- **No key bindings existed to Start()/Stop() the Accelerometer or Gyroscope
  interactively** — both were `Start()`'d once in the constructor and never touched
  again; only vibration had start/stop keys. Added `HandleSensorToggleInput()`: `A`
  toggles the Accelerometer, `G` toggles the Gyroscope, each wrapped in the same
  `try`/`catch (const System::Exception&)` pattern the constructor already uses. The
  existing per-frame `State` indicator square (already drawn, Ready-green vs.
  Disabled-purple) reflects the toggle with no new visual element needed.
- **No clear "Compass/Motion not supported by SDL backend" message existed** — only a
  red `Unsupported` indicator square and permanently-zero reading bars, which reads as
  "supported but idle," not "will never report data." Added an explicit
  `Compass/Motion: not supported by SDL backend` segment to the window title.

**Build and runtime verification:**
```bash
cmake --build cmake-build-debug --target cna_demo_devices -j"$(nproc)"
# clean
```
This container has a real X11 display (`DISPLAY=:0`) — ran the demo directly (not just
compiled it): the process starts, logs successful `SDL_CreateWindow` and
`EasyGLGraphicsBackend initialized with OpenGL OpenGL ES 3.2 Mesa 25.0.7-2`, runs for
several seconds without crashing (tried twice — once with default video driver, once
with `SDL_VIDEODRIVER=x11` forced, matching the exact env-var convention
`NOXNA.md`'s own Quick Start section uses), and exits cleanly on `SIGTERM` with no error
output either time.

**Honest limitation, not claimed as verified:** attempted to confirm the actual
on-screen rendering and window-title text via `xdotool search`/`getwindowname`,
`xwininfo -root -tree`, and a raw root-window screen capture (`import -window root`) —
**all three failed to find or capture the demo's window** in this specific display
environment (a GNOME Shell/mutter session where the window tree shows only WM-internal
windows, never the demo's own — likely a restricted/virtual display setup that doesn't
expose ordinary top-level windows to standard X11 introspection tools the way a normal
desktop session would). Per this project's own "if you can't test the UI, say so
explicitly rather than claiming success" standard: **the demo is confirmed to build and
run without crashing, using a real display and a real OpenGL ES backend, but the actual
rendered content and window-title text could not be visually confirmed in this
environment.** Re-running the existing `Accelerometer*`/`Gyroscope*`/etc. `CnaTests`
suite (226/224/2 skipped) confirms the demo-only changes didn't affect the library
itself, which is a different, already-covered claim.

**Tests added:** none — this is example/demo code, not covered by the unit test suite;
no dedicated demo tests exist or were added.

**Remaining risk:** low. The added code paths (`getIsDataValidProperty()`,
`getDeviceNameProperty()`, `Stop()`/`Start()` toggling) are all pre-existing, already
publicly-tested library APIs, called through the same try/catch pattern the demo
already used successfully — the risk surface is the demo's own new code, and it's a
small, mechanical addition to an already-working file, verified to compile and run.

## P9-7: Final API/status table

### Resolution

**Files changed:** `AUDIT.md` (new "Final precise status table" section, inserted
before the existing per-class API-completeness table in the `Microsoft::Devices`
section) and `NEXT.md` (new quick-reference table at the top of Section 2, kept in
sync with `AUDIT.md`'s copy).

**Design:** five independent columns per component — API surface, SDL/native runtime,
sanitizer status, Android compile status, physical hardware status — deliberately kept
separate rather than folded into a single verdict, per this task's own "avoid vague
words like 'done' without qualifiers" instruction. Every cell reflects what was actually
run this session or in a prior, cited phase — not an assumption. `System.Device.Location`
is included explicitly as "not implemented, future plan only" so its absence reads as a
documented decision, not an accidental omission.

**Sanity check:** re-ran the Devices-only test filter after the doc-only change — 226
tests, 224 passed, 2 expected skips, unaffected (as expected for a documentation-only
change).

**Tests added:** none — pure documentation.

**Remaining risk:** none — documentation only, no code changed.

## P9-8: Native backend design boundary document

### Resolution

**Files changed:** `docs/devices-native-backend-design.md` (new).

**What was created, and why it's design-only:** consolidated the native-backend
architecture for `Compass`/`Motion` that had previously been scattered across
`plan_devices_phase5.md` Tasks P5-8/P5-9 (prose Android/iOS field mappings) and
`plan_devices_phase6.md` Task P6-8 (`ICompassBackend`/`IMotionBackend` interface
sketch) into a single reference document, and extended the sketch with a common
`IDeviceSensorBackend` base plus `IAccelerometerBackend`/`IGyroscopeBackend` for
architectural symmetry — explicitly documented as never-to-be-implemented, since
`Accelerometer`/`Gyroscope` already have a real, working SDL3 backend. Per this task's
own instruction, **nothing was implemented**: no `.hpp`/`.cpp` file was added or
changed, `Compass.hpp`/`.cpp` and `Motion.hpp`/`.cpp` are untouched, and the document
says so explicitly in its own "Purpose and status" and "Explicitly not part of this
document" sections.

**Contents:** the interface sketch (documentation-only C++, not compiled); the Android
backend path (`SensorManager`, `TYPE_MAGNETIC_FIELD`/`TYPE_ACCELEROMETER`/
`TYPE_ROTATION_VECTOR` for Compass, `TYPE_ROTATION_VECTOR`/`TYPE_GAME_ROTATION_VECTOR`/
`TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION`/`TYPE_GYROSCOPE` for Motion, `GeomagneticField`
for true heading, `SENSOR_STATUS_*` for accuracy/`Calibrate`); the iOS backend path
(`CLLocationManager`/`CLHeading` for Compass, `CMMotionManager`/`CMDeviceMotion` for
Motion); permission/lifecycle notes (Android sensor permissions vs. iOS's
location-permission-for-heading quirk and `NSMotionUsageDescription`, and reuse of
`SensorBase<T>`'s existing disposal machinery rather than inventing a second one); and
an explicit six-step migration plan whose first three steps are exactly "no existing
public API changes," "backend selection is a new private seam added only inside
Compass/Motion," and "the default/no-backend-yet case keeps today's exact
`NotSupported` behavior" — so the plan cannot regress any platform that doesn't get a
native backend.

**Sanity check:** this is a pure documentation addition with no code touched. Re-ran the
Devices-only test filter to confirm — 226 tests, 224 passed, 2 expected skips,
unaffected, as expected.

**Tests added:** none — pure documentation, no behavior to test.

**Remaining risk:** none — no `.hpp`/`.cpp` file was added or changed; `Compass`/`Motion`
behavior is byte-for-byte identical to before this task.

## P9-9: Final acceptance decision

### Status: **Accepted for merge as SDL-backed Devices baseline**

This is the first of the three exact statuses this task's brief allows, chosen because
every one of its stated criteria was actually met this phase, not assumed:

- **Normal tests pass** (Task P9-2): the Devices-only filter is 226 tests, 224 passing,
  2 expected `GTEST_SKIP()`s on hardware-dependent paths this container has no hardware
  for — reproduced fresh this phase, not carried over from Phase 8's own count without
  re-running it. The full `ctest` suite has the same 2 pre-existing, unrelated
  `EasyGL`/`easy-gl` graphics-backend failures every phase since Phase 5 has seen — no
  new failure anywhere in `Microsoft::Devices`.
- **Sanitizer tests pass** (Task P9-3): ASan and UBSan are clean under
  `devices-asan`/`devices-ubsan`. TSan's only finding is the single, already-known,
  out-of-scope `sharp-runtime` race in `System::TimeSpan::TimeSpan(const TimeSpan&)`
  (`TimeSpan.cpp:55`'s unsynchronized `copy_count++` debug counter) — re-confirmed this
  phase by reading the actual stack trace, not just the summary count, to verify both
  racing accesses are at that one sharp-runtime line and touch no `Microsoft::Devices`
  field or lock. Zero new sanitizer findings anywhere in this namespace's own code.
- **Docs are honest** (Tasks P9-1, P9-5, P9-6, P9-7): `AUDIT.md`/`NEXT.md`'s status
  table marks every unverified claim as exactly that — "Not verified" for physical
  hardware, "N/A (stub)" for `Compass`/`Motion`, "Not implemented" for
  `System.Device.Location` — no cell claims more than what was actually run this
  session or a specifically-cited prior phase.
- **Only hardware/native-backend gaps remain** (see the exact list below) — no
  code-level race, leak, use-after-free, or reproducible failing test was found in this
  phase's own fresh audit (Task P9-1 re-verified all six of Phase 8's claims and found
  none of them had regressed or been overstated).

**Exact remaining work** (unchanged in kind from Phase 8, re-confirmed rather than
newly discovered, so none of it is a reason to withhold merge — it's the honestly
documented gap between "SDL-backed baseline" and "fully hardware-verified"):

1. **Physical hardware verification** — Android accelerometer/gyroscope/vibration, a
   real gamepad's rumble motor, and any iOS device are all still **not run**, in any
   session to date, including this one (Task P9-5). No accelerometer, gyroscope,
   haptic device, or gamepad/joystick hardware exists in this container
   (`/proc/bus/input/devices` confirmed this directly this phase). Use
   `docs/devices-hardware-checklist.md` + `cna_demo_devices` whenever real hardware
   becomes available.
2. **Android APK packaging and an actual emulator/device run** — investigated this
   phase (Task P9-4), not merely assumed absent: no CNA build-system integration exists
   connecting SDL3's vendored `android-project` Gradle template to any CNA CMake
   target, and the one configured AVD (`Medium_Phone`, x86_64) fails outright with
   `/dev/kvm` absent (no hardware acceleration available in this container). The
   library itself still cross-compiles cleanly for `arm64-v8a` (Task P9-4's re-run of
   the existing Android CMake toolchain check).
3. **Native backend implementation for `Compass`/`Motion`** — design-only as of this
   phase (Task P9-8, `docs/devices-native-backend-design.md`); no `.hpp`/`.cpp` change
   was made or should be inferred from that document. Both remain permanent, honest
   `SensorState::NotSupported` stubs until a future phase explicitly scopes and
   implements a real Android/iOS backend against that design.
4. **iOS cross-compilation** — still blocked; no Apple toolchain exists in this Linux
   container, re-confirmed unchanged in every phase from Phase 5 through this one.
5. **`DevicesDemo`'s on-screen rendering** — confirmed to build and run without
   crashing on a real X11 display with a real OpenGL ES backend (Task P9-6), but the
   actual rendered window-title text could not be visually confirmed in this specific
   display environment (`xdotool`/`xwininfo`/`import` all failed to find or capture the
   demo's own window). This is a demo/example-code gap, not a library gap.

**Not creating a Phase 10** for the sole purpose of continued iteration: this phase's
own fresh audit (Task P9-1) found zero code-level regressions, and Tasks P9-2/P9-3
reproduced clean test/sanitizer results directly rather than by assumption. Per this
task's own brief, a new phase should only be opened if a future session finds a
concrete code-level issue (a race, leak, use-after-free, or reproducible failing test)
— not to keep re-auditing a codebase that has now been independently re-audited by
Phases 6, 7, 8, and 9 in a row with a shrinking, then zero, list of new findings this
time. The five items above are legitimate next steps for a *future*, differently-scoped
effort (hardware access, a native-backend implementation task, Android packaging work),
not grounds for another hardening phase over the same SDL-backed code.

**Files changed:** none — this section is a decision record, not a code or even a
documentation-elsewhere change (the referenced docs were already updated by their own
tasks above).

**Sanity check:** none needed — no file outside this plan was touched by this task.

**Remaining risk:** none — a decision record carries no behavioral risk of its own.
