# Building and Testing `Microsoft::Devices` — Reproducible Commands

Every command below was actually run in a session working on `Microsoft::Devices`
(`plan_devices_phase4.md`/`plan_devices_phase5.md`/`plan_devices_phase6.md`/
`plan_devices_phase7.md`/`plan_devices_phase8.md`), not copy-pasted from
documentation without running it. Where a command's success is asserted, it was
verified in this repository, on this branch, this session.

**ZIP-export caveat (Task P7-6):** every claim in this document describes a real
`git clone` of this repository with submodules initialized and the sibling repositories
below present (Section 0) — it does **not** describe, and should not be read as
implying anything about, a bare ZIP/tarball export of this source tree. A raw source
snapshot without `git submodule update --init` having been run has empty
`third_party/SDL` (and `SDL_image`/`SDL_mixer`, `vendor/googletest`) directories
and will not configure, let alone build or pass any test below.

## 0. Fresh clone / submodule setup

**Re-verified 2026-07-06 (`DEV-BUILD-001`) from an actually fresh clone** — every prior
version of this section was written and re-confirmed only in environments that already
had the sibling repositories below pre-provisioned, so the sibling-repo requirement had
never actually been tested/documented until this pass. All timings below were measured
directly in this pass, not estimated.

**Step 1 — sibling repositories.** `sharp-runtime` and `easy-gl` (which itself needs
`meta-gl`) are **separate git repositories that must be cloned next to this repo's own
directory** — they are plain sibling checkouts referenced via `add_subdirectory(../x)`
in `CMakeLists.txt`, **not** git submodules of this repo, so `git submodule update
--init` cannot fetch them. Confirmed by reproducing the failure from a genuinely fresh
clone with no siblings present: CMake now fails fast with an actionable
`FATAL_ERROR` naming the missing sibling and the exact fix (previously a generic
"`add_subdirectory` given source ... which is not an existing directory" with no
indication this was a required, separate clone at all — Task `DEV-BUILD-001` fixed
this). From this repo's own parent directory:

```bash
git clone https://github.com/openeggbert/sharp-runtime.git
git clone https://github.com/openeggbert/easy-gl.git
git clone https://github.com/openeggbert/meta-gl.git
```

Layout expected (all four as siblings under one parent directory):

```text
<parent>/
├── cna_devices/      (this repo)
├── sharp-runtime/
├── easy-gl/
└── meta-gl/
```

`meta-gl` has no further sibling or submodule dependencies of its own (confirmed by
inspecting its `CMakeLists.txt`/`.gitmodules` directly) — this three-repo chain is the
full transitive closure for an `OPENGLES3`-backend build. `sharp-runtime` has its own
separate `vendor/googletest` submodule, handled automatically by its own build, not
something this repo's setup needs to touch directly.

**Step 2 — this repo's own git submodules.** This repository vendors SDL3 (and
`SDL_image`/`SDL_mixer`) as git submodules under `third_party/`, plus `googletest`
under `vendor/` — confirmed via `.gitmodules` and `cmake/ThirdPartySDL.cmake` (which
hard-fails with `FATAL_ERROR` and prints the exact fix if a submodule is missing,
rather than silently doing something else). A fresh clone needs:

```bash
git submodule update --init
```

**Deliberately non-recursive** (Task `DEV-BUILD-001` corrected this from an earlier,
misleading `--recursive` suggestion): `SDL_image`/`SDL_mixer` each carry their own
further nested `external/*` submodules (AVIF, JXL, WebP, libpng, GME, mod_xmp, mpg123,
FluidSynth-MIDI, Opus, Vorbis — ~19 total), none of which this project's own
`SDLIMAGE_*`/`SDLMIXER_*` CMake args (see `cmake/ThirdPartySDL.cmake`) actually enable.
Measured directly: the plain, non-recursive form above took **~6.5 minutes** in this
environment (network-bound, not CPU-bound — these are large upstream repos cloned at
full history, no shallow-clone flag currently used); the `--recursive` form additionally
attempts all ~19 unneeded nested clones and was observed to still be running, unfinished,
past 2 minutes into just the *extra* nested fetches on top of that.

