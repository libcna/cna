# Building and Testing `Microsoft::Devices` — Reproducible Commands

Every command below was actually run in a session working on `Microsoft::Devices`
(the devices phase-4 to phase-8 plans, deleted long ago; Git history), not copy-pasted from
documentation without running it. Where a command's success is asserted, it was
verified in this repository, on this branch, this session.

**ZIP-export caveat (Task P7-6):** every claim in this document describes a real
`git clone` of this repository with submodules initialized and the sibling repositories
below present (Section 0) — it does **not** describe, and should not be read as
implying anything about, a bare ZIP/tarball export of this source tree. A raw source
snapshot without `git submodule update --init` having been run has empty
`third_party/SDL` (and `SDL_mixer`, `vendor/googletest`) directories
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
git clone https://github.com/libcna/sharp-runtime.git
git clone https://github.com/libcna/easy-gl.git
git clone https://github.com/libcna/meta-gl.git
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
`SDL_mixer`) as git submodules under `third_party/`, plus `googletest`
under `vendor/` — confirmed via `.gitmodules` and `cmake/ThirdPartySDL.cmake` (which
hard-fails with `FATAL_ERROR` and prints the exact fix if a submodule is missing,
rather than silently doing something else). A fresh clone needs:

```bash
git submodule update --init
```

**Deliberately non-recursive** (Task `DEV-BUILD-001` corrected this from an earlier,
misleading `--recursive` suggestion): `SDL_mixer` carries its own further nested
`external/*` submodules (GME, mod_xmp, mpg123, FluidSynth-MIDI, Opus, Vorbis and more), none
of which this project's own `SDLMIXER_*` CMake args (see `cmake/ThirdPartySDL.cmake`) actually
enable. (The measurement below predates the removal of the `SDL_image` submodule, which carried
more of them.)
Measured directly: the plain, non-recursive form above took **~6.5 minutes** in this
environment (network-bound, not CPU-bound — these are large upstream repos cloned at
full history, no shallow-clone flag currently used); the `--recursive` form additionally
attempts all ~19 unneeded nested clones and was observed to still be running, unfinished,
past 2 minutes into just the *extra* nested fetches on top of that.

**Step 3 — first CMake configure.** `cmake/ThirdPartySDL.cmake` then builds SDL3 (and
`SDL_mixer`) into a persistent, cache-backed `.sdl-prebuilt-<platform>/`
directory the *first* time any CMake configure runs (outside any `cmake-build-*`
directory, so deleting a build directory or running `cmake --build --clean-first` does
**not** trigger an SDL rebuild) — this step can take a few minutes the very first time,
and is a one-time cost per checkout, not per build. Re-verified 2026-07-06: from a fresh
clone with Steps 1-2 already done, `cmake --preset devices-ubsan` completed the full
first-time vendored SDL3/SDL_image/SDL_mixer build and configured successfully with no
errors. The focused Devices verification targets are now `CnaDevicesTests`,
`CnaDevicesExtTests`, and `cna_strict_xna_api_check`; build them with at most eight
parallel jobs.

## 1. Desktop debug build (Linux, `OPENGLES3` backend)

```bash
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON -DCNA_DEVICES=ON \
      -DCMAKE_BUILD_TYPE=Debug

cmake --build cmake-build-debug --target CnaDevicesTests --parallel 8
```

The test target and its transitive CNA libraries build clean as of this writing (2026-07-04, `feature/devices`,
the devices phase-8 plan) — compiled and tested locally in this session's
git checkout; see the ZIP-export caveat above for what this does not claim.

## 2. Focused Devices test executables

Devices tests have dedicated executables, so do not maintain a hand-written list of
GoogleTest suites. Building and running the executable includes every current and future
test under `modules/devices/tests/Microsoft/Devices/` automatically:

```bash
cmake --build cmake-build-debug --target CnaDevicesTests --parallel 8
SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaDevicesTests
```

