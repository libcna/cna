# Building and Testing `Microsoft::Devices` — Reproducible Commands

Every command below was actually run in a session working on `Microsoft::Devices`
(`plan_devices_phase4.md`/`plan_devices_phase5.md`/`plan_devices_phase6.md`/
`plan_devices_phase7.md`), not copy-pasted from documentation without running it.
Where a command's success is asserted, it was verified in this repository, on
this branch, this session.

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
`plan_devices_phase7.md`) — compiled and tested locally in this session's
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
# 220 tests, 100% passing, as of plan_devices_phase7.md Task P7-7 (last verified this way).

# Or directly via the test binary's own gtest filter — narrower: only the 9
# "main" per-class suites, not the Reading/EventArgs/FailedException ones:
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*:VibrateControllerTests.*:SensorSubsystemOwnershipTests.*:AndroidSensorOrientationTests.*:SensorBaseTests.*:ScopeExitTests.*"
# 140 tests, 138 passing, as of plan_devices_phase7.md Task P7-7 (last verified this way).
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

## 3. Full test suite

```bash
cd cmake-build-debug && ctest --output-on-failure
```

As of this writing: 2045 tests, 2 failures — both pre-existing, unrelated `EasyGL`/
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

## 5. iOS — confirmed still blocked, not attempted

No Apple/iOS toolchain of any kind exists in this Linux dev container — confirmed by
actually checking (`plan_devices_phase4.md` Task P4-12, re-confirmed
`plan_devices_phase5.md` Task P5-1's audit, `plan_devices_phase6.md` Task P6-10, and
`plan_devices_phase7.md` Task P7-7): no `xcodebuild`, no `xcrun`, no `osxcross`,
nothing matching `*ios*toolchain*` anywhere on the filesystem. Unlike Android (a
missing NDK package, which this session found had since been installed), iOS
cross-compilation fundamentally requires macOS/Xcode to obtain and run its own
toolchain — not fixable by installing a package in a Linux container. Re-check before
assuming this is still true in a future session (environments can change, as Android's
did), but don't expect it to resolve the way Android's did.