**Step 3 — first CMake configure.** `cmake/ThirdPartySDL.cmake` then builds SDL3 (and
`SDL_image`/`SDL_mixer`) into a persistent, cache-backed `.sdl-prebuilt-<platform>/`
directory the *first* time any CMake configure runs (outside any `cmake-build-*`
directory, so deleting a build directory or running `cmake --build --clean-first` does
**not** trigger an SDL rebuild) — this step can take a few minutes the very first time,
and is a one-time cost per checkout, not per build. Re-verified 2026-07-06: from a fresh
clone with Steps 1-2 already done, `cmake --preset devices-ubsan` completed the full
first-time vendored SDL3/SDL_image/SDL_mixer build and configured successfully with no
errors. `cmake --build --preset devices-ubsan --target CnaTests -j"$(nproc)"` then
built the full test binary from scratch in **~3m42s** (parallel build, this
environment's core count) with zero errors — confirmed by actually running both
commands in a genuinely fresh clone, not assumed from the existing checkout this
document's other sections use.

## 1. Desktop debug build (Linux, `OPENGLES3` backend)

```bash
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
```

The test target and its transitive CNA libraries build clean as of this writing (2026-07-04, `feature/devices`,
`plan_devices_phase8.md`) — compiled and tested locally in this session's
git checkout; see the ZIP-export caveat above for what this does not claim.

## 2. Devices-only test filter

**Re-verified 2026-07-06 (`DEV-BUILD-002`) against the full, current
`tests/Microsoft/Devices/` tree** — every `.cpp` file under that directory was listed
and every `TEST(...)` suite name extracted directly (21 suites, 283 `TEST()` cases
total, confirmed by a plain `grep -rE '^(TEST|TEST_F|TEST_P)\(' tests/Microsoft/Devices`
count; no `TEST_F`/`TEST_P` fixtures exist in this scope, only plain `TEST`). The
substring-based filter this section previously documented (e.g. bare `Accelerometer`,
`Motion`) had **two real problems**, both confirmed by diffing its actual `ctest -N`
match list against the 283-case ground truth above:
- **Silently dropped `CalibrationEventArgsTests`** (3 tests) — its suite name contains
  none of the old filter's substrings.
- **Matched 2 unrelated false positives outside `Microsoft::Devices`** —
  `GamePadTest.GetAccelerometerEXTReturnsFalseAndZeroesOutputWhenNoGamePadConnected`
  (via the bare `Accelerometer` substring) and
  `SdlInputBridgeTouchGestureTest.FingerMotionThroughProcessEventProducesFlick` (via the
  bare `Motion` substring).

The corrected filter below uses the 21 full suite names instead of loose substrings —
this is both complete (matches all 283 cases, confirmed) and precise (zero
cross-namespace false positives, confirmed by diff):

```bash
# Via ctest — matches exactly the 283 current Devices/Sensors/VibrateController
# TEST() cases, nothing more, nothing less (confirmed 2026-07-06):
cd cmake-build-debug && ctest --output-on-failure \
    -R "AccelerometerFailedExceptionTests|AccelerometerReadingEventArgsTests|AccelerometerReadingTests|AccelerometerTests|AndroidSensorOrientationTests|AttitudeReadingTests|CalibrationEventArgsTests|CompassReadingTests|CompassTests|AndroidCompassMathTests|AndroidMotionMathTests|AndroidSensorBridgeTests|GyroscopeReadingTests|GyroscopeTests|MotionReadingTests|MotionTests|ScopeExitTests|SensorBaseTests|SensorFailedExceptionTests|SensorSubsystemOwnershipTests|VibrateControllerTests"
# 283 tests, 281 passing, 2 expected hardware skips, as of 2026-07-06 (feature/devices).

# Or directly via the test binary's own gtest filter — same 21 suites, same coverage:
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerFailedExceptionTests.*:AccelerometerReadingEventArgsTests.*:AccelerometerReadingTests.*:AccelerometerTests.*:AndroidSensorOrientationTests.*:AttitudeReadingTests.*:CalibrationEventArgsTests.*:CompassReadingTests.*:CompassTests.*:AndroidCompassMathTests.*:AndroidMotionMathTests.*:AndroidSensorBridgeTests.*:GyroscopeReadingTests.*:GyroscopeTests.*:MotionReadingTests.*:MotionTests.*:ScopeExitTests.*:SensorBaseTests.*:SensorFailedExceptionTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*"
# Same 283 tests, 281 passing, 2 expected hardware skips.
```

**Re-verified 2026-07-06 (stabilization pass, same day as the count above but a later
pass — `ANDROID-BRIDGE-002`/`READINGS-002`/`MOTION-008` and the `ShouldAcceptUpdateAt()`
monotonic-clock fix all landed test changes in between):** the same 21-suite filter now
matches **293** tests (281 → 290 passed as tests were added across those tasks, 2
expected hardware skips throughout) — confirmed by actually running the command above
again, not assumed. The suite list itself is unchanged (still 21 suites, no new `.cpp`
file added under `tests/Microsoft/Devices/`); only per-suite test counts grew. If this
number drifts again, re-run the ground-truth `grep -rE '^(TEST|TEST_F|TEST_P)\('
tests/Microsoft/Devices` count from the paragraph above rather than trusting either
number at face value.

If a new file is added under `tests/Microsoft/Devices/` with a new suite name, add that
suite name to both filters above — re-verify with the same diff technique (compare
`ctest -N -R "<filter>"`'s match list against a fresh `grep -rE
'^(TEST|TEST_F|TEST_P)\(' tests/Microsoft/Devices` count) rather than assuming the
filter still covers everything.

Both commands' 2 skips are the same pair: `AccelerometerTests`/`GyroscopeTests`'
`GetCurrentValuePropertyDoesNotThrowWhenSupported` — these `GTEST_SKIP()` themselves
*because* this dev container genuinely has no accelerometer/gyroscope hardware, which
is itself the expected, correct result here, not a failure.

**Concurrency tests in this suite are stress tests, not single-shot checks — a single
green `ctest` run does not prove a concurrency fix is correct.** `plan_devices_phase6.md`
Task P6-1's own addendum found a real, reproducible heap-corruption bug
(`AccelerometerTests`/`GyroscopeTests`' concurrent-construction tests) that a single
`ctest` run did not catch — it only surfaced after looping the same test binary
invocation tens of times in a row. `plan_devices_phase7.md` reinforced the same lesson
twice more: Task P7-1's cross-class SDL-mutex fix needed a *new* stress test that
constructs/destroys/probes both `Accelerometer` and `Gyroscope` concurrently (not just
one class at a time, which the P6-1-era test already covered) — verified clean over 40
loop iterations; and Task P7-3's dispatch use-after-free fix was confirmed real, not
theoretical, only by *deliberately reverting it* and observing the regression test
segfault 5 times out of 5 — a technique worth reaching for whenever a new regression
test passes cleanly on the first try and you want to be sure it would actually fail
without the fix. If you change anything touching `Detail::SdlSensorSubsystem<TSensor>`,
`Accelerometer`, or `Gyroscope`, re-run the relevant `--gtest_filter` in a loop (20–60
iterations) before trusting a single pass:

```bash
cd cmake-build-debug
for i in $(seq 1 40); do
  ./CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*" > /tmp/run_$i.log 2>&1 || echo "run $i FAILED"
done
```

**Exception-swallowing policy in sensor dispatch (Task P8-5):**
`Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()` catches and swallows *any*
exception a `CurrentValueChanged`/`ReadingChanged` handler throws, per-instance, and
continues dispatching to the rest of the batch. This is a deliberate design choice, not
an oversight: the real path (`SensorEventWatch()`) is an `SDL_EventFilter` callback
invoked directly by `SDL_PushEvent()`, a C API that does not expect a C++ exception to
unwind through its own call frames — and swallowing it there is also what lets a
*different*, later instance in the same dispatch batch still receive its own event even
if an earlier instance's handler misbehaves. `AccelerometerTests`/`GyroscopeTests`'
`ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent` (Task
P8-5) proves the batch-continuation half of this claim directly (Task P6-4's
`ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose` only proves the
single-instance half — that a throwing handler doesn't corrupt *that* instance's own
dispatch-tracking state or hang a future `Dispose()`). A game's own
`CurrentValueChanged`/`ReadingChanged` handler should not rely on an exception it throws
propagating anywhere — it never will.

## 3. Full test suite

```bash
cd cmake-build-debug && ctest --output-on-failure
```

As of `plan_devices_phase8.md`: 2051 tests, 2 failures — both pre-existing, unrelated
`EasyGL`/`easy-gl` graphics-backend bugs (`EasyGL_MRT_TwoAttachments`,
`easy-gl-resource-smoke-tests`) that that session's environment happened to have a real
GPU/display to actually run for the first time (previously silently `Not Run`
headless) — confirmed via direct investigation to be 100% unrelated to
`Microsoft::Devices` (see `plan_devices_phase5.md` Task P5-1's Resolution for the full
finding).

**As of `plan_devices.md` Phase 10 (2026-07-05):** 3348 tests, 36 failures — same root
cause category (`EasyGL`/graphics-backend, headless-environment-dependent — this
session's container has no real GPU/display, so more `EasyGL` cases fail/skip than the
2 from the session above that did have one), still confirmed 100% unrelated to
`Microsoft::Devices`: the Devices-only filter (Section 2 above) is separately,
independently 100% green. Not fixed here — out of scope for `Microsoft::Devices` work,
and the exact failure count is expected to vary by environment (GPU/display
availability), not something to chase toward a fixed number. The total test count only
grows as `Microsoft::Devices` (and the rest of CNA) gains tests.

## 4. Android cross-compile

Requires an Android NDK. This session found one already present at
`~/Android/Sdk/ndk/30.0.14904198` — **do not assume this exists in every environment**;
it was absent in every session before `plan_devices_phase4.md` Task P4-11, so check
first (`ls ~/Android/Sdk/ndk/` or equivalent) rather than assuming either way.

```bash
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DCNA_BUILD_TESTS=OFF

cmake --build cmake-build-android -j"$(nproc)"
```

`-DCNA_BUILD_TESTS=OFF`: `googletest` was not configured for the Android NDK toolchain
in this session, so `CnaTests` was never cross-compiled — only the configured CNA libraries.
This is a **compile-only** verification: no APK packaging, no emulator/device
run. Confirmed (Task P4-11, then re-confirmed after further changes in Task P5-7) that
`Accelerometer.cpp`/`Gyroscope.cpp`'s `#ifdef __ANDROID__` code actually gets compiled
in, via the NDK's own `llvm-nm` (the host's plain `nm` produces empty/wrong output
against the cross-compiled ARM64 object files):

```bash
"$HOME/Android/Sdk/ndk/30.0.14904198/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm" -C \
    cmake-build-android/CMakeFiles/CNA.dir/src/Devices/Microsoft/Sensors/Accelerometer.cpp.o \
    | grep -i landscape
```

`plan_devices_phase7.md` Task P7-7 re-ran this same `llvm-nm` check against Phase 7's
actual new symbols (`GetGlobalSdlSensorMutex()`, `WaitForDisposalToComplete()`,
`Detail::SdlSensorSubsystem<...>::DispatchToInstances<...>()`) in
`Accelerometer.cpp.o`/`Gyroscope.cpp.o`, not just re-confirming the Task P4-11-era
landscape symbols still compile — see that task's Resolution for the exact commands.
`plan_devices_phase8.md` Task P8-8 did the same again for Phase 8's actual new symbols
(`dispatchToken_`, the lock-proof-parameter overloads of
`EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()`/`ProbeIsSupported()`).
`plan_devices_phase9.md` Task P9-4 re-confirmed the library cross-compile once more
(no source changes since Task P8-8, so `ninja` correctly reported nothing to rebuild).

### 4.1 APK packaging and emulator/device run (`plan_devices.md` Phase 9, 2026-07-05)

**Superseded — `cna_demo_devices` now packages into a real, installable Android APK.**
Every prior phase (through `plan_devices_phase9.md` Task P9-4) found this "not
available" for two independent reasons: no Gradle/CMake integration existed, and
`/dev/kvm` was absent so no emulator could run even if one had. **Both are now
resolved**, in this exact environment, this session — re-verify both before trusting
this section if either regresses in a future session (environments do change, as this
one just did).

**1. `/dev/kvm` now exists.** `ls -la /dev/kvm` shows a real, openable device node
(`crw-rw----+ 1 root kvm`), confirmed via a direct `open()` call, not just `stat`. Every
session since `plan_devices_phase4.md` found this absent. First noticed during
`plan_devices.md`'s Phase 0 audit (Task DEVICES-0012); exploited for real in Phase 9.

**2. The Gradle/CMake integration now exists**, built from SDL's own vendored template
rather than from scratch:

```bash
# Generate the Android Studio/Gradle project from SDL's template (already done,
# checked in under modules/devices/examples/demo_devices/android/ — re-run only if regenerating):
python3 third_party/SDL/build-scripts/create-android-project.py \
  --variant copy \
  --output modules/devices/examples/demo_devices/android \
  com.openeggbert.cna.demodevices \
  modules/devices/examples/demo_devices/src/Main.cpp modules/devices/examples/demo_devices/src/DevicesDemo.cpp \
  modules/devices/examples/demo_devices/src/DevicesDemo.hpp
```

The generated project's own vendored SDL copy (`app/jni/SDL`, ~34MB) was deleted and its
CMake wiring changed to reuse CNA's own root project instead of building a second,
separate SDL — see `app/jni/CMakeLists.txt` (`add_subdirectory(<cna-root> cna_build)`
with `CNA_BUILD_TESTS`/`CNA_BUILD_EXAMPLES` forced `OFF`) and `app/jni/src/CMakeLists.txt`
(links `main` against `CNA`/`SHARP_RUNTIME` instead of `SDL3::SDL3` directly). This
avoids configuring/building SDL3 twice and any `SDL3::SDL3` target collision.
`CMakeLists.txt`'s root `target_link_libraries(CNA ...)` also gained a `PUBLIC android`
link on `ANDROID`, since `Detail::AndroidSensorBridge`'s NDK `ASensorManager`/`ALooper`
calls (Phase 6) need `libandroid.so`, which nothing previously linked (the `CNA` library
target itself never needed it — only an executable/shared-library consumer does, and
none existed before this phase).

`app/build.gradle` was adjusted to `ndkVersion "30.0.14904198"` and
`ANDROID_PLATFORM=android-24`/`minSdkVersion 24` to match this project's actual minimum
(previously the template's own defaults, `28.2.13676358`/`android-21`).
`AndroidManifest.xml` already ships `android.permission.VIBRATE` uncommented (confirmed
independently in Phase 0, Task DEVICES-0048) — added `android.hardware.sensor.
{accelerometer,gyroscope,compass}` as `android:required="false"` `uses-feature`
declarations (Task DEVICES-0123), so this diagnostic demo still installs on devices
missing any one sensor.

**Build it:**
```bash
cd modules/devices/examples/demo_devices/android/com.openeggbert.cna.demodevices
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
export ANDROID_HOME="$HOME/Android/Sdk"
./gradlew -PBUILD_WITH_CMAKE assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk
```

`BUILD SUCCESSFUL in 1m 39s` this session, producing a 7.3MB `app-debug.apk` — the first
Android APK ever built in this project's history. (One fix needed along the way: the
generator's manifest-comment edit for Task DEVICES-0123 originally used a bare `--`
inside an XML comment body, which XML forbids — `ManifestMerger2` rejected it with a
parse error; fixed by rewording, not by removing the comment.)

**3. The emulator now actually boots and runs the app**, end to end (Task DEVICES-0126):

```bash
~/Android/Sdk/emulator/emulator -avd Medium_Phone -no-window -no-audio -gpu swiftshader_indirect -no-snapshot
# adb devices → emulator-5554  device   (boots in ~1-2 minutes; every prior session
# hard-failed here on "x86_64 emulation currently requires hardware acceleration!")

adb install -r app/build/outputs/apk/debug/app-debug.apk   # Success
adb shell am start -n com.openeggbert.cna.demodevices/com.openeggbert.cna.demodevices.DemodevicesActivity
```

**First install/launch attempt failed** with a real, specific, now-fixed bug:
`logcat` showed `nativeRunMain(): Couldn't find function SDL_main in library libmain.so`.
Root cause: `modules/devices/examples/demo_devices/src/Main.cpp` defines a plain `int main(int, char**)`
and never included `<SDL3/SDL_main.h>` — on desktop this doesn't matter (Linux needs no
special entry point), but `SDL_PLATFORM_ANDROID` requires `#define main SDL_main` (via
`SDL_MAIN_NEEDED`, only applied if that header is actually included) because
`SDLActivity.java` finds the app's entry point via `dlsym(RTLD_DEFAULT, "SDL_main")`, not
a normal native `main()`. Fixed by adding `#include <SDL3/SDL_main.h>` to `Main.cpp`
(a no-op on desktop, confirmed by rebuilding `cna_demo_devices` there — same cached
build, nothing to recompile, no regression). **Note for anyone regenerating this Android
project:** `create-android-project.py --variant copy` duplicates source files into
`app/jni/src/` rather than symlinking them — editing the original
`modules/devices/examples/demo_devices/src/*` does **not** automatically propagate; re-`cp` (or
regenerate) after any source change.

