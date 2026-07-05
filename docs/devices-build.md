# Building and Testing `Microsoft::Devices` — Reproducible Commands

Every command below was actually run in a session working on `Microsoft::Devices`
(`plan_devices_phase4.md`/`plan_devices_phase5.md`/`plan_devices_phase6.md`/
`plan_devices_phase7.md`/`plan_devices_phase8.md`), not copy-pasted from
documentation without running it. Where a command's success is asserted, it was
verified in this repository, on this branch, this session.

**ZIP-export caveat (Task P7-6):** every claim in this document describes a real
`git clone` of this repository with submodules initialized (Section 0 below) — it
does **not** describe, and should not be read as implying anything about, a bare
ZIP/tarball export of this source tree. A raw source snapshot without
`git submodule update --init --recursive` having been run has empty
`third_party/SDL` (and `SDL_image`/`SDL_mixer`, `vendor/googletest`) directories
and will not configure, let alone build or pass any test below.

## 0. Fresh clone / submodule setup

This repository vendors SDL3 (and `SDL_image`/`SDL_mixer`) as git submodules under
`third_party/`, plus `googletest` under `vendor/` — confirmed via `.gitmodules` and
`cmake/ThirdPartySDL.cmake` (which hard-fails with `FATAL_ERROR` and prints the exact
fix if a submodule is missing, rather than silently doing something else). A fresh
clone needs:

```bash
git submodule update --init --recursive
```

`cmake/ThirdPartySDL.cmake` then builds SDL3 into a persistent, cache-backed
`.sdl-prebuilt/` directory the *first* time any CMake configure runs (outside any
`cmake-build-*` directory, so deleting a build directory or running `cmake --build
--clean-first` does **not** trigger an SDL rebuild) — this step can take a few minutes
the very first time, and is a one-time cost per checkout, not per build.

## 1. Desktop debug build (Linux, `EASYGL` backend)

```bash
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

cmake --build cmake-build-debug --target CNA -j"$(nproc)"
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
```

Both targets build clean as of this writing (2026-07-04, `feature/devices`,
`plan_devices_phase8.md`) — compiled and tested locally in this session's
git checkout; see the ZIP-export caveat above for what this does not claim.

## 2. Devices-only test filter

```bash
# Via ctest, matching every Devices/Sensors/VibrateController test suite by name
# (this also matches the Reading/EventArgs/FailedException-type test suites, e.g.
# AccelerometerReadingTests, SensorFailedExceptionTests — anything with these
# substrings in its suite name, which is more than just the 9 "main" suites below.
# Does NOT match SensorBaseTests or ScopeExitTests — add them explicitly to a
# gtest_filter, or use ctest -R "SensorBase|ScopeExit" separately, if you need
# those suites too):
cd cmake-build-debug && ctest --output-on-failure \
    -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase|ScopeExit"
# 226 tests, 100% passing, as of plan_devices_phase8.md Task P8-8 (last verified this way).

# Or directly via the test binary's own gtest filter — narrower: only the 9
# "main" per-class suites, not the Reading/EventArgs/FailedException ones:
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*:VibrateControllerTests.*:SensorSubsystemOwnershipTests.*:AndroidSensorOrientationTests.*:SensorBaseTests.*:ScopeExitTests.*"
# 146 tests, 144 passing, as of plan_devices_phase8.md Task P8-8 (last verified this way).
```

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

As of this writing: 2051 tests, 2 failures — both pre-existing, unrelated `EasyGL`/
`easy-gl` graphics-backend bugs (`EasyGL_MRT_TwoAttachments`,
`easy-gl-resource-smoke-tests`) that this session's environment happens to have a real
GPU/display to actually run for the first time (previously silently `Not Run`
headless) — confirmed via direct investigation to be 100% unrelated to
`Microsoft::Devices` (see `plan_devices_phase5.md` Task P5-1's Resolution for the full
finding). Not fixed here — out of scope for `Microsoft::Devices` work. Same 2 failures,
same root cause, every phase since Phase 5 — the test count only grows as
`Microsoft::Devices` itself gains tests.

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

cmake --build cmake-build-android --target CNA -j"$(nproc)"
```

`-DCNA_BUILD_TESTS=OFF`: `googletest` was not configured for the Android NDK toolchain
in this session, so `CnaTests` was never cross-compiled — only the `CNA` static library
itself. This is a **compile-only** verification: no APK packaging, no emulator/device
run. Confirmed (Task P4-11, then re-confirmed after further changes in Task P5-7) that
`Accelerometer.cpp`/`Gyroscope.cpp`'s `#ifdef __ANDROID__` code actually gets compiled
in, via the NDK's own `llvm-nm` (the host's plain `nm` produces empty/wrong output
against the cross-compiled ARM64 object files):

```bash
"$HOME/Android/Sdk/ndk/30.0.14904198/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm" -C \
    cmake-build-android/CMakeFiles/CNA.dir/src/Microsoft/Devices/Sensors/Accelerometer.cpp.o \
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
# checked in under examples/demo_devices/android/ — re-run only if regenerating):
python3 third_party/SDL/build-scripts/create-android-project.py \
  --variant copy \
  --output examples/demo_devices/android \
  com.openeggbert.cna.demodevices \
  examples/demo_devices/src/Main.cpp examples/demo_devices/src/DevicesDemo.cpp \
  examples/demo_devices/src/DevicesDemo.hpp
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
cd examples/demo_devices/android/com.openeggbert.cna.demodevices
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
Root cause: `examples/demo_devices/src/Main.cpp` defines a plain `int main(int, char**)`
and never included `<SDL3/SDL_main.h>` — on desktop this doesn't matter (Linux needs no
special entry point), but `SDL_PLATFORM_ANDROID` requires `#define main SDL_main` (via
`SDL_MAIN_NEEDED`, only applied if that header is actually included) because
`SDLActivity.java` finds the app's entry point via `dlsym(RTLD_DEFAULT, "SDL_main")`, not
a normal native `main()`. Fixed by adding `#include <SDL3/SDL_main.h>` to `Main.cpp`
(a no-op on desktop, confirmed by rebuilding `cna_demo_devices` there — same cached
build, nothing to recompile, no regression). **Note for anyone regenerating this Android
project:** `create-android-project.py --variant copy` duplicates source files into
`app/jni/src/` rather than symlinking them — editing the original
`examples/demo_devices/src/*` does **not** automatically propagate; re-`cp` (or
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

```bash
# AddressSanitizer — catches use-after-free, heap corruption, buffer overflows.
# Does NOT catch data races; use ThreadSanitizer for that.
cmake --preset devices-asan
cmake --build --preset devices-asan
./cmake-build-devices-asan/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"

# ThreadSanitizer — catches data races. This is the one that actually validates
# Microsoft::Devices's own locking discipline.
cmake --preset devices-tsan
cmake --build --preset devices-tsan
./cmake-build-devices-tsan/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"

# UndefinedBehaviorSanitizer — catches signed overflow, misaligned access,
# invalid enum values, null-pointer-arithmetic UB, etc.
cmake --preset devices-ubsan
cmake --build --preset devices-ubsan
./cmake-build-devices-ubsan/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
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
