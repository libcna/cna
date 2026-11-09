# Building and Testing `Microsoft::Devices` — Reproducible Commands

This page holds the build and test recipes for `Microsoft::Devices`. Automated GPU/window runs use
the private runner; compilation uses at most eight parallel jobs.

**Source-archive caveat (Task P7-6, DEVPERF-001):** every recipe below assumes a real `git clone`
of this repository with submodules initialized and the sibling repositories present (Section 0).
A plain source export (`git archive`, a hosting service's "download ZIP") cannot contain submodule
content, because submodules are gitlinks; its `third_party/SDL`, `third_party/SDL_mixer` and
`vendor/googletest` directories are empty and it will not configure. The repository has no
release/archive script of its own. Configure fails fast with the exact fix for every missing
vendored directory: `cmake/ThirdPartySDL.cmake` for the SDL submodules and `cmake/UnitTests.cmake`
for `vendor/googletest`.

## 0. Fresh clone / submodule setup

**Step 1 — sibling repositories.** `sharp-runtime` and `easy-gl` (which itself needs
`meta-gl`) are **separate git repositories that must be cloned next to this repo's own
directory** — they are plain sibling checkouts referenced via `add_subdirectory(../x)`
in `CMakeLists.txt`, **not** git submodules of this repo, so `git submodule update
--init` cannot fetch them. CMake fails fast with an actionable `FATAL_ERROR` naming the
missing sibling and the exact fix (`DEV-BUILD-001`). From this repo's own parent directory:

```bash
git clone https://github.com/libcna/sharp-runtime.git
git clone https://github.com/libcna/easy-gl.git
git clone https://github.com/libcna/meta-gl.git
```

Layout expected (all four as siblings under one parent directory):

```text
<parent>/
├── cna/              (this repo)
├── sharp-runtime/
├── easy-gl/
└── meta-gl/
```

`meta-gl` has no further sibling or submodule dependencies of its own — this three-repo chain is
the full transitive closure for an `OPENGLES3` renderer build. `sharp-runtime`'s own
`vendor/googletest` submodule is handled by its own build.

**Step 2 — this repo's own git submodules.** SDL3 and `SDL_mixer` are git submodules under
`third_party/`, plus `googletest` under `vendor/`:

```bash
git submodule update --init
```

**Deliberately non-recursive** (`DEV-BUILD-001`): `SDL_mixer` carries its own nested `external/*`
submodules (GME, mod_xmp, mpg123, FluidSynth-MIDI, Opus, Vorbis and more), none of which this
project's `SDLMIXER_*` CMake arguments (`cmake/ThirdPartySDL.cmake`) enable; `--recursive` only adds
large, unneeded network fetches.

**Step 3 — first CMake configure.** `cmake/ThirdPartySDL.cmake` builds SDL3 (and `SDL_mixer`)
into a persistent, cache-backed `.sdl-prebuilt-<platform>/` directory the *first* time any CMake
configure runs. It lives outside every `cmake-build-*` directory, so deleting a build directory or
running `cmake --build --clean-first` does **not** rebuild SDL: the few minutes it takes are a
one-time cost per checkout. The focused Devices verification targets are `CnaDevicesTests`,
`CnaDevicesExtTests` and `cna_strict_xna_api_check`.

## 1. Desktop debug build (Linux, `OPENGLES3` renderer)

```bash
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON -DCNA_DEVICES=ON \
      -DCMAKE_BUILD_TYPE=Debug

cmake --build cmake-build-debug --target CnaDevicesTests --parallel 8
```

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
never a live desktop:

```bash
cmake --build cmake-build-debug --target CnaDevicesExtTests --parallel 8
tools/platform/run_gpu_tests_private.sh --exec cmake-build-debug/CnaDevicesExtTests
```

Use `./cmake-build-debug/CnaDevicesTests --gtest_list_tests` when an exact current test
inventory is needed; do not copy a count or suite list into this document. Tests that
genuinely require unavailable hardware remain in the executable and call `GTEST_SKIP()`.

**Concurrency tests in this suite are stress tests, not single-shot checks — a single
green run does not prove a concurrency fix is correct.** A reproducible heap corruption in the
concurrent-construction tests (P6-1) surfaced only after looping the same binary tens of times; the
cross-class mutex fix (P7-1) needed a stress test that constructs, destroys and probes
`Accelerometer` and `Gyroscope` concurrently; and the dispatch use-after-free fix (P7-3) was shown
real by reverting it and watching its regression test crash every time — a technique worth using
whenever a new regression test passes on the first try. If you change anything touching
`Detail::PlatformSensorSubsystem<TSensor>`, `Accelerometer`, or `Gyroscope`, re-run the relevant
`--gtest_filter` in a loop (20–60 iterations) before trusting a single pass:

```bash
cd cmake-build-debug
for i in $(seq 1 40); do
  ./CnaDevicesTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*" > stress-$i.log 2>&1 || echo "run $i FAILED"
done
```

**Exception-swallowing policy in sensor dispatch (Task P8-5):**
`Detail::PlatformSensorSubsystem<TSensor>::DispatchToInstances()` catches and swallows *any*
exception a `CurrentValueChanged`/`ReadingChanged` handler throws, per instance, and continues
dispatching to the rest of the batch. This is deliberate: the selected platform session may invoke
through a C callback boundary that does not permit a C++ exception to unwind through its frames,
and swallowing it there lets a later instance in the same batch still receive its event.
`ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent` (P8-5) proves the
batch continuation; `ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose`
(P6-4) proves that a throwing handler neither corrupts its own instance's dispatch tracking nor
hangs a later `Dispose()`. A game's handler should not rely on an exception it throws propagating
anywhere — it never will.

## 3. Full test suite

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-debug --output-on-failure -j4
```

For Devices changes, run the focused executables in Section 2 first. The repository-wide
suite includes GPU/window tests, so the full-suite command uses the private runner. Its total
count is intentionally not frozen here.

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
is a **compile-only** verification: no APK packaging and no emulator/device run. The minimum API
level is 24 (see [devices-android.md](devices-android.md)). The Android-only sensor sources live
under `modules/devices/src/Sensors/`; confirm their objects were produced rather than relying on a
host build that excludes `__ANDROID__`:

```bash
find cmake-build-android/modules/devices -name 'AndroidSensorBridge.cpp.o' \
    -o -name 'AndroidMotionBackend.cpp.o' -o -name 'AndroidSensorWorkerLifecycle.cpp.o'
```

### 4.1 APK packaging and emulator/device run

`cna_demo_devices` packages into an installable Android APK through the Gradle project checked in
under `modules/devices/examples/demo_devices/android/`. It was generated from SDL's own template;
re-run this only when regenerating:

```bash
python3 third_party/SDL/build-scripts/create-android-project.py \
  --variant copy \
  --output modules/devices/examples/demo_devices/android \
  com.openeggbert.cna.demodevices \
  modules/devices/examples/demo_devices/src/Main.cpp modules/devices/examples/demo_devices/src/DevicesDemo.cpp \
  modules/devices/examples/demo_devices/src/DevicesDemo.hpp
```

Rules of that project:

- The template's own vendored SDL copy (`app/jni/SDL`) is deleted. `app/jni/CMakeLists.txt`
  reuses CNA's root project (`add_subdirectory(<cna-root> cna_build)` with
  `CNA_BUILD_TESTS`/`CNA_BUILD_EXAMPLES` forced `OFF`) and `app/jni/src/CMakeLists.txt` links
  `main` against `CNA`/`SHARP_RUNTIME`, so SDL3 is never configured twice and no `SDL3::SDL3`
  target collides.
- The root `CNA` target has a `PUBLIC android` link on `ANDROID`: `Detail::AndroidSensorBridge`'s
  NDK `ASensorManager`/`ALooper` calls need `libandroid.so` in every executable/shared-library
  consumer.
- `app/build.gradle` pins `ndkVersion` and uses `ANDROID_PLATFORM=android-24`/`minSdkVersion 24`,
  the project's minimum.
- `Main.cpp` includes `<SDL3/SDL_main.h>`: on Android `SDLActivity.java` finds the entry point via
  `dlsym(RTLD_DEFAULT, "SDL_main")`, so without the `#define main SDL_main` redirection the app
  fails with `Couldn't find function SDL_main in library libmain.so`. The include is a no-op on
  desktop.
- `--variant copy` duplicates source files into `app/jni/src/`; edits to
  `modules/devices/examples/demo_devices/src/*` do **not** propagate. Re-copy (or regenerate)
  after any source change.
- The manifest additions (sensor `uses-feature` declarations, `HIGH_SAMPLING_RATE_SENSORS`) and
  their reasons are in [devices-android.md](devices-android.md). An XML comment must not contain a
  bare `--`; `ManifestMerger2` rejects it.

**Build it:**
```bash
cd modules/devices/examples/demo_devices/android/com.openeggbert.cna.demodevices
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
export ANDROID_HOME="$HOME/Android/Sdk"
./gradlew -PBUILD_WITH_CMAKE assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk
```

**Run it on an emulator** (x86_64 emulation needs `/dev/kvm`; Task DEVICES-0126):

```bash
~/Android/Sdk/emulator/emulator -avd Medium_Phone -no-window -no-audio -gpu swiftshader_indirect -no-snapshot
# adb devices -> emulator-5554  device   (boots in about one to two minutes)

adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.openeggbert.cna.demodevices/com.openeggbert.cna.demodevices.DemodevicesActivity
```

Stop the emulator when finished (`adb emu kill`); a leftover headless emulator keeps consuming CPU.
`adb logcat` should show `SDL: Running main function SDL_main from library .../libmain.so`, and
`adb shell screencap` the demo's per-sensor indicator squares and value bars. Synthetic values
injected through the emulator console (`sensor set acceleration 0:9.81:3.0`,
`sensor set magnetic-field 30:5:40`) light the demo's event-flash indicator, showing that sensor
events flow through the Android runtime pipeline. That run exercises SDL3's Android
accelerometer/gyroscope path only: the emulator console offers no rotation-vector sensor to inject,
so Compass/Motion's `Detail::AndroidSensorBridge` path is only built and launched. The emulator
proves the software pipeline, not physical behavior; its limits are in
[devices-hardware-checklist.md](devices-hardware-checklist.md) §9, and no physical Android device
has been used.

## 5. iOS — not built

Building for iOS requires macOS and Xcode; there is no Linux cross-toolchain, so nothing iOS-specific
has been compiled. The designed iOS Compass, Motion and vibration backends are in
[devices-native-backend-design.md](devices-native-backend-design.md).

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
change touches `modules/devices-ext/`. Sanitizer findings belong in `KNOWN_ISSUES.md`, not as
fixed test counts in this command guide.

## 7. Continuous Integration (`DEV-BUILD-003`)

`.github/workflows/devices-tests.yml` builds `CnaDevicesTests`, `CnaDevicesExtTests`, and
`cna_strict_xna_api_check` on every push/PR that touches the Devices module paths or
their build configuration, plus on manual `workflow_dispatch`. It runs on a plain
`ubuntu-latest` GitHub-hosted runner without physical sensor or haptic hardware.
Hardware-dependent tests are not filtered out; they self-skip when unsupported.

The job:
- checks out this repo fresh with non-recursive submodules (Section 0), plus the three sibling
  repos (`sharp-runtime`, `easy-gl`, `meta-gl`) into sibling directories, since `CMakeLists.txt`
  resolves them via `add_subdirectory(../x)`; the `FATAL_ERROR` guards above make an empty vendored
  directory fail loudly, so the job is also the clean-room reproduction check;
- installs the Ubuntu SDL3 build dependencies from `third_party/SDL/docs/README-linux.md` (plus
  `libwayland-dev`/`libdecor-0-dev`) and the FFmpeg dev packages for the optional VideoPlayer
  backend, which a device-only build does not need (`CNA_ENABLE_VIDEO=AUTO` uses them when present,
  `OFF` omits them; see `docs/video-backend.md`);
- caches `.sdl-prebuilt-Linux-x86_64/` keyed on the submodules' tracked commits, so a submodule
  bump invalidates it and other pushes reuse the first-time SDL build;
- configures and builds with the `devices-ubsan` preset (Section 6), so every run also gets
  UndefinedBehaviorSanitizer coverage;
- runs the complete `CnaDevicesTests` executable directly, without a copied suite list;
- runs `CnaDevicesExtTests` separately under Xvfb because its camera tests create a
  graphics device on the hosted runner;
- builds and runs `cna_strict_xna_api_check` as its own step (`DEVPERF-001`); it is registered as
  the separate `StrictXnaApiSurfaceCheck_Compile_Run` CTest (`cmake/Harnesses.cmake`), not part of
  the focused gtest executables.

Consult the Actions run for the current hosted-runner result; this document does not
freeze a run ID, test count, or copied filter that can drift from the executables.