After that fix: `adb logcat` showed
`SDL: Running main function SDL_main from library .../libmain.so` (no error this time),
`ActivityTaskManager: Displayed .../.DemodevicesActivity for user 0: +1s548ms`, and
`adb shell pidof com.openeggbert.cna.demodevices` returned a live PID throughout.
`adb shell screencap` confirmed the demo's actual UI rendering — per-sensor
supported/state indicator squares and signed-value bars, matching
`DevicesDemo.cpp`'s real draw layout, not a blank/crashed screen. Injecting synthetic
sensor values via the emulator console (`sensor set acceleration 0:9.81:3.0`,
`sensor set magnetic-field 30:5:40`) and re-screenshotting showed the demo's
`DrawEventFlash()` indicator lighting up bright green between frames — live evidence
that real sensor events are flowing through the actual Android runtime pipeline (SDL3's
own Android sensor backend for Accelerometer/Gyroscope — Compass/Motion's own
`Detail::AndroidSensorBridge` path was not separately exercised this way, since the
emulator's virtual sensor set doesn't include a virtual rotation-vector/game-rotation
sensor to inject through the emulator console).

The emulator's own system apps (Pixel Launcher, then SystemUI) intermittently showed
"isn't responding" ANR dialogs during this session — an emulator/container resource
constraint (likely `-gpu swiftshader_indirect` software rendering under load), not a bug
in `cna_demo_devices` itself: the demo's own process (confirmed via `pidof`) stayed alive
and kept rendering correctly throughout, unaffected by the system-level ANRs layered on
top of it in the screenshots.