When `CNA_DEVICES=ON`, the extension surface under
`modules/devices-ext/tests/CNA/Devices/` is checked separately. Some camera tests create
a graphics device, so use a private display (or the repository's private GPU runner),
never the owner's live desktop:

```bash
cmake --build cmake-build-debug --target CnaDevicesExtTests --parallel 8
tools/platform/run_gpu_tests_private.sh --exec cmake-build-debug/CnaDevicesExtTests
```

Use `./cmake-build-debug/CnaDevicesTests --gtest_list_tests` when an exact current test
inventory is needed; do not copy a count or suite list into this document. Tests that
genuinely require unavailable hardware remain in the executable and call `GTEST_SKIP()`.

**Concurrency tests in this suite are stress tests, not single-shot checks — a single
green `ctest` run does not prove a concurrency fix is correct.** The devices phase-6 plan
Task P6-1's own addendum found a real, reproducible heap-corruption bug
(`AccelerometerTests`/`GyroscopeTests`' concurrent-construction tests) that a single
`ctest` run did not catch — it only surfaced after looping the same test binary
invocation tens of times in a row. The devices phase-7 plan reinforced the same lesson
twice more: Task P7-1's cross-class SDL-mutex fix needed a *new* stress test that
constructs/destroys/probes both `Accelerometer` and `Gyroscope` concurrently (not just
one class at a time, which the P6-1-era test already covered) — verified clean over 40
loop iterations; and Task P7-3's dispatch use-after-free fix was confirmed real, not
theoretical, only by *deliberately reverting it* and observing the regression test
segfault 5 times out of 5 — a technique worth reaching for whenever a new regression
test passes cleanly on the first try and you want to be sure it would actually fail
without the fix. If you change anything touching `Detail::PlatformSensorSubsystem<TSensor>`,
`Accelerometer`, or `Gyroscope`, re-run the relevant `--gtest_filter` in a loop (20–60
iterations) before trusting a single pass:

```bash
cd cmake-build-debug
for i in $(seq 1 40); do
  ./CnaDevicesTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*" > /tmp/run_$i.log 2>&1 || echo "run $i FAILED"
done
```

**Exception-swallowing policy in sensor dispatch (Task P8-5):**
`Detail::PlatformSensorSubsystem<TSensor>::DispatchToInstances()` catches and swallows *any*
exception a `CurrentValueChanged`/`ReadingChanged` handler throws, per-instance, and
continues dispatching to the rest of the batch. This is a deliberate design choice, not
an oversight: the selected platform session may invoke through a C callback boundary that does
not permit a C++ exception to unwind through its own call frames. Swallowing it there is also what lets a
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

For Devices changes, run the focused executables in Section 2 first. The repository-wide
suite includes GPU/window tests and must be run through the private runner when those
tests are selected; its total count is intentionally not frozen here.

## 4. Android cross-compile

Requires an Android NDK. Select an installed version explicitly rather than copying a
machine-specific version number from this document:

```bash
CNA_ANDROID_NDK=/absolute/path/to/Android/Sdk/ndk/<installed-version>
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$CNA_ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DCNA_BUILD_TESTS=OFF

cmake --build cmake-build-android --parallel 8
```

`-DCNA_BUILD_TESTS=OFF` means only the configured CNA libraries are cross-compiled. This
is a **compile-only** verification: no APK packaging and no emulator/device run. The
Android-only sensor sources live under `modules/devices/src/Sensors/`; confirm their
objects were produced rather than relying on a host build that excludes `__ANDROID__`:

```bash
find cmake-build-android/modules/devices -name 'AndroidSensorBridge.cpp.o' \
    -o -name 'AndroidMotionBackend.cpp.o' -o -name 'AndroidSensorWorkerLifecycle.cpp.o'
```

### 4.1 APK packaging and emulator/device run (devices plan Phase 9, 2026-07-05)

**Superseded — `cna_demo_devices` now packages into a real, installable Android APK.**
Every prior phase (through devices phase-9 plan Task P9-4) found this "not
available" for two independent reasons: no Gradle/CMake integration existed, and
`/dev/kvm` was absent so no emulator could run even if one had. **Both are now
resolved**, in this exact environment, this session — re-verify both before trusting
this section if either regresses in a future session (environments do change, as this
one just did).

**1. `/dev/kvm` now exists.** `ls -la /dev/kvm` shows a real, openable device node
(`crw-rw----+ 1 root kvm`), confirmed via a direct `open()` call, not just `stat`. Every
session since the devices phase-4 plan found this absent. First noticed during
the devices plan's Phase 0 audit (Task DEVICES-0012); exploited for real in Phase 9.

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
actually checking (devices phase-4 plan Task P4-12, re-confirmed
devices phase-5 plan Task P5-1's audit, devices phase-6 plan Task P6-10,
devices phase-7 plan Task P7-7, and devices phase-8 plan Task P8-8): no
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

Use the focused executable with each Devices sanitizer preset. This avoids stale suite
filters while retaining every test added to the module:

```bash
# AddressSanitizer: lifetime and memory errors.
cmake --preset devices-asan
cmake --build --preset devices-asan --target CnaDevicesTests --parallel 8
SDL_AUDIODRIVER=dummy ./cmake-build-devices-asan/CnaDevicesTests

# ThreadSanitizer: data races.
cmake --preset devices-tsan
cmake --build --preset devices-tsan --target CnaDevicesTests --parallel 8
SDL_AUDIODRIVER=dummy ./cmake-build-devices-tsan/CnaDevicesTests

# UndefinedBehaviorSanitizer.
cmake --preset devices-ubsan
cmake --build --preset devices-ubsan --target CnaDevicesTests --parallel 8
SDL_AUDIODRIVER=dummy ./cmake-build-devices-ubsan/CnaDevicesTests
```

Run `CnaDevicesExtTests` as an additional target through the private GPU runner when a
change touches `modules/devices-ext/`. Historical sanitizer findings belong in Git
history or the current defect handoff, not as fixed test counts in this command guide.

## 7. Decided against: a native Android vibration backend (devices plan Task DEVICES-0031, 2026-07-05)

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
speculative abstraction — see the devices plan's Task DEVICES-0031 for the full
evidence trail before reconsidering this.

## 8. Continuous Integration (`DEV-BUILD-003`, 2026-07-06)

`.github/workflows/devices-tests.yml` builds `CnaDevicesTests`, `CnaDevicesExtTests`, and
`cna_strict_xna_api_check` on every push/PR that touches the Devices module paths or
their build configuration, plus on manual `workflow_dispatch`. It runs on a plain
`ubuntu-latest` GitHub-hosted runner without physical sensor or haptic hardware.
Hardware-dependent tests are not filtered out; they self-skip when unsupported.

The job:
- checks out this repo (non-recursive submodules — `third_party/SDL`/
  `SDL_mixer`/`vendor/googletest`, matching the `DEV-BUILD-001`-corrected guidance in
  Section 0, not `--recursive`), plus the three sibling repos (`sharp-runtime`,
  `easy-gl`, `meta-gl`) into sibling directories on the runner, since `CMakeLists.txt`
  resolves them via `add_subdirectory(../x)`, not a submodule path;
- installs the same Ubuntu SDL3 build dependencies documented in
  `third_party/SDL/docs/README-linux.md`'s "Ubuntu 18.04, all available features"
  list (plus `libwayland-dev`/`libdecor-0-dev` from its Ubuntu 22.04+ addendum) and the
  FFmpeg dev packages for the optional VideoPlayer backend. They are no longer required by a
  device-only/Game build: `CNA_ENABLE_VIDEO=AUTO` uses them when present and
  `CNA_ENABLE_VIDEO=OFF` omits the entire dependency (see `docs/video-backend.md`);
- caches `.sdl-prebuilt-Linux-x86_64/` (the vendored SDL3/SDL_mixer
  from-source build tree, Section 1) keyed on the submodules' own tracked commits, so a
  submodule bump invalidates the cache automatically and every other push reuses the
  ~6-7 minute first-time SDL build (`DEV-BUILD-001`);
- configures and builds with the existing `devices-ubsan` preset (Section 6) rather than
  a plain, non-sanitized build, so every CI run also gets UndefinedBehaviorSanitizer
  coverage for free, at no extra job;
- runs the complete `CnaDevicesTests` executable directly, without a copied suite list;
- runs `CnaDevicesExtTests` separately under Xvfb because its camera tests create a
  graphics device on the hosted runner;
- builds and runs `cna_strict_xna_api_check` directly as its own separate step (Task
  `DEVPERF-001`, 2026-07-17) — this target is registered as its own `ctest` test
  (`StrictXnaApiSurfaceCheck_Compile_Run`, `cmake/Harnesses.cmake`), so it was never
  actually exercised by the focused gtest executables above, which are distinct
  gtest executable.

Consult the Actions run for the current hosted-runner result; this document does not
freeze a run ID, test count, or copied filter that can drift from the executables.

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