**Net result (Task DEVICES-0125/0126): APK builds, installs, launches, and renders
correctly on an emulator, for the first time in this project's history.** No physical
Android device has been tried (see `docs/devices-hardware-checklist.md`), and the
emulator's own virtual sensors are not the same as real hardware — this closes the
"software pipeline works end-to-end" gap, not the "physically verified" one.

See Section 4.1.1 below for the actual emulator install/run result.

## 5. iOS — confirmed still blocked, not attempted

No Apple/iOS toolchain of any kind exists in this Linux dev container — confirmed by
actually checking (`plan_devices_phase4.md` Task P4-12, re-confirmed
`plan_devices_phase5.md` Task P5-1's audit, `plan_devices_phase6.md` Task P6-10,
`plan_devices_phase7.md` Task P7-7, and `plan_devices_phase8.md` Task P8-8): no
`xcodebuild`, no `xcrun`, no `osxcross`, nothing matching `*ios*toolchain*` anywhere
on the filesystem. Unlike Android (a
missing NDK package, which this session found had since been installed), iOS
cross-compilation fundamentally requires macOS/Xcode to obtain and run its own
toolchain — not fixable by installing a package in a Linux container. Re-check before
assuming this is still true in a future session (environments can change, as Android's
did), but don't expect it to resolve the way Android's did.

### 5.1. `VibrateController` iOS backend — plan only (Task VIB-004, 2026-07-06)

**Decision: yes, iOS vibration should eventually be supported, behind
`Detail::IVibrateBackend` (`VIB-002`), via a `CHHapticEngine`-backed implementation —
planned here, not implemented, since no Apple toolchain exists in this environment to
write or compile Objective-C++ against (Section 5 above).**

- **API choice: `CHHapticEngine` (Core Haptics, iOS 13+), not
  `UIImpactFeedbackGenerator`.** `UIImpactFeedbackGenerator` is UIKit's canned
  tap/knock feedback API (`.light`/`.medium`/`.heavy`/`.soft`/`.rigid` styles via
  `impactOccurred()`) — a fundamentally different shape than XNA's
  `Start(TimeSpan)`/`Start(TimeSpan, float)` contract (an arbitrary caller-chosen
  duration plus a continuous intensity), and it isn't designed to run for a specified
  duration at all. `CHHapticEngine` is the correct match: a `CHHapticEvent` of type
  `.hapticContinuous` takes both `duration` and a `CHHapticEventParameterID.hapticIntensity`
  value directly, which maps onto `Start(TimeSpan, float)` almost one-to-one.
- **Hardware availability check:** `CHHapticEngine.capabilitiesForHardware().supportsHaptics`
  — false on iPad and on iPhones without a Taptic Engine (iPhone 6s and earlier) — this
  is the natural `IVibrateBackend::IsSupported()` implementation; a future
  `CoreHapticsVibrateBackend` must check this before ever constructing an engine, not
  assume every iOS device has one.
  - **`StartLeftRight()` on iOS:** the Taptic Engine is a single actuator — there is no
    iOS API surface for two independently-driven motors. A future implementation should
    fold `largeMotor`/`smallMotor` into one intensity, the same way Android's own SDL3
    haptic backend already does today for the phone's single vibrator
    (`docs/devices-android.md`'s "Vibration" section, `large*0.6 + small*0.4`, confirmed
    by `VIB-003`) — using the identical blend weighting would keep behavior consistent
    across both real phone platforms, rather than inventing a third, arbitrary formula.
    This reinforces `StartLeftRight()`'s own doc comment: true independent two-motor
    output should only be expected from desktop dual-actuator hardware, never from a
    phone, on either supported mobile platform.
  - **Permissions:** none required — Core Haptics needs no `Info.plist` entry or
    runtime permission prompt, unlike `CMMotionManager`'s `NSMotionUsageDescription`
    (`docs/devices-native-backend-design.md`'s iOS Motion section).
  - **Lifecycle note for a future implementation:** `CHHapticEngine` instances can stop
    themselves on audio session interruptions/app backgrounding and must be explicitly
    restarted (`engine.start()` again) before the next `Start()` call — a real
    implementation will need to handle `engine.resetHandler`/`stoppedHandler`, not just
    construct the engine once and assume it stays running, unlike this codebase's
    SDL-haptic backend which has no equivalent interruption model to handle.
- **Not planned:** no separate legacy pre-iOS-13 fallback (e.g. `AudioServicesPlaySystemSound`
  with a vibration system-sound ID) — this project's minimum iOS version has not been
  decided anywhere in this codebase yet, and speculative-abstracting for an undecided
  minimum would be premature; a future task should decide the minimum iOS version
  first, at which point this note should be revisited.

Until an Apple toolchain is available to actually write and compile this,
`VibrateController` has no iOS-specific backend at all — same permanently-`false`
`getIsSupportedProperty()`/silent-no-op `Start()`/`Stop()`/`StartLeftRight()` behavior
as any other platform without a registered `IVibrateBackend` implementation, which is
itself deterministic and already covered by `VibrateControllerTests.cpp`'s
no-hardware-present tests (this container has no haptic device either, so the same code
path is already exercised here).

## 6. Sanitizer builds (Task P8-4)

**A single green `ctest` run proves neither memory safety nor thread safety.** Section 2
already covers stress-looping a plain build to surface bugs that only show up under real
timing pressure (Task P6-1's addendum, Task P7-3). Sanitizers are the complementary
tool: they catch a real bug on the *first* triggering execution, rather than requiring
dozens of loop iterations to get lucky. Both are worth using — a stress loop under a
sanitizer is stronger evidence than either alone.

`CMakePresets.json` has three presets, each verified working in this session
(`plan_devices_phase8.md` Task P8-4 — configured, built, and run against the full
Devices-only test suite, not just written and assumed):

These use the same corrected, exact-suite-name filter as Section 2 above (a bare
`Accelerometer*`/`Motion*` glob here would pick up the same `GamePadTest`/
`SdlInputBridgeTouchGestureTest` false positives noted there):

```bash
# AddressSanitizer — catches use-after-free, heap corruption, buffer overflows.
# Does NOT catch data races; use ThreadSanitizer for that.
cmake --preset devices-asan
cmake --build --preset devices-asan
./cmake-build-devices-asan/CnaTests --gtest_filter="AccelerometerFailedExceptionTests.*:AccelerometerReadingEventArgsTests.*:AccelerometerReadingTests.*:AccelerometerTests.*:AndroidSensorOrientationTests.*:AttitudeReadingTests.*:CalibrationEventArgsTests.*:CompassReadingTests.*:CompassTests.*:AndroidCompassMathTests.*:AndroidMotionMathTests.*:AndroidSensorBridgeTests.*:GyroscopeReadingTests.*:GyroscopeTests.*:MotionReadingTests.*:MotionTests.*:ScopeExitTests.*:SensorBaseTests.*:SensorFailedExceptionTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*"

# ThreadSanitizer — catches data races. This is the one that actually validates
# Microsoft::Devices's own locking discipline.
cmake --preset devices-tsan
cmake --build --preset devices-tsan
./cmake-build-devices-tsan/CnaTests --gtest_filter="AccelerometerFailedExceptionTests.*:AccelerometerReadingEventArgsTests.*:AccelerometerReadingTests.*:AccelerometerTests.*:AndroidSensorOrientationTests.*:AttitudeReadingTests.*:CalibrationEventArgsTests.*:CompassReadingTests.*:CompassTests.*:AndroidCompassMathTests.*:AndroidMotionMathTests.*:AndroidSensorBridgeTests.*:GyroscopeReadingTests.*:GyroscopeTests.*:MotionReadingTests.*:MotionTests.*:ScopeExitTests.*:SensorBaseTests.*:SensorFailedExceptionTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*"

# UndefinedBehaviorSanitizer — catches signed overflow, misaligned access,
# invalid enum values, null-pointer-arithmetic UB, etc.
cmake --preset devices-ubsan
cmake --build --preset devices-ubsan
./cmake-build-devices-ubsan/CnaTests --gtest_filter="AccelerometerFailedExceptionTests.*:AccelerometerReadingEventArgsTests.*:AccelerometerReadingTests.*:AccelerometerTests.*:AndroidSensorOrientationTests.*:AttitudeReadingTests.*:CalibrationEventArgsTests.*:CompassReadingTests.*:CompassTests.*:AndroidCompassMathTests.*:AndroidMotionMathTests.*:AndroidSensorBridgeTests.*:GyroscopeReadingTests.*:GyroscopeTests.*:MotionReadingTests.*:MotionTests.*:ScopeExitTests.*:SensorBaseTests.*:SensorFailedExceptionTests.*:SensorSubsystemOwnershipTests.*:VibrateControllerTests.*"
```

**Actual results as of `plan_devices_phase8.md` (2026-07-04), all three presets
configured, built, and run against this exact filter** (224 tests, 222 passed, 2
expected skips — this direct `--gtest_filter` glob form matches a few more suites than
Section 2's `ctest -R` regex form catches by name; both cover the same
`Microsoft::Devices` scope, the exact count differs slightly by matching mechanics, not
by coverage):
- **ASan:** clean (0 issues) with the Task P8-1 fix in place. Used during Task P8-1 to
  get a *reliable* answer on a use-after-free that a plain (non-sanitized) run did not
  reproduce — heap-use-after-free bugs do not reliably crash without instrumentation,
  since freed small allocations often aren't immediately overwritten; ASan reported a
  definitive, exact-line `heap-use-after-free` when that fix was temporarily reverted.
- **TSan:** first run surfaced **two** distinct races, not one. The first was the
  expected pre-existing `sharp-runtime` finding (see below). The **second was real, and
  new**: `SensorBaseTests.cpp`'s own `TestSensorBase` fixture incremented a plain
  `int timeBetweenUpdatesChangedCount` from its `TimeBetweenUpdatesChanged` handler —
  which fires *outside* `mutex_` by design (never hold the lock while raising an event)
  — so Task P8-2's own new `ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash` test
  (the first test in this codebase's history to actually drive concurrent value changes
  on `TimeBetweenUpdates`) raced on it from multiple threads. This is a test-fixture-only
  race, not a `Microsoft::Devices` production-code bug, but real and worth fixing
  properly: changed to `std::atomic<int>`, confirmed clean on the next TSan run. **This
  is exactly the point of actually running the sanitizer instead of just writing the
  preset** — a plain, unsanitized run of the same test never showed any symptom.
  After that fix, the *only* remaining finding (41 reports, all the identical location)
  is the pre-existing, out-of-scope `sharp-runtime` race:
  `System::TimeSpan::TimeSpan(const TimeSpan&)` incrementing an unsynchronized global
  `copy_count` debug/instrumentation counter at `sharp-runtime/src/System/TimeSpan.cpp:55`.
  This is a `sharp-runtime` issue (a separate repo with its own `CLAUDE.md`/git history —
  see `NEXT.md`'s "do not fix bugs discovered in sharp-runtime..." rule), not a
  `Microsoft::Devices` bug. **If a future TSan run reports anything other than this one
  `TimeSpan.cpp:55` finding, treat it as a real, new bug worth investigating** — don't
  assume it's "just that same old sharp-runtime thing" without checking the actual
  file/line, the same mistake this task's first run would have been if the second race
  had been waved away without reading it.
- **UBSan:** clean (0 issues).

**Re-verified as of `plan_devices.md` Phase 10 (2026-07-05), all three presets
reconfigured, rebuilt, and re-run against the updated filter above** (271 tests, 269
passed, 2 expected skips):
- **ASan:** clean (0 issues) — confirmed on Phases 6-8's new `Detail::AndroidSensorBridge`/
  `AndroidCompassBackend`/`AndroidMotionBackend` code too (their non-Android inert paths
  and pure math functions run on this desktop build; the real `#ifdef __ANDROID__` code
  cannot execute here at all, only compile — see `docs/devices-hardware-checklist.md`
  §6-8 for what that leaves genuinely unverified).
- **TSan:** 40 reports, **all confirmed the identical known finding** — checked directly,
  not assumed: every single report's own `Location is global 'System::TimeSpan::copy_count'`
  line matches exactly, despite the surrounding call stacks now varying more (e.g. via
  `SensorBase.hpp:294`, `DateTimeOffset.cpp:71/74` — different construction paths through
  `Accelerometer`'s constructor and `AccelerometerReading`'s default `Timestamp`, all
  ultimately copying the same `TimeSpan`/`DateTimeOffset` types that hit the same
  unsynchronized `sharp-runtime` counter). This re-confirms the "if a future TSan run
  reports anything other than this one finding, treat it as new" instruction above still
  holds — verified by actually reading all 40 reports' location lines, not by pattern-matching
  the call stacks alone (which looked different enough at a glance to warrant checking).
- **UBSan:** clean (0 issues).

**Re-verified 2026-07-06 (`DEV-BUILD-002`), all three presets rebuilt and re-run against
the corrected, exact-suite-name filter above** (283 tests, 281 passed, 2 expected
skips — matches the ground-truth 283 `TEST()` count exactly, unlike every prior
session's filter):
- **ASan:** clean (0 issues).
- **TSan:** 41 reports, all confirmed the identical known finding — every report's
  `Location is global 'System::TimeSpan::copy_count'` line matches exactly (checked
  directly, not assumed); this is the same pre-existing, out-of-scope `sharp-runtime`
  race documented above, not a new `Microsoft::Devices` finding.
- **UBSan:** 3 reports, all pre-existing and confirmed 0 in any `Microsoft::Devices`
  file — 2× `Vector3.cpp:117` and 1× `Matrix.cpp:249`, both `GetHashCode()` signed
  integer overflow, triggered indirectly by `AccelerometerReadingTests`/
  `AttitudeReadingTests` hashing a `Vector3`/`Matrix` member, not a bug in the reading
  struct's own code.

**Throwaway, non-preset builds used during development** (e.g.
`/tmp/cmake-build-asan-check`) are equally valid if you'd rather not create a build
directory inside the repo — pass the same `CMAKE_CXX_FLAGS`/`CMAKE_EXE_LINKER_FLAGS`
directly to a plain `cmake -S . -B <dir>` invocation instead of using the preset. Either
way, remember these are Debug, unoptimized-ish (`-O0`/`-O1`), instrumented builds —
useful for correctness verification, not for measuring performance, and slower to
build/run than a plain `cmake-build-debug`.

## 7. Decided against: a native Android vibration backend (`plan_devices.md` Task DEVICES-0031, 2026-07-05)

Do not build a JNI/`Vibrator`/`VibrationEffect` bridge for `VibrateController` on
Android, and do not build an `IDeviceVibrationBackend` abstraction seam to select one
in. Both were considered and explicitly rejected: reading SDL3's own Android haptic
backend (`third_party/SDL/src/haptic/android/SDL_syshaptic.c` and its Java counterpart,
`third_party/SDL/android-project/app/src/main/java/org/libsdl/app/SDLControllerManager.java`'s
`SDLHapticHandler`/`SDLHapticHandler_API26`/`SDLHapticHandler_API31`) confirms it already
queries `Context.VIBRATOR_SERVICE` (the phone's own built-in vibrator, separate from any
connected-controller vibrator) and already implements amplitude control end to end via
`VibrationEffect.createOneShot()`/`VibratorManager` — including the exact
`intensity == 0.0f → stop()` and `intensity * 255` clamped to `[1,255]` mapping a custom
bridge would have had to reinvent. SDL's own Android manifest template
(`third_party/SDL/android-project/app/src/main/AndroidManifest.xml`) already declares
`android.permission.VIBRATE`, uncommented. Building a second (native) backend behind an
abstraction seam that would only ever have one real implementation would be pure
speculative abstraction — see `plan_devices.md`'s Task DEVICES-0031 for the full
evidence trail before reconsidering this.

## 8. Continuous Integration (`DEV-BUILD-003`, 2026-07-06)

`.github/workflows/devices-tests.yml` builds `CnaTests` and runs the exact-suite-name
Devices/Sensors filter from Section 2 above on every push/PR that touches
`include/Microsoft/Devices/**`, `src/Devices/Microsoft/**`,
`tests/Microsoft/Devices/**`, or the top-level CMake files, plus on manual
`workflow_dispatch`. It runs on a plain `ubuntu-latest` GitHub-hosted runner — no
physical sensor or haptic hardware is available there, which is the point: the two
tests that need real hardware
(`AccelerometerTests.GetCurrentValuePropertyDoesNotThrowWhenSupported`,
`GyroscopeTests.GetCurrentValuePropertyDoesNotThrowWhenSupported`) are not excluded from
the CI filter at all — they call `GTEST_SKIP()` internally whenever
`getIsSupportedProperty()` is false (see their own bodies), so on a hardware-free runner
they simply report `SKIPPED`, the same way they do in this local development container.
No separate "hardware-only" CI filter was needed for that reason.

The job:
- checks out this repo (non-recursive submodules — `third_party/SDL`/`SDL_image`/
  `SDL_mixer`/`vendor/googletest`, matching the `DEV-BUILD-001`-corrected guidance in
  Section 0, not `--recursive`), plus the three sibling repos (`sharp-runtime`,
  `easy-gl`, `meta-gl`) into sibling directories on the runner, since `CMakeLists.txt`
  resolves them via `add_subdirectory(../x)`, not a submodule path;
- installs the same Ubuntu SDL3 build dependencies documented in
  `third_party/SDL/docs/README-linux.md`'s "Ubuntu 18.04, all available features"
  list (plus `libwayland-dev`/`libdecor-0-dev` from its Ubuntu 22.04+ addendum) and the
  FFmpeg dev packages from this repo's own `CLAUDE.md` (`CMakeLists.txt` requires
  `libavcodec`/`libavformat`/`libavutil`/`libswresample` via `pkg-config` unconditionally
  on Linux, regardless of which target is actually being built — see its
  `CNA_FFMPEG_AVAILABLE` block);
- caches `.sdl-prebuilt-Linux-x86_64/` (the vendored SDL3/SDL_image/SDL_mixer
  from-source build tree, Section 1) keyed on the submodules' own tracked commits, so a
  submodule bump invalidates the cache automatically and every other push reuses the
  ~6-7 minute first-time SDL build (`DEV-BUILD-001`);
- configures and builds with the existing `devices-ubsan` preset (Section 6) rather than
  a plain, non-sanitized build, so every CI run also gets UndefinedBehaviorSanitizer
  coverage for free, at no extra job;
- runs `CnaTests` with the Section 2 filter directly (not through `ctest`, matching this
  project's own documented reason in the `tests` preset description — `ctest` races
  several tests that share hardcoded `/tmp` fixture paths across processes);
- builds and runs `cna_strict_xna_api_check` directly as its own separate step (Task
  `DEVPERF-001`, 2026-07-17) — this target is registered as its own `ctest` test
  (`StrictXnaApiSurfaceCheck_Compile_Run`, `cmake/Harnesses.cmake`), so it was never
  actually exercised by the `CnaTests`-binary-direct step above, which is a distinct
  gtest executable.

This CI job has not yet actually executed on GitHub Actions as of this writing (no push
to a remote branch has triggered it in this session) — the exact commands it runs
(`cmake --preset devices-ubsan`, `cmake --build --preset devices-ubsan --parallel`, the
Section 2 filter) were each independently verified locally in this container, and the
apt package list is `third_party/SDL`'s own documented Ubuntu list, but the *workflow
file itself* running green on an actual GitHub-hosted runner is not yet confirmed —
worth checking the Actions tab after the first push that includes it.

### Reproducibility from a clean checkout (`DEVPERF-001`, 2026-07-17)

An independent static/native-contract audit of a manually-produced archive
(`cna-feature-devices(17).zip`) found that archive had empty `third_party/SDL` (and
`SDL_image`/`SDL_mixer`, `vendor/googletest`) directories, making its recorded
build/sanitizer results impossible to independently repeat from that archive alone.
Investigating this (`DEVPERF-001`) found:

- **This repository has no export/release/archive script of its own** — there is no
  in-repo tooling that produces a distributable ZIP/tarball at all (confirmed: no
  `scripts/`, `tools/`, or CI step packages a release artifact). The archive the audit
  used was necessarily produced by some process *outside* this repo's control — most
  plausibly a plain "download source as ZIP" export (e.g. GitHub's `codeload` endpoint,
  or `git archive`), which **cannot** include submodule content under any circumstances:
  submodules are gitlinks (a commit-SHA pointer to a separate repository), and a
  source-tree-only archive export has no mechanism to recurse into them. This is
  standard git/GitHub behavior, not a defect in any script this project owns — there was
  nothing to "update" here as the task's literal "Required work" phrasing assumed.
- What *is* actionable, and has been done:
  1. **Fail fast, for every required vendored directory, with an actionable message.**
     `cmake/ThirdPartySDL.cmake` already did this for `third_party/SDL`/`SDL_image`/
     `SDL_mixer` (Task `DEV-BUILD-001`, Section 0 above). `cmake/UnitTests.cmake` did
     **not** have the equivalent guard for `vendor/googletest` — `add_subdirectory(vendor/
     googletest)` would previously fail with CMake's own generic, non-actionable
     "given source ... which is not an existing directory" error. Added the same
     `FATAL_ERROR`-with-exact-fix guard there.
  2. **A genuine clean-room CI job.** `.github/workflows/devices-tests.yml` already
     checked out this repo fresh (via `actions/checkout`, `submodules: true`) on an
     isolated GitHub-hosted runner with no access to any contributor's own working
     tree, then configured/built/tested `Microsoft::Devices` from that fresh checkout —
     this already *is* the "clean-room CI job" the task's required work asked for, and
     it already fails loudly (via the `FATAL_ERROR` guards above) if a required
     vendored directory were ever somehow empty at that point. What it was missing:
     building and running the strict XNA API surface check (see above) — added as its
     own step.
  3. **Document the actual reproduction recipe clearly**, so nobody attempts to reproduce
     results from a bare ZIP/tarball export in the first place — Section 0 above and this
     file's own opening "ZIP-export caveat" already did this (`Task P7-6`); no further
     change was needed there.
- **What was not built, deliberately:** a new release/archive pipeline or a "clean-room
  log stored as a release artifact." This project has no release process to attach such
  an artifact to, and inventing one purely to produce a document nobody asked for would
  be speculative scope creep — the existing CI job's own logs (viewable on every run via
  the GitHub Actions tab) already serve as that evidence, for every push/PR that touches
  `Microsoft::Devices`, not just a point-in-time release.
