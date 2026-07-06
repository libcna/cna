# CNA Devices and Sensors XNA 4.0 Compatibility Plan

**Written from scratch on 2026-07-05.** This document fully replaces every prior version
of `plan_devices.md` (the original 31-task plan, `plan_devices_phase2.md` through
`plan_devices_phase9.md`, and the second 143-task/Phases-0-10 plan). None of those task
IDs, phase numbers, "done" claims, or assumptions carry over. Prior plans are historical
record only — see git history if that detail is ever needed — and are not a source of
truth for this plan. Every task below was written after inspecting the current state of
the code, headers, tests, demo, and build files in this repository, not by trusting any
earlier plan's status text.

This plan does not implement anything. It only defines the work. No task listed here has
been carried out as part of writing this plan.

**Terminology note, stated once here and assumed throughout the rest of this document:**
the correct XNA 4.0 / Windows Phone class name is `Microsoft::Devices::VibrateController`.
"VibrationController" is not the XNA API name and must not be used as the public class
name, in documentation, or in code — see Section 4 (`VIB-001`) for the audit task that
enforces this everywhere else in the repository.

---

## 0. Scope

This plan covers:

- `Microsoft::Devices::VibrateController`
- `Microsoft::Devices::Sensors::SensorBase<TSensorReading>`
- `Microsoft::Devices::Sensors::Accelerometer`
- `Microsoft::Devices::Sensors::Gyroscope`
- `Microsoft::Devices::Sensors::Compass`
- `Microsoft::Devices::Sensors::Motion`
- Sensor reading structs and event-args classes (`AccelerometerReading`,
  `GyroscopeReading`, `CompassReading`, `MotionReading`, `AttitudeReading`,
  `AccelerometerReadingEventArgs`, `SensorReadingEventArgs<T>`, `CalibrationEventArgs`,
  `ISensorReading`, `SensorState`, `SensorFailedException`,
  `AccelerometerFailedException`)
- Android sensor backends (`Detail::AndroidSensorBridge`, `Detail::AndroidCompassBackend`,
  `Detail::AndroidMotionBackend`, `Detail::AndroidCompassMath`, `Detail::AndroidMotionMath`,
  `Detail::AndroidSensorOrientation`)
- SDL sensor backends (`Detail::SdlSensorSubsystem<TSensor>`, used by `Accelerometer` and
  `Gyroscope`)
- Platform vibration/haptic backends (currently SDL3 haptic only — see Section 4)
- Tests, demos, docs, and CI for all of the above

This plan does not cover unrelated input, audio, graphics, or gamepad APIs, except where
they interact with vibration or sensor routing (e.g. `VibrateController` deliberately
excluding gamepad haptic devices so it never competes with
`Microsoft::Xna::Framework::Input::GamePad::SetVibration()`).

---

## 1. Baseline observations (verified by inspection, 2026-07-05)

These are facts confirmed by reading the current repository — not carried over from any
old plan's claims. Where a fact could not be directly confirmed by reading code, it is
stated as unverified rather than assumed.

- **`VibrateController` is the correct XNA name, already used correctly as the public
  class name** in `include/Microsoft/Devices/VibrateController.hpp` and
  `src/Microsoft/Devices/VibrateController.cpp`. It currently has exactly one backend:
  SDL3's `SDL_Haptic` API (`SDL3/SDL_haptic.h`). There is no dedicated phone-vibrator
  backend, no `IVibrateBackend` abstraction, and no Android JNI vibrator bridge — SDL3's
  own bundled Android haptic backend is relied on to reach `Context.VIBRATOR_SERVICE`
  (documented in a comment in `VibrateController.cpp` above `OpenFirstHapticDevice()`).
  `Start(TimeSpan)` is the strict XNA method; `Start(TimeSpan, float)`,
  `getIsSupportedProperty()`, `getDeviceNameProperty()`, and `StartLeftRight(...)` are
  all marked `NOXNA` in the header.
- **`Accelerometer` and `Gyroscope` are SDL3-backed** via the shared
  `Detail::SdlSensorSubsystem<TSensor>` template
  (`include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`). `Accelerometer`
  additionally raises a legacy `ReadingChanged` event alongside `CurrentValueChanged`
  (`AccelerometerReadingEventArgs`); `Gyroscope` only raises `CurrentValueChanged`.
- **`Compass` and `Motion` are real on Android only**, via
  `Detail::AndroidCompassBackend`/`Detail::AndroidMotionBackend`, both built on the
  shared `Detail::AndroidSensorBridge` (a pure-NDK `ASensorManager`/`ASensorEventQueue`/
  `ALooper` wrapper, no JNI). On every other platform both classes keep the same
  permanent stub behavior they always had (`getIsSupportedProperty()` false,
  `Start()` throws). Neither has an iOS backend.
- **`getStateProperty()`'s `NOXNA` split across the four sensor classes is intentional,
  not drift — re-confirmed 2026-07-06 (`DEV-API-003`).** `Accelerometer::getStateProperty()`
  has no `NOXNA` marker while `Gyroscope`/`Compass`/`Motion`'s equivalents all do; this
  plan's Section 1 draft (written before that re-check) flagged the shape difference as
  unexplained drift, but a prior pass (`plan_devices_phase2.md` Task P2-17, 2026-07-02)
  had already resolved the underlying question against archived MSDN "previous-versions"
  pages: `Accelerometer.State` is real WP7 API (MSDN `ff707930`, cited in `AUDIT.md`'s
  `Accelerometer` row and `docs/devices-api-coverage.md`), while `Gyroscope.State`
  (`hh239201`), `Compass.State` (`hh220912`), and `Motion.State` (`hh239189`) do not exist
  on the real classes — so their `getStateProperty()` is correctly a CNA symmetry
  extension. No code change needed; see `DEV-API-003`'s closing note.
- **Fixed 2026-07-06 (`SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/`SDL-SENSOR-002`):**
  `TimeBetweenUpdates` was not enforced by the SDL backends at all —
  `src/Microsoft/Devices/Sensors/Accelerometer.cpp`/`Gyroscope.cpp` had zero references
  to `getTimeBetweenUpdatesProperty()`. Both now call the new
  `SensorBase<T>::ShouldAcceptUpdateAt()` from their `ProcessSensorUpdateEvent()`, a
  per-instance, mutex-guarded software throttle (SDL3 has no polling-rate control API
  for these sensor types) — see those tasks' closing notes for full detail.
- **Fixed 2026-07-06 (`ANDROID-BRIDGE-002`):** `Detail::AndroidCompassBackend`/
  `Detail::AndroidMotionBackend` previously only forwarded `timeBetweenUpdates` into
  `Detail::AndroidSensorBridge::Start()` once, at `Start()` time. `AndroidSensorBridge`
  now has `SetSampleInterval()`, which re-applies `ASensorEventQueue_setEventRate()` on
  the live queue from the worker thread that owns it; `Compass`/`Motion` forward their
  own `TimeBetweenUpdatesChanged` event to it. See that task's closing note for full
  detail, including the Android cross-compile + `llvm-nm` symbol check (no real
  hardware/emulator run this session).
- **Verified: two different "sensor failed" exception types are in use.** `Accelerometer`
  throws its own dedicated `AccelerometerFailedException`
  (`include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp`), while
  `Gyroscope`, `Compass`, and `Motion` all throw the generic `SensorFailedException`
  (`include/Microsoft/Devices/Sensors/SensorFailedException.hpp`) — confirmed by
  grepping `throw` sites in all four `.cpp` files. Whether this split is intentional
  (`AccelerometerFailedException` being the one real XNA/WP7 exception type, with
  `SensorFailedException` a CNA-invented stand-in for the other three, which may not
  have a real XNA equivalent at all) has not been verified against any authoritative
  XNA/WP7 API reference. See `DEV-API-005`.
- **Verified: `Compass::TrueHeading` is deliberately set equal to `MagneticHeading`.**
  `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  (`PublishReading()`) has an explicit comment stating this is intentional because real
  declination requires a location source this codebase does not implement. This is a
  documented current behavior, not a bug — but whether it is the *correct* XNA-compatible
  behavior for "declination unknown" has not been verified against any authoritative
  reference. See `COMPASS-002`.
- **Verified: `Compass::getIsSupportedProperty()` requires both a rotation-vector sensor
  and a magnetic-field sensor** (`AndroidCompassBackend::IsSupported()` returns
  `rotationVectorBridge_.IsAvailable() && magneticFieldBridge_.IsAvailable()`). Devices
  with only a magnetometer are reported unsupported today. See `COMPASS-005`.
- **Verified: no CI infrastructure exists in this repository.** `.github/workflows/`
  does not exist. All verification in this repository today is manual, local
  `cmake`/`ctest` invocation. See `DEV-BUILD-003`.
- **Verified: three sanitizer CMake presets already exist** —
  `devices-asan`, `devices-tsan`, `devices-ubsan` — confirmed in `CMakePresets.json`
  (both as configure and matching build presets, alongside `web` and `tests`). These
  are reused directly in Section 14's verification commands rather than inventing new
  preset names.
- **Verified: this environment has all four required submodules/vendored dependencies
  present** (`third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer`,
  `vendor/googletest`, confirmed via `git submodule status`) — this specific checkout is
  not a bare ZIP export. Whether a *fresh* clone reliably reaches this same state with
  clearly actionable errors if a submodule is missing has not been re-verified as part
  of writing this plan. See `DEV-BUILD-001`.
- **The test suite is extensive by file count** — 21 test files under
  `tests/Microsoft/Devices/` covering every reading struct, event-args class, exception
  type, `SensorBase<T>` itself, the SDL ownership/dispatch machinery
  (`SensorSubsystemOwnershipTests.cpp`), the Android bridge and its pure-math helpers
  (`AndroidSensorBridgeTests.cpp`, `AndroidCompassMathTests.cpp`,
  `AndroidMotionMathTests.cpp`, both under `tests/Microsoft/Devices/Sensors/Detail/`),
  and `VibrateController`. Whether these tests currently build and pass in a given
  environment must still be confirmed by actually running them (Section 14) — file
  count alone is not evidence of passing behavior, and this plan does not claim any
  test currently passes without having run it.
- **Hardware orientation/coordinate-mapping correctness for `Accelerometer`,
  `Gyroscope`, `Compass`, and `Motion` has never been verified against real Android (or
  any) hardware** in any session that produced the prior plans — this is stated
  explicitly in this repository's own `NEXT.md` and `docs/devices-hardware-checklist.md`,
  and nothing found while writing this plan contradicts that. Treat every axis-sign,
  unit-conversion, and heading/attitude claim in Sections 6–9 as needing real-device
  confirmation, not as already-settled.
- **Existing docs relevant to this plan** (to be updated by the tasks that touch them,
  not duplicated): `docs/devices-build.md`, `docs/devices-hardware-checklist.md`,
  `docs/devices-native-backend-design.md`, `docs/devices-api-coverage.md`,
  `docs/devices-android.md`. None of these currently contain a strict XNA-vs-`NOXNA`
  public API matrix, a phone-vibrator-vs-desktop-haptic backend split writeup, or a CI
  description — those are new artifacts this plan's tasks produce.

Nothing in this section should be read as "these are the only problems" — it is the set
of facts that grounded the specific tasks below, not an exhaustive audit result.

---

## 2. Build and verification bootstrap tasks

### DEV-BUILD-001 — Restore reproducible local build — CLOSED (2026-07-06)

- **Priority:** Critical
- **Area:** Build / CI
- **Problem:** Device/sensor tests cannot be trusted until the project builds
  reproducibly from a clean checkout. This environment already has submodules
  initialized, but that has not been re-verified from a genuinely fresh clone as part of
  this plan.
- **Resolution (2026-07-06):** actually did the fresh-clone test this task calls for
  (every prior pass had only ever run in an environment with everything already
  provisioned) and found two real, previously-undocumented gaps:
  - **Major finding: this repo depends on `sharp-runtime` and `easy-gl` (which itself
    depends on `meta-gl`) as sibling repository checkouts, referenced via
    `add_subdirectory(../sharp-runtime)`/`add_subdirectory(../easy-gl)` in
    `CMakeLists.txt` — not git submodules.** `git submodule update --init[--recursive]`
    cannot fetch them; a genuinely fresh clone of just this repo, even with its own
    submodules fully initialized, **fails to configure at all**. This was completely
    undocumented anywhere in this repo before this pass — confirmed by reproducing the
    failure from an actual fresh clone with no siblings present. Fixed:
    - `CMakeLists.txt` now checks for `../sharp-runtime/CMakeLists.txt` and
      `../easy-gl/CMakeLists.txt` before each `add_subdirectory()` call, failing with an
      actionable `FATAL_ERROR` (names the missing sibling, states it's a separate
      checkout not a submodule, gives the exact `cd .. && git clone
      https://github.com/openeggbert/<repo>.git` fix) instead of CMake's own generic
      "given source ... which is not an existing directory". Verified the new message
      actually fires correctly by reproducing the original failure with the fix in
      place.
    - `docs/devices-build.md` Section 0 rewritten to document the full three-repo
      sibling chain (`sharp-runtime`, `easy-gl`, `meta-gl`), confirmed `meta-gl` has no
      further transitive dependencies of its own.
  - **Second finding: the existing `git submodule update --init --recursive` guidance
    (in both `cmake/ThirdPartySDL.cmake`'s error message and `docs/devices-build.md`)
    was itself misleading/harmful.** `--recursive` additionally attempts to clone ~19
    unneeded nested codec submodules under `SDL_image`'s/`SDL_mixer`'s own `external/*`
    (AVIF, JXL, WebP, libpng, GME, mod_xmp, mpg123, FluidSynth-MIDI, Opus, Vorbis) that
    this project's own `SDLIMAGE_*`/`SDLMIXER_*` CMake args all explicitly disable —
    measured directly: the correct, non-recursive `git submodule update --init` took
    ~6.5 minutes in this environment; the `--recursive` form was still running,
    unfinished, past 2 more minutes on top of that just for the extra unneeded fetches.
    Fixed the error message in `cmake/ThirdPartySDL.cmake` to recommend the correct,
    non-recursive form with the reasoning inline; updated `docs/devices-build.md`
    Section 0 to match.
  - **Full fresh-clone verification actually performed, not assumed:** fresh `git
    clone` of this repo (new directory, not this working checkout) + fresh sibling
    clones of `sharp-runtime`/`easy-gl`/`meta-gl` + `git submodule update --init` (timed,
    ~6.5 min) + `cmake --preset devices-ubsan` (configured clean, first-time vendored
    SDL3/SDL_image/SDL_mixer build succeeded) + `cmake --build --preset devices-ubsan
    --target CnaTests -j$(nproc)` (built clean from scratch, ~3m42s) + a spot-check
    `SensorBaseTests.*:AccelerometerTests.*` run (55 passed, 1 expected hardware skip).
    Confirmed the main working checkout still builds/configures identically after these
    CMake changes (existence checks are no-ops when siblings are already present).
    Scratch clone directories cleaned up afterward.
- **Required work:**
  - Verify required submodules and vendored dependencies from an actually fresh clone
    (`git clone --recurse-submodules`, or `git submodule update --init --recursive`
    after a plain clone). Done — and found `--recursive` itself was the wrong
    recommendation; corrected to non-recursive.
  - Add or update bootstrap instructions for SDL, SDL_image, SDL_mixer, googletest, and
    any platform-specific sensor dependencies (Android NDK path, minimum API level).
    Done for SDL/SDL_image/SDL_mixer/googletest and the newly-found sibling-repo
    requirement; Android NDK path/API level bootstrap instructions already existed in
    `docs/devices-build.md` Section 4 from a prior pass, not touched by this task.
  - Make missing-dependency CMake configure errors actionable (clear message naming the
    missing submodule/package and the exact command to fix it), instead of a generic
    "file not found" from deep inside a `third_party/` include path. Done — for the
    sibling-repo case, which turned out to be the actual blocking gap; the existing
    `third_party/` submodule check in `cmake/ThirdPartySDL.cmake` already had an
    actionable message (just a wrong recommended command, now fixed).
- **Acceptance criteria:**
  - A clean checkout can be prepared with documented commands that another engineer can
    copy-paste without guessing. Done — `docs/devices-build.md` Section 0.
  - `cmake --preset devices-ubsan` configures successfully from that clean checkout.
    Done, verified directly.
  - `CnaTests` builds successfully from that clean checkout. Done, verified directly.
- **Suggested files to inspect or edit:**
  - `CMakeLists.txt` (edited)
  - `cmake/ThirdPartySDL.cmake` (edited)
  - `CMakePresets.json` (inspected, no change needed)
  - `third_party/` (submodule pointers only — not edited)
  - `vendor/` (same caveat — not edited)
  - `docs/devices-build.md` (edited)
  - `plan_devices.md` (edited)

### DEV-BUILD-002 — Add device-only verification commands — CLOSED (2026-07-06)

- **Priority:** High
- **Area:** Build / Tests
- **Problem:** There is no single documented command sequence specifically for
  Devices/Sensors verification with sanitizer variants included; `docs/devices-build.md`
  exists but must be checked against the actual current test-suite filter (a stale
  filter that silently drops new test suites has happened before in this repository's
  history).
- **Resolution (2026-07-06):** the concern was justified — the filter documented in
  `docs/devices-build.md`/`NEXT.md` *was* stale. Ground truth was established directly:
  every `TEST(...)` in `tests/Microsoft/Devices/` (21 suites, 283 cases, no
  `TEST_F`/`TEST_P` in this scope), via
  `grep -rE '^(TEST|TEST_F|TEST_P)\(' tests/Microsoft/Devices`. Diffing that against the
  old substring filter (`Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|...`)
  found two real problems:
  - **Silently dropped `CalibrationEventArgsTests`** (3 tests — no substring in the old
    filter matched its suite name).
  - **Matched 2 unrelated false positives outside `Microsoft::Devices`:**
    `GamePadTest.GetAccelerometerEXTReturnsFalseAndZeroesOutputWhenNoGamePadConnected`
    and `SdlInputBridgeTouchGestureTest.FingerMotionThroughProcessEventProducesFlick`.
  - Replaced with an exact-suite-name filter (`docs/devices-build.md` Sections 2/6,
    `NEXT.md` Section 7) — verified via diff to match all 283 cases, zero false
    positives. Re-ran the corrected filter on plain `cmake-build-debug` and all three
    sanitizer presets: 283/283 (281 passed + 2 expected hardware skips) on all four;
    sanitizer findings unchanged from previously known (0 ASan; 41 TSan reports, all
    the same pre-existing `sharp-runtime` `TimeSpan::copy_count` race; 3 UBSan reports,
    all the same pre-existing `Vector3`/`Matrix::GetHashCode()` signed-overflow finding,
    0 in `Microsoft::Devices` itself).
  - No production code changed — this task was documentation/verification-only.
- **Suggested files inspected/edited:**
  - `docs/devices-build.md` (edited)
  - `NEXT.md` (edited)
  - `CMakePresets.json` (inspected, unchanged)
  - `tests/Microsoft/Devices/` (full recursive listing, used to build the filter)

### DEV-BUILD-003 — Add CI job for Devices/Sensors — CLOSED (2026-07-06, workflow added and verified locally; not yet observed green on an actual GitHub Actions runner)

- **Priority:** High
- **Area:** CI
- **Problem:** No CI infrastructure exists in this repository at all (`.github/workflows/`
  is absent) — sensor/vibration regressions can currently slip in with zero automated
  gate.
- **Resolution (2026-07-06):** added `.github/workflows/devices-tests.yml` — a single
  `build-and-test` job on `ubuntu-latest`, triggered on push/PR paths touching
  `include/Microsoft/Devices/**`, `src/Microsoft/Devices/**`,
  `tests/Microsoft/Devices/**`, or the top-level CMake files, plus manual
  `workflow_dispatch`. Full detail in `docs/devices-build.md` Section 8 (new): checks
  out this repo (non-recursive submodules, per `DEV-BUILD-001`) plus the three sibling
  repos (`sharp-runtime`, `easy-gl`, `meta-gl`) as true siblings on the runner,
  installs the Ubuntu SDL3 build dependencies from `third_party/SDL`'s own documented
  list plus this project's FFmpeg dev packages, caches the vendored
  `.sdl-prebuilt-Linux-x86_64/` tree keyed on submodule commits, configures/builds with
  the existing `devices-ubsan` preset (so every CI run also gets UBSan coverage for
  free), and runs `CnaTests` directly with the exact-suite-name filter from
  `DEV-BUILD-002`. Verified locally in this session that the filter's 313/313 (311
  passed + 2 expected hardware skips) result holds on plain `cmake-build-debug`
  (matches the already-established sanitizer results from `DEV-BUILD-002`).
  **Hardware-test handling:** the two hardware-dependent tests
  (`AccelerometerTests`/`GyroscopeTests` `.GetCurrentValuePropertyDoesNotThrowWhenSupported`)
  are **not** excluded from the CI filter — both already call `GTEST_SKIP()` internally
  when `getIsSupportedProperty()` is false (confirmed by reading both test bodies), so a
  hardware-free CI runner reports them `SKIPPED` automatically, exactly like this local
  container does; no separate hardware-only filter was needed to satisfy the "excluded
  with a clear comment" acceptance criterion — the workflow file's own header comment
  explains this instead of maintaining a second filter.
  **Honestly stated limitation:** the workflow file has not yet actually executed on a
  real GitHub Actions runner in this session (no push to a remote branch that would
  trigger it) — every individual command it runs was independently verified locally,
  and the apt package list is copied directly from `third_party/SDL`'s own documented
  Ubuntu requirements rather than guessed, but the workflow file's *end-to-end* run on
  GitHub's actual infrastructure is unconfirmed. Worth checking the Actions tab after
  the first push that includes it, and treating this task as re-opened if that first
  run fails for an environment reason (missing package, cache-action quota, etc.) that
  local verification couldn't catch.
- **Required work:**
  - Add CI (e.g. GitHub Actions) to build and run Devices/Sensors tests on at least one
    desktop platform. Done.
  - Ensure no real hardware is required for the default CI path — fake/injected
    backends only (see `ACCEL-006`, `GYRO-005`, `VIB-009`). Done — the two genuinely
    hardware-dependent tests self-skip; everything else in the filter already runs
    hardware-free.
  - Clearly mark any hardware-dependent test as manual/integration-only, excluded from
    the default CI filter. Done differently than originally phrased — not excluded from
    the filter, but self-skipping, with the workflow file documenting why that's
    sufficient.
- **Acceptance criteria:**
  - CI has a Devices/Sensors job that runs on every push/PR touching these paths. Done.
  - CI runs all pure unit tests without physical sensors or haptic hardware present.
    Done, verified locally; not yet observed on an actual runner (see limitation above).
  - Hardware tests are excluded from the CI filter with a clear comment explaining why.
    Done via self-skip + workflow-file comment, not a separate filter.
- **Suggested files to inspect or edit:**
  - `.github/workflows/devices-tests.yml` (new)
  - `tests/Microsoft/Devices/` (inspected, no changes needed)
  - `tests/Microsoft/Devices/Sensors/` (inspected, no changes needed)
  - `docs/devices-build.md` (edited — new Section 8)

### DEV-BUILD-004 — Root-cause and fix `cna_demo_devices`'s Android build gap — CLOSED (2026-07-06)

- **Priority:** High
- **Area:** Build
- **Problem:** `NEXT.md` Section 4 tracked a reproducible, not-yet-root-caused gap:
  `cmake --build cmake-build-android --target cna_demo_devices` failed with
  `examples/demo_devices/src/Main.cpp:1:10: fatal error: 'SDL3/SDL_main.h' file not
  found`. Reproduced identically across three prior verification passes, never
  investigated further.
- **Root cause (confirmed 2026-07-06):** `CMakeLists.txt` links `SDL3::SDL3` to `CNA`
  as `PRIVATE` (line ~222) — a deliberate choice, since `CNA` hides its graphics/SDL
  backend behind `IGraphicsBackend` and should not leak SDL3 to every consumer (per
  this project's own architecture). `cna_demo_devices` only links `CNA` (never SDL3
  directly), so it never received SDL3's include directories transitively — yet its
  `Main.cpp` `#include <SDL3/SDL_main.h>` directly (added for the Android
  `SDLActivity.java`/`dlsym("SDL_main")` requirement, `plan_devices.md` Task
  DEVICES-0125/0126). **This has always been broken** — it only ever "worked" on
  desktop by pure accident: this container's host compiler default include path
  (`/usr/local/include`) happens to carry a coincidentally-installed system SDL3 dev
  package, masking the missing project-level include path. Cross-compiling for
  Android has no such host-path fallback and surfaces the real gap. Confirmed by
  directly inspecting the actual compiler invocation (`make VERBOSE=1`) for both
  platforms — neither ever passed an explicit SDL3 `-I` flag for this target.
- **A second, deeper problem, found only after fixing the first:** fixing the missing
  include (below) revealed the real Android cross-compile then fails at the *link*
  step instead, with `ld.lld: error: undefined symbol: main`. Root cause: SDL's own
  `<SDL3/SDL_main.h>` `#define`s `main` to `SDL_main` on Android (`SDL_MAIN_NEEDED`),
  which leaves no literal `main` symbol for a plain ELF executable's C runtime startup
  (`crtbegin_dynamic.o`) to link against — that redirection is designed for a shared
  library an `Activity` loads via JNI/`dlsym`, not a standalone native executable.
  **A plain `add_executable()` was therefore never a valid Android app target format
  for this demo, full stop** — independent of any include-path fix. The actual working
  Android build of this same demo (confirmed real, launched, screenshotted —
  `docs/devices-build.md` Section 4.1) is an entirely separate Gradle/CMake project
  under `examples/demo_devices/android/`, which compiles the same
  `Main.cpp`/`DevicesDemo.cpp` into a genuine shared library (`libmain.so`) via its own
  `app/jni/src/CMakeLists.txt` — never through this top-level `cna_demo_devices` target
  at all. `NEXT.md`'s prior "this is a regression, it worked once before" framing was
  a mix-up between these two independent build paths, not an actual regression in
  either one.
- **Resolution:**
  - Added an explicit `target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3)` —
    fixes the include-path gap on **every** platform (not just Android), removing the
    desktop build's reliance on host-system luck. Kept even though it alone isn't
    sufficient for Android, since it's a genuine, independent correctness fix.
  - Wrapped `cna_demo_devices`'s entire target definition in `if(NOT ANDROID)`,
    matching this project's own precedent for demo targets that cannot make sense as
    plain Android executables. Documented directly in the `CMakeLists.txt` comment why,
    with a pointer to the real Gradle-based Android build path.
  - Verified: desktop (`cmake-build-debug`) `cna_demo_devices` still builds and links
    clean, unaffected. Android (`cmake-build-android`) `--target CNA` still builds
    clean (unaffected — this task never touched `CNA`'s own target definition).
    `--target cna_demo_devices` now reports "no work to do" (the target no longer
    exists for this platform) instead of failing.
  - **New, out-of-scope finding, not fixed by this task:** a full, untargeted
    `cmake --build cmake-build-android` (i.e. building every target, not just `CNA`)
    still fails — on `cna_demo_input` this time (`MouseCursor.hpp:8:10: fatal error:
    'SDL3/SDL.h' file not found`), the identical root cause as above
    (`cna_demo_input` also only links `CNA`, never `SDL3::SDL3` directly). Every
    other demo executable (`cna_demo_2d`, `cna_demo_sound`, `cna_demo_xact`) likely has
    the same latent gap, unconfirmed. **Not fixed here** — out of this task's scope
    (`cna_demo_devices` specifically), and the documented Android verification gate
    (`docs/devices-build.md` Section 4, `--target CNA` only) never exercises any demo
    target, so this does not block that gate. Worth a dedicated future task if Android
    example-app cross-compilation beyond `cna_demo_devices` is ever needed.
- **Acceptance criteria:**
  - `cmake --build cmake-build-android --target cna_demo_devices` no longer fails.
    Done (target no longer attempted on this platform, by design).
  - Desktop build of `cna_demo_devices` unaffected. Confirmed.
  - Root cause documented, not just worked around. Done, in both this entry and the
    `CMakeLists.txt` comment itself.
- **Suggested files to inspect or edit:**
  - `CMakeLists.txt` (edited)

---

## 3. Public API compatibility audit tasks

### DEV-API-001 — Create official XNA public API matrix — CLOSED (2026-07-06)

- **Priority:** Critical
- **Area:** API Compatibility
- **Problem:** There is no explicit, single table comparing CNA's current public API in
  this area to XNA 4.0 / Windows Phone 7's actual API surface. `docs/devices-api-coverage.md`
  exists but was not written against a fresh, from-scratch audit for this plan and must
  be re-verified, not assumed current.
- **Resolution (2026-07-06):** read every public header in scope end-to-end
  (`VibrateController.hpp`; `SensorBase.hpp`; `Accelerometer`/`Gyroscope`/`Compass`/
  `Motion.hpp`; all five reading structs; `CalibrationEventArgs`/
  `AccelerometerReadingEventArgs`/`SensorReadingEventArgs.hpp`; `SensorState`/
  `ISensorReading.hpp`; `SensorFailedException`/`AccelerometerFailedException.hpp`) and
  cross-checked every public member against `docs/devices-api-coverage.md`'s existing
  content rather than trusting it. Result: **zero Missing, zero Extra-unmarked, 2
  Wrong-visibility (unverified)** findings — see that file's new "DEV-API-001
  verification result" section for the full accounting. Added two new "Cross-cutting
  members" tables (previously-implicit boilerplate — destructor/`Dispose()`/
  `Dispose(bool)`/`GetTypeName()` for the four sensor classes; constructor/getter/
  setter/equality/`ToString()`/`GetHashCode()`/`GetTypeName()` for the five reading
  structs — now explicitly tabulated instead of assumed), a "Flagged findings" section
  (the 2 wrong-visibility findings, cross-referenced to the already-existing
  `READINGS-002` task rather than fixed here), and extended the `Detail::` internals
  table with `SdlSensorSubsystem<TSensor>`/`GetGlobalSdlSensorMutex()`/`ScopeExit<F>`
  (existed in code, missing from that table). Explicitly re-confirmed this task's named
  example case — `getStateProperty()`'s `NOXNA` asymmetry — is not a bug (already
  resolved by `DEV-API-003`), not something this pass needed to newly catch.
- **Required work:**
  - Build a table with one row per public class, struct, method, property, enum, event,
    and exception in this plan's scope (Section 0). Done.
  - Mark each row as: strict XNA 4.0, Windows Phone 7 legacy (e.g. `ReadingChanged`),
    CNA `NOXNA` extension, or internal-only (should not be public at all). Done.
  - Include `VibrateController` explicitly — not "VibrationController" — as its own
    section of the table. Done (already present; verified still correct).
- **Acceptance criteria:**
  - The matrix exists (in `docs/devices-api-coverage.md` or a new file this task
    creates) and covers every public member currently declared in the headers listed
    below. Done — `docs/devices-api-coverage.md`, extended, not duplicated.
  - The matrix identifies at least the known drift already found in Section 1
    (`getStateProperty()`'s inconsistent `NOXNA` marking) as a concrete example of
    something it must catch. Done — explicitly re-checked and confirmed already
    resolved (`DEV-API-003`), not re-flagged as open drift.
  - The matrix distinguishes missing API (present in real XNA/WP7 but absent here),
    extra API (present here but not in XNA/WP7 and not marked `NOXNA`), and wrong
    signatures. Done — explicit legend + per-finding classification added.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp` (inspected, no changes needed)
  - `include/Microsoft/Devices/Sensors/*.hpp` (inspected, no changes needed)
  - `include/Microsoft/Devices/Sensors/Detail/*.hpp` (inspected, confirmed still
    internal-only, no changes needed)
  - `docs/devices-api-coverage.md` (edited)

### DEV-API-002 — Enforce the `NOXNA` boundary — IN PROGRESS (2026-07-06, one more real bug found and fixed; strict-mode check acceptance criterion still open)

- **Priority:** Critical
- **Area:** API Compatibility
- **Problem:** CNA-specific extensions must not silently become indistinguishable from
  the strict XNA API. 36 `NOXNA` occurrences already exist across 13 headers under
  `include/Microsoft/Devices/` (confirmed by grep) — this task audits whether that
  marking is complete and consistently enforced, not whether `NOXNA` is used at all.
- **Progress so far (2026-07-06):** the first bullet's audit is effectively complete —
  every header in `include/Microsoft/Devices/` has now been read end-to-end this
  session (across `SENSORBASE-007`, `DEV-API-004`, and this task), cross-referenced
  against `docs/devices-api-coverage.md`. Three real, previously-unmarked
  Extra-unmarked bugs were found and fixed across that work: `SensorBase<T>::
  TimeBetweenUpdatesChanged` (`SENSORBASE-007`); `operator==`/`operator!=`/`ToString()`/
  `GetHashCode()` on all five reading structs (`DEV-API-004`); and, found while
  finishing this task's own pass, the identical pattern on
  `AccelerometerReadingEventArgs` (a `class`, not `struct` — its real, unmodified base
  is `System.Object`, not `ValueType`, per its own archived MSDN page `ff707998`, but
  the same "no such member in the real API" conclusion). Fixed identically: tagged
  `NOXNA`, doc comment cites the MSDN page. Also explicitly re-confirmed clean (no gap):
  `VibrateController.hpp` (fully and correctly marked already — cross-checked against
  `docs/devices-api-coverage.md`'s own table), `SensorReadingEventArgs.hpp` (generic;
  its public setter was already independently verified real by `READINGS-002`),
  `CalibrationEventArgs.hpp` (genuinely empty marker class, no extra members),
  `SensorFailedException.hpp`/`AccelerometerFailedException.hpp` (constructor
  signatures unchanged from before, not newly re-verified against MSDN this pass —
  see remaining work below).
- **Remaining work (why this is not marked CLOSED):** the acceptance criteria's third
  bullet — "a test (or documented manual check) fails when an extension is
  accidentally left unmarked" — has no such check yet, compile-time or test-time. No
  regression mechanism currently exists to catch a *future* unmarked extension the way
  this session caught three *existing* ones by manual, one-header-at-a-time reading.
  This is real, un-done work, not yet designed. A plausible future direction: a single
  test that walks `docs/devices-api-coverage.md`'s tables and asserts (via some
  generated or hand-maintained member list) that every entry's real/`NOXNA`
  classification still matches the header — but this needs its own design pass, not a
  quick addition here. `SensorFailedException`/`AccelerometerFailedException`'s exact
  constructor-signature verification against MSDN (mentioned above) is also still open.
- **Required work:**
  - Audit every `NOXNA` declaration in Devices/Sensors headers against `DEV-API-001`'s
    matrix.
  - Verify every non-XNA method/property is consistently marked (the `getStateProperty()`
    drift in Section 1 is the known counter-example to fix first).
  - Add a compile-time or test-time check if the project's build system can express a
    "strict XNA surface" mode; if it cannot yet, document that limitation rather than
    silently skipping this requirement.
- **Acceptance criteria:**
  - No CNA-only method/property is missing a `NOXNA` marker in any header in scope.
  - `NOXNA` extensions are documented separately from strict XNA API (in the matrix from
    `DEV-API-001`).
  - A test (or documented manual check, if no strict-mode build exists yet) fails when
    an extension is accidentally left unmarked.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `include/Microsoft/Devices/Sensors/Compass.hpp`
  - `include/Microsoft/Devices/Sensors/Motion.hpp`
  - `tests/Microsoft/Devices/`
  - `tests/Microsoft/Devices/Sensors/`

### DEV-API-003 — Standardize `State`/`getStateProperty()` exposure — CLOSED, no code change (2026-07-06)

- **Priority:** High
- **Area:** API Compatibility
- **Problem as originally stated:** apparent inconsistency (Section 1 draft):
  `Accelerometer::getStateProperty()` has no `NOXNA` marker; `Gyroscope::getStateProperty()`,
  `Compass::getStateProperty()`, and `Motion::getStateProperty()` all do.
- **Resolution (2026-07-06):** not a bug. This exact question was already investigated
  and answered by an earlier pass, `plan_devices_phase2.md` Task P2-17 (2026-07-02),
  against archived MSDN "previous-versions" pages (cited in `AUDIT.md`'s per-class rows
  and `docs/devices-api-coverage.md`):
  - `Accelerometer.State` is real WP7 API — confirmed against MSDN `ff707930`. Its
    `getStateProperty()` correctly has **no** `NOXNA` marker.
  - `Gyroscope.State` (`hh239201`), `Compass.State` (`hh220912`), and `Motion.State`
    (`hh239189`) do **not** exist on the real classes. Their `getStateProperty()` is a
    CNA-added symmetry extension, correctly marked `NOXNA`.
  - The four classes are therefore already consistent with the authoritative reference —
    "all `NOXNA` or all strict XNA" (this task's original acceptance criterion) was the
    wrong bar; the real API itself is asymmetric across these four sibling classes.
  - `getStateProperty()`'s actual runtime behavior (the `SensorState` values it returns)
    already has test coverage in `AccelerometerTests.cpp`/`GyroscopeTests.cpp`/etc. —
    `NOXNA` itself is a compile-time-only empty marker macro (`CNAHelper.hpp`), so there
    is no additional runtime test to add for the marking policy; it is enforced by
    code/doc review, not `ctest`.
  - No header or source change made. `docs/devices-api-coverage.md` and `AUDIT.md`
    already reflect this; this closing note exists so a future pass doesn't re-open the
    same already-answered question.
- **Suggested files inspected (no changes needed):**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `include/Microsoft/Devices/Sensors/Compass.hpp`
  - `include/Microsoft/Devices/Sensors/Motion.hpp`
  - `include/Microsoft/Devices/Sensors/SensorState.hpp`

### DEV-API-004 — Audit reading struct `ToString`, equality, and hash behavior — CLOSED (2026-07-06, real systemic Extra-unmarked bug found and fixed)

- **Priority:** High
- **Area:** API Compatibility
- **Problem:** Every reading struct (`AccelerometerReading`, `GyroscopeReading`,
  `CompassReading`, `MotionReading`, `AttitudeReading`) implements `ToString()`,
  equality, and hashing; whether this matches expected .NET `ValueType`-style behavior,
  or is CNA convenience behavior that happens to look plausible, has not been verified
  against an authoritative source.
- **What was found:** fetched each reading structure's own archived MSDN reference page
  directly (`AccelerometerReading` `ff403534(v=vs.105)`, `CompassReading`
  `hh203072(v=vs.105)`, `MotionReading` `hh220685(v=vs.105)`, `AttitudeReading`
  `hh220667(v=vs.105)`; `GyroscopeReading` by the identical established pattern for this
  struct family). **All five show the identical result:** `Equals(Object)`,
  `GetHashCode()`, and `ToString()` are each listed as **"(Inherited from
  `ValueType`)"** — none are overridden by the struct itself. `ValueType.ToString()`
  specifically "Returns the fully qualified type name of this instance" (e.g. just the
  literal string `"Microsoft.Devices.Sensors.AccelerometerReading"`, never field
  values). None of the five Methods tables list any equality *operator* at all (`==`/
  `!=` do not exist in C#'s `ValueType.Equals(Object)`-only equality model). **CNA's
  `operator==`/`operator!=`/`ToString()`/`GetHashCode()` on all five reading structs are
  therefore entirely CNA-only conveniences, not real XNA/WP7 API** — and, before this
  task, all four were declared with **no `NOXNA` tag**, and `docs/devices-api-coverage.md`'s
  own "Cross-cutting members" table actively (and incorrectly) asserted these were
  `Real`, with `ToString()`'s note even claiming it "Matches XNA's conventional format."
  This is the exact same **Extra-unmarked** bug pattern `SENSORBASE-007` found for
  `TimeBetweenUpdatesChanged`, this time systemic across all five reading structs at
  once — a real, previously-unflagged finding, not a false alarm.
- **Decision (required work's second bullet): keep the current C++-convenience
  behavior, tag it `NOXNA`** — do not cripple `ToString()` to return just the type
  name (technically closer to the real, undocumented-in-practice `ValueType` default,
  but far less useful, and would break substantial existing test coverage for no
  compatibility benefit real games would ever depend on). A C++ `struct`/`class` has no
  automatic reflection-based `Equals`/`GetHashCode`/`ToString` the way a C# `ValueType`
  does, so providing real ones is a deliberate, justified CNA extension — it just needed
  the `NOXNA` marker it was missing.
- **Fix:** tagged `NOXNA` on `operator==`, `operator!=`, `ToString()`, and
  `GetHashCode()` across all five reading-struct headers
  (`AccelerometerReading.hpp`/`GyroscopeReading.hpp`/`CompassReading.hpp`/
  `MotionReading.hpp`/`AttitudeReading.hpp`), each doc comment rewritten to cite the
  specific archived MSDN page and explain the CNA-extension rationale (cross-referencing
  `AccelerometerReading`'s doc comment as the canonical explanation, to avoid repeating
  the full rationale five times). Corrected `docs/devices-api-coverage.md`'s
  "Cross-cutting members — reading structs" table (3 rows) and its "DEV-API-001
  verification result" section's now-inaccurate "zero Extra-unmarked" claim.
- **Test coverage:** already comprehensive for all five structs
  (`EqualityOperatorEqualInstances`/`EqualityOperatorUnequal<Field>`/`ToStringFormat`/
  `GetHashCodeConsistency`/`GetHashCodeDifferentForUnequalInstances`, confirmed by grep
  across all five `*ReadingTests.cpp` files) — no new tests needed, this was a
  marker/doc-only fix.
- **Verified:** 313/313 tests (unchanged) on plain `cmake-build-debug` and both ASan/
  UBSan sanitizer presets (0 issues each; TSan not re-run — no concurrency-relevant code
  touched, pure header annotation change).
- **Required work:**
  - Verify expected behavior for all reading structs and event-args classes.
  - Decide, per struct, whether CNA should mimic .NET `ValueType.ToString()` conventions
    exactly or keep the current C++-convenience format behind a documented `NOXNA`
    rationale.
  - Add or extend tests for the decided behavior.
- **Acceptance criteria:**
  - All reading structs have documented, decided compatibility behavior (not just
    "whatever the code currently does").
  - Tests cover `ToString()`, equality (`==`/`!=`), and hash consistency for every
    reading struct that implements them.
  - Any non-XNA convenience behavior is explicitly labelled as an extension in docs.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/AccelerometerReading.hpp`
  - `include/Microsoft/Devices/Sensors/GyroscopeReading.hpp`
  - `include/Microsoft/Devices/Sensors/CompassReading.hpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `src/Microsoft/Devices/Sensors/*Reading.cpp`
  - `tests/Microsoft/Devices/Sensors/*ReadingTests.cpp`

### DEV-API-005 — Audit exception types and messages — CLOSED (2026-07-06, verified correct as-is against archived MSDN citations, no code change)

- **Priority:** High
- **Area:** API Compatibility
- **Problem:** Confirmed (Section 1): `Accelerometer` throws its own
  `AccelerometerFailedException`; `Gyroscope`, `Compass`, and `Motion` all throw the
  generic `SensorFailedException`. Whether this split matches real XNA/WP7 exception
  types (which may not have a generic "sensor failed" exception at all) is unverified.
- **What was found — the exception-type split is exactly correct, now with direct
  citations (`docs/devices-api-coverage.md` previously asserted this with no citation
  shown):**
  - Fetched `Gyroscope`/`Compass`/`Motion`'s own archived MSDN class pages
    (`hh239201(v=vs.110)`, `hh220912(v=vs.105)`, `hh239189(v=vs.105)`): all three list
    `Start`/`Stop` as **"(Inherited from `SensorBase<TSensorReading>`)"** — none of them
    override `Start()`/`Stop()` in the real API at all. `SensorBase(TSensorReading).Start()`'s
    own page (`hh220889(v=vs.105)`) documents its Exceptions table as
    `UnauthorizedAccessException`/`InvalidOperationException`/`OutOfMemoryException`/
    `ObjectDisposedException`/`SensorFailedException` ("Data acquisition from the sensor
    cannot be started. The cause of the error is described in the exception's message
    field.") — confirming `SensorFailedException` genuinely is the real, base-class,
    shared failure type these three sensors throw, not a CNA invention.
  - Fetched `Accelerometer.Stop()`'s own dedicated page (`ff707301(v=vs.105)`, distinct
    from the base page — confirming `Accelerometer` *does* override `Stop()` in the real
    API): Exceptions table lists `UnauthorizedAccessException`/`AccelerometerFailedException`
    specifically — confirming `AccelerometerFailedException` is real, `Accelerometer`-only
    API, exactly matching CNA's existing choice.
  - **Unsupported sensor:** `InvalidOperationException` — already verified by
    `SENSORBASE-005` against `CurrentValue`'s own page (`hh239261`).
  - **Disposed sensor:** `ObjectDisposedException` — documented directly on
    `Start()`'s own Exceptions table above; CNA throws it symmetrically from `Stop()`
    too (not separately documented on the real `Stop()` page, but not contradicted by
    it either — a reasonable, undocumented-either-way extension, consistent with the
    conventional .NET `IDisposable` pattern).
  - **Double `Start()`:** matches the same `SensorFailedException`/`AccelerometerFailedException`
    "cannot be started, cause in message" contract above — CNA's actual messages
    ("...already started") are intentionally CNA wording (the real API's own message
    text is not specified beyond "described in the exception's message field").
  - **Double `Stop()`:** `SensorBase(TSensorReading).Stop()`'s own base page
    (`hh220748(v=vs.110)`) has **no Exceptions/Remarks section at all** — no documented
    exception for calling `Stop()` when not started. CNA's choice (safe no-op) is
    unverified-but-not-contradicted, same tier as the disposed-`Stop()` case above.
  - **Invalid `TimeBetweenUpdates`:** already resolved by `SENSORBASE-008` (verified
    correct as-is — the real setter has no documented range restriction either).
  - **Test coverage:** already comprehensive and already asserts the exact correct type
    per class at every throw site (`AccelerometerFailedException` throughout
    `AccelerometerTests.cpp`'s `Start()`-failure paths; `SensorFailedException`
    throughout `GyroscopeTests.cpp`/`CompassTests.cpp`/`MotionTests.cpp`) — confirmed by
    grep, no new tests needed.
- **Decision (required work's second bullet): keep `SensorFailedException` as the
  shared type for `Gyroscope`/`Compass`/`Motion`** — this is not a CNA-invented
  stand-in needing a `NOXNA` tag, it is the real, documented, base-class exception type
  those three classes genuinely throw in the real API (confirmed above), so no
  dedicated `GyroscopeFailedException`/`CompassFailedException`/`MotionFailedException`
  should ever be added — doing so would be a *deviation* from the real API, not a fix.
- **Doc updates:** `docs/devices-api-coverage.md`'s `Exceptions / Enums` table entry for
  `AccelerometerFailedException` upgraded from an uncited assertion to citing all of the
  above archived MSDN pages directly.
- **Verified:** no code or test changes — this was a pure documentation/citation task,
  confirming already-correct, already-tested behavior. Existing 313/313 test result
  unaffected.
- **Required work:**
  - Verify expected exception types/messages for: unsupported sensor, disposed sensor,
    double `Start()`, double `Stop()`, failed `Start()`, invalid `TimeBetweenUpdates`.
  - Decide, with a documented rationale, whether `SensorFailedException` should be kept
    as a CNA-wide stand-in (and marked `NOXNA` if so) or whether `Gyroscope`/`Compass`/
    `Motion` should gain their own dedicated exception types matching
    `AccelerometerFailedException`'s pattern.
  - Add tests for every public failure path per class.
- **Acceptance criteria:**
  - Exception-type policy is documented in `DEV-API-001`'s matrix.
  - Tests cover unsupported sensor, disposed sensor, double start, double stop, and
    invalid parameters for all four sensor classes.
  - Messages are either verified-compatible or explicitly documented as intentionally
    non-exact CNA wording.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp`
  - `include/Microsoft/Devices/Sensors/SensorFailedException.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerFailedExceptionTests.cpp`
  - `tests/Microsoft/Devices/Sensors/SensorFailedExceptionTests.cpp`

---

## 4. `VibrateController` tasks

**Reminder:** the XNA 4.0 class name is `VibrateController`. Any occurrence of
"VibrationController" found while doing this work is a naming error to fix (`VIB-001`),
not an alternate spelling to preserve.

### VIB-001 — Correct terminology everywhere — CLOSED (2026-07-06, verified clean, no occurrences found)

- **Priority:** Critical
- **Area:** Vibration API
- **Problem:** The XNA class is `VibrateController`, not `VibrationController`. The
  public class name in this repository is already correct
  (`include/Microsoft/Devices/VibrateController.hpp`), but docs, comments, tests,
  examples, and any future plan text must be audited to make sure the wrong name never
  creeps in.
- **Resolution (2026-07-06):** ran a repository-wide, case-insensitive grep for
  `vibrationcontroller`/`vibration controller` across every file (excluding vendored
  `third_party/`/`vendor/` and `.git/`). Every hit (9 total, in `plan_devices.md` and
  `NEXT.md` only) is an explicit warning against the mistake ("not `VibrationController`",
  "never call it `VibrationController`"), not an actual misuse — confirmed by reading
  each hit's surrounding line. No occurrence anywhere in `include/`, `src/`, `tests/`,
  `examples/`, or `docs/` at all. No fix needed; this task's job was to verify, and the
  verification is now recorded.
- **Required work:**
  - Grep the whole repository (docs, comments, tests, examples, this plan file's own
    future edits) for "VibrationController" and fix any occurrence found, unless it is
    explicitly quoting/explaining the common mistake. Done — zero real occurrences.
  - Ensure the public class stays exactly `VibrateController` through every task in this
    section. Done, re-confirmed for every VIB task closed alongside this one.
- **Acceptance criteria:**
  - Public API uses `VibrateController` everywhere. Confirmed.
  - No documentation, comment, test name, or example accidentally says
    "VibrationController" as if it were the real name. Confirmed.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp` (inspected, no change needed)
  - `src/Microsoft/Devices/VibrateController.cpp` (inspected, no change needed)
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp` (inspected, no change needed)
  - `examples/demo_devices/` (inspected, no change needed)
  - `docs/devices-*.md` (inspected, no change needed)
  - `plan_devices.md` (this entry only)

### VIB-002 — Split XNA phone vibration from SDL haptics

- **Priority:** Critical
- **Area:** Vibration Backend
- **Problem:** Confirmed (Section 1): today there is exactly one backend, SDL3's
  `SDL_Haptic`, used for both the strict XNA `Start(TimeSpan)` and every `NOXNA`
  extension. A generic SDL haptic device (e.g. an arbitrary desktop force-feedback
  wheel) is not the same concept as "the Windows Phone's vibration motor," and treating
  them identically is a compatibility risk, not just a naming one.
- **Required work:**
  - Introduce a backend abstraction (e.g. `Detail::IVibrateBackend`) that
    `VibrateController` calls through, instead of calling `SDL_Haptic` functions
    directly.
  - Separate "the phone/system vibration motor" backend concept from "any SDL haptic
    device" — the desktop SDL-haptic path should be an explicit, documented fallback or
    `NOXNA`-flavored behavior, not silently presented as equivalent to strict XNA phone
    vibration.
  - Make the backend choice injectable for tests (see `VIB-009`).
- **Acceptance criteria:**
  - Strict XNA `Start(TimeSpan)` behavior does not rumble an arbitrary desktop haptic
    device (e.g. a random USB force-feedback gadget) as if it were "the phone vibrating,"
    unless that mapping is explicitly documented as the deliberate desktop behavior.
  - Backend choice is documented and independently testable.
  - Unit tests can inject a fake `IVibrateBackend`.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `src/Microsoft/Devices/Detail/` (new — no `Detail/` directory exists yet under
    `Microsoft::Devices`, unlike `Microsoft::Devices::Sensors::Detail`)
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-003 — Implement Android phone vibrator backend

- **Priority:** Critical
- **Area:** Android Backend
- **Problem:** Confirmed (Section 1): there is no dedicated Android vibrator backend
  today — the code relies entirely on SDL3's own bundled Android haptic backend
  reaching `Context.VIBRATOR_SERVICE`. This was a previously-made, deliberate decision
  (per an existing comment in `VibrateController.cpp`) to not build a redundant native
  bridge; this task's job is to re-verify that decision still holds, not to assume it
  is wrong.
- **Required work:**
  - Re-verify, by reading SDL3's actual Android haptic backend source
    (`third_party/SDL`), that it truly reaches `Vibrator`/`VibratorManager` with
    adequate amplitude control for this project's needs.
  - If the existing SDL3-only approach is confirmed sufficient, document that
    re-verification explicitly (do not silently re-assert the old conclusion without
    having checked it again).
  - If gaps are found (e.g. missing `VibrationEffect`/`VibratorManager` support for
    modern Android versions, or missing legacy fallback), implement or plan a
    dedicated Android backend behind the `IVibrateBackend` abstraction from `VIB-002`.
  - Ensure Android manifest permissions are documented (`VIBRATE` permission) whether or
    not a new native path is added.
- **Acceptance criteria:**
  - The Android demo (`examples/demo_devices/android/`) can vibrate a physical phone's
    motor.
  - Backend handles devices without a vibrator gracefully (no crash, `IsSupported`-style
    check returns false).
  - Unit tests cover backend selection using fake platform hooks, not a physical device.
  - A manual test checklist entry records Android OS/API version and device model used.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `src/Microsoft/Devices/Detail/` (new, from `VIB-002`)
  - `third_party/SDL/src/haptic/` (read-only research — vendored, do not edit)
  - `examples/demo_devices/android/`
  - `docs/devices-android.md`

### VIB-004 — Add iOS vibration backend plan or implementation

- **Priority:** High
- **Area:** iOS Backend
- **Problem:** There is no iOS toolchain available in this development environment
  (confirmed repeatedly in this repository's own history), and no explicit iOS-native
  vibration/haptics path exists in the code.
- **Required work:**
  - Decide whether CNA should support iOS vibration in this API at all, and document
    the decision with a rationale.
  - If yes: plan (or implement, if an Apple toolchain ever becomes available) using
    `UIImpactFeedbackGenerator`/`CHHapticEngine` as appropriate.
  - If no: document the unsupported behavior clearly (silent no-op, matching the
    "no hardware" desktop case), so callers get deterministic behavior either way.
- **Acceptance criteria:**
  - iOS behavior is deterministic and documented, whichever choice is made.
  - If a backend is added, it compiles behind the appropriate platform guard even
    without an Apple toolchain available to actually link it here.
  - Unsupported devices/platforms do not crash.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Detail/` (new, from `VIB-002`)
  - iOS build/toolchain files (none currently present — confirm before assuming a
    location)
  - `docs/devices-*.md`

### VIB-005 — Fix `IsSupported` semantics

- **Priority:** High
- **Area:** Vibration API
- **Problem:** `getIsSupportedProperty()` is a `NOXNA` extension. Confirmed current
  behavior (`VibrateController.cpp`): it opens (or reuses) a haptic device and reports
  supported if a device could be opened, without confirming
  `SDL_InitHapticRumble()`-style actual rumble capability — this exact tradeoff was
  already reviewed once in this codebase's history and left unchanged because
  `SDL_InitHapticRumble()` has a real, unverifiable-without-hardware side effect
  (uploads an effect onto the physical device). This task must re-examine that decision
  with fresh eyes, not just re-assert it.
- **Required work:**
  - Re-decide exact `NOXNA` semantics for `getIsSupportedProperty()` given the backend
    split introduced in `VIB-002`/`VIB-003` (a phone vibrator backend may have different,
    simpler support semantics than a generic SDL haptic device).
  - Ensure probing checks genuinely usable vibration capability for whichever backend is
    selected.
  - Avoid side effects such as leaving devices open or changing global haptic/backend
    state as a side effect of probing.
- **Acceptance criteria:**
  - `getIsSupportedProperty()` returns false when no usable vibration backend exists,
    for every backend in play after `VIB-002`/`VIB-003`.
  - Tests cover supported, unsupported, and backend-failure paths via a fake backend.
  - Any remaining known-imprecise case (e.g. "device opens but rumble capability itself
    is unconfirmed") is explicitly documented, not silently accepted.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-006 — Validate duration compatibility

- **Priority:** High
- **Area:** Vibration API
- **Problem:** `Start(TimeSpan)`'s duration boundaries must match XNA/Windows Phone
  behavior (this codebase currently validates `TimeSpan.Zero` to
  `TimeSpan.FromSeconds(5)` — confirm this range and the exact rejection behavior
  against an authoritative reference rather than assuming the current implementation is
  correct just because it exists).
- **Required work:**
  - Verify minimum and maximum duration behavior against XNA/WP7 documentation.
  - Add boundary tests for zero, negative, exactly 5 seconds, above 5 seconds, very
    large `TimeSpan` values, and repeated `Start()` calls.
  - Confirm whether `Start(TimeSpan.Zero)` should stop, no-op, or start a
    zero-duration vibration, and make the implementation match that decision exactly.
- **Acceptance criteria:**
  - Duration validation behavior is documented with its rationale.
  - Tests cover every boundary case listed above.
  - Behavior is consistent across every backend introduced by `VIB-002`/`VIB-003`.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-007 — Define repeated `Start`/`Stop` behavior

- **Priority:** High
- **Area:** Vibration API
- **Problem:** Repeated vibration calls must have fully deterministic, tested behavior
  across backend changes from `VIB-002`.
- **Required work:**
  - Verify behavior for `Start()` while already vibrating (does it restart the timer,
    ignore the new call, or something else?).
  - Verify `Stop()` before any `Start()`, repeated `Stop()`, and `Stop()` after a backend
    failure.
  - Add fake-backend tests for all of the above.
- **Acceptance criteria:**
  - Repeated calls behave consistently and are documented.
  - Tests do not require real hardware.
  - No backend resource leaks occur across repeated Start/Stop cycles (verify under
    `devices-asan`).
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-008 — Make left/right motor support explicitly `NOXNA`

- **Priority:** Medium
- **Area:** `NOXNA` Extension
- **Problem:** `StartLeftRight(float, float, TimeSpan)` is already marked `NOXNA` in the
  header — this task is to keep it that way through the `VIB-002`/`VIB-003` backend
  refactor and make sure its documentation is unambiguous, not to introduce the marker
  for the first time.
- **Required work:**
  - Keep `StartLeftRight` behind `NOXNA` through any backend changes.
  - Document that it is a CNA extension for dual-motor/gamepad-like haptic hardware, not
    XNA `VibrateController` API.
  - Ensure any future strict-XNA-surface check (from `DEV-API-002`) rejects it if that
    mechanism is built.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `StartLeftRight` as `NOXNA`.
  - Docs clearly state it is not XNA 4.0 API.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/VibrateController.hpp`
  - `src/Microsoft/Devices/VibrateController.cpp`
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`

### VIB-009 — Add fake vibration backend tests

- **Priority:** Critical
- **Area:** Tests
- **Problem:** Current `VibrateControllerTests.cpp` exercises the real SDL3 haptic path
  directly; after `VIB-002` introduces a backend abstraction, tests should be able to
  inject a fake backend instead of depending on whatever haptic hardware (or lack of it)
  happens to be present in the test environment.
- **Required work:**
  - Add fake-backend injection (mirroring the `SetBackendForTesting()`-style pattern
    already used by `Compass`/`Motion` for their Android backends).
  - Test duration forwarding, stop forwarding, supported/unsupported probing, backend
    errors, and resource cleanup — all via the fake.
  - Ensure these tests run in CI (`DEV-BUILD-003`) without any hardware.
- **Acceptance criteria:**
  - All core `VibrateController` tests pass without real hardware present.
  - Hardware-dependent tests (if any remain) are separate and explicitly opt-in.
- **Suggested files to inspect or edit:**
  - `tests/Microsoft/Devices/VibrateControllerTests.cpp`
  - `src/Microsoft/Devices/Detail/` (new, from `VIB-002`)

### VIB-010 — Add manual hardware vibration checklist

- **Priority:** Medium
- **Area:** QA
- **Problem:** Phone vibration cannot be fully validated by unit tests alone; there is
  currently no dedicated manual checklist scoped specifically to vibration (the existing
  `docs/devices-hardware-checklist.md` covers sensors broadly and should be extended,
  not duplicated).
- **Required work:**
  - Add a manual checklist section covering: Android phone, iOS phone (if `VIB-004`
    adds support), desktop without haptics, desktop with a connected haptic device, and
    gamepad-connected desktop (confirming `VibrateController` and
    `GamePad::SetVibration()` do not fight over the same motor).
  - Record expected behavior for both strict XNA and `NOXNA` modes.
- **Acceptance criteria:**
  - `docs/devices-hardware-checklist.md` (extended, not duplicated) contains a
    vibration-specific validation matrix.
  - Each manual test row has device, OS, backend, action, expected result, and observed
    result columns.
- **Suggested files to inspect or edit:**
  - `docs/devices-hardware-checklist.md`
  - `examples/demo_devices/`
  - `plan_devices.md`

---

## 5. `SensorBase<T>` tasks

### SENSORBASE-001 — Implement real `TimeBetweenUpdates` semantics — CLOSED (2026-07-06)

- **Priority:** Critical
- **Area:** `SensorBase<T>`
- **Problem:** Confirmed (Section 1): `TimeBetweenUpdates` is public XNA API, stored and
  change-notified generically in `SensorBase<T>`, but the SDL backends
  (`Accelerometer`, `Gyroscope`) never read it back at all — zero references in either
  `.cpp` file. The Android backends only apply it once, at `Start()` time.
- **Resolution (2026-07-06) — all four sensor classes now honor
  `TimeBetweenUpdates`, see `ACCEL-005`/`GYRO-004`/`SDL-SENSOR-002` (SDL-backed) and
  `ANDROID-BRIDGE-002` (Android-backed) for full detail:**
  - Added `SensorBase<T>::ShouldAcceptUpdateAt(now)`/`ResetUpdateThrottle()` — a
    per-instance, mutex-guarded throttle decision. `now` is passed in by the caller
    (real wall-clock time in production, synthetic `DateTimeOffset` values in tests) so
    the decision is a pure function of its inputs, unit-tested with zero real-time
    sleeps.
  - Wired into `Accelerometer`/`Gyroscope`'s `ProcessSensorUpdateEvent()` (the real SDL
    event path only) and reset from each class's `Start()`, so a fresh `Start()` always
    delivers an immediate first sample.
  - `getTimeBetweenUpdatesProperty()` is read fresh on every incoming event, so a change
    while the sensor is running takes effect on the very next event, no
    `Stop()`/`Start()` needed.
  - Deliberately **not** applied to the `NOXNA` synthetic-injection test hooks
    (`InjectSyntheticSensorUpdate()`, `DispatchToInstancesForTesting()`) on either
    class — those exist specifically so tests can exercise dispatch behavior without
    depending on real elapsed time; throttling them would have broken the existing
    283-test baseline's assumption that every injected update dispatches immediately.
  - **`Compass`/`Motion`'s Android backend (`ANDROID-BRIDGE-002`, 2026-07-06):**
    `Detail::AndroidSensorBridge::SetSampleInterval()` re-applies
    `ASensorEventQueue_setEventRate()` on the live queue; `Compass`/`Motion` forward
    their own `TimeBetweenUpdatesChanged` event to the active backend.
  - **Minimum/maximum value validation (`SENSORBASE-008`, closed 2026-07-06):**
    confirmed via archived MSDN pages (`hh220884`/`hh239315`) that the real WP7
    `TimeBetweenUpdates` setter has no documented validation contract at all — CNA's
    unvalidated `setTimeBetweenUpdatesProperty()` already matches it exactly. Not a gap;
    verified correct as-is, no code change needed.
  - **Addendum (2026-07-06, same-day stabilization pass):** `ShouldAcceptUpdateAt()`'s
    throttle *decision* was changed from `System::DateTimeOffset` wall-clock time to
    `std::chrono::steady_clock` — a wall-clock-based interval measurement can go
    backward or jump on an NTP step/clock change, which would wedge the throttle open
    (rejecting every update) or defeat it entirely for one event; `steady_clock` is
    standard-guaranteed never to be adjusted. Production call sites
    (`Accelerometer`/`Gyroscope::ProcessSensorUpdateEvent()`) now pass
    `std::chrono::steady_clock::now()`; `SensorBaseTests.cpp`'s throttle tests use
    synthetic `steady_clock::time_point` values instead of `DateTimeOffset` ones. Sensor
    *reading* timestamps (`AccelerometerReading::Timestamp` etc.) are unchanged —
    still wall-clock `DateTimeOffset`, matching the real WP7 API. Also added
    `ShouldAcceptUpdateAtWithNegativeTimeBetweenUpdatesNeverThrottles` (proves the
    negative-interval fallback is safe, feeding `SENSORBASE-008`). Verified: 293 tests
    (up from 292) on plain `cmake-build-debug` and all three sanitizer presets, 0 new
    findings, 40-iteration `AccelerometerTests.*:GyroscopeTests.*` loop clean.
- **Required work:**
  - Define exact minimum, maximum, and default behavior for `TimeBetweenUpdates` (the
    current default, `TimeSpan.FromMilliseconds(2.0)`, is commented as matching ".NET
    source" — verify that comment against an authoritative reference rather than
    trusting it at face value). Done, via `SENSORBASE-008` — no validation contract
    exists in the real API to define; the "no validation" behavior itself is now
    citation-backed rather than assumed.
  - Apply the value to every backend (SDL done 2026-07-06; Android done 2026-07-06,
    `ANDROID-BRIDGE-002`).
  - Support changing the value while the sensor is already running, for every backend
    (SDL done; Android done, `ANDROID-BRIDGE-002`).
  - Add tests proving the callback rate is actually throttled, or the backend's own
    sample rate is actually updated — not just that the property getter/setter
    round-trips. Done — `SensorBaseTests.cpp`'s 7 new `ShouldAcceptUpdateAt`/
    `ResetUpdateThrottle` tests (SDL) plus `CompassTests.cpp`/`MotionTests.cpp`'s new
    `SetTimeBetweenUpdatesPropertyForwardsToBackend` tests (Android, via the fake
    backends — the real NDK path itself was only confirmed to compile, not
    runtime-verified; see `ANDROID-BRIDGE-002`'s closing note).
- **Acceptance criteria:**
  - `Accelerometer`, `Gyroscope`, `Compass`, and `Motion` all honor
    `TimeBetweenUpdates` in their actual event delivery rate. Done for all four (SDL
    software-throttled; Android real hardware-rate-adjusted, host-verified only, see
    `ANDROID-BRIDGE-002`).
  - Setting `TimeBetweenUpdates` while a sensor is started changes behavior without
    requiring `Stop()`/`Start()` or object recreation. Done for all four.
  - Tests cover invalid values (negative, zero if disallowed) and valid updates, for
    every sensor class. Done — zero (`ShouldAcceptUpdateAtWithZeroTimeBetweenUpdatesAlwaysAccepts`)
    and negative (`ShouldAcceptUpdateAtWithNegativeTimeBetweenUpdatesNeverThrottles`,
    `SetTimeBetweenUpdatesPropertyAcceptsNegativeValueWithoutThrowing`) are both covered
    as of `SENSORBASE-008`, which also confirmed "disallowed" doesn't apply — the real
    API disallows nothing.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp` (edited)
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp` (edited)
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp` (edited)
  - `src/Microsoft/Devices/Sensors/Compass.cpp` (edited, `ANDROID-BRIDGE-002`)
  - `src/Microsoft/Devices/Sensors/Motion.cpp` (edited, `ANDROID-BRIDGE-002`)
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp`

### SENSORBASE-002 — Verify default `TimeBetweenUpdates` — CLOSED (2026-07-06, Medium-confidence citation, no code change)

- **Priority:** High
- **Area:** `SensorBase<T>`
- **Problem:** The current default (`TimeSpan.FromMilliseconds(2.0)`, shared by all four
  sensor classes via `SensorBase<T>`'s constructor) may not match per-sensor-type XNA/WP7
  defaults — a single shared default is a simplifying assumption that has not been
  checked against a per-class authoritative default.
- **Resolution (2026-07-06):** the archived MSDN property/class pages for
  `SensorBase(TSensorReading).TimeBetweenUpdates` (already fetched for `SENSORBASE-008`,
  MSDN `hh220884`/`hh239315`) document no default value at all in their Remarks
  sections — the real API's default isn't stated in the reference docs directly.
  Cross-checked instead against MonoGame's own reimplementation (Medium confidence, same
  citation tier this project already uses for `SensorState`'s enum values) — MonoGame's
  `SensorBase()` constructor (`MonoGame.Framework/Devices/Sensors/SensorBase.cs`,
  `develop` branch) sets `this.TimeBetweenUpdates = TimeSpan.FromMilliseconds(2)` at
  exactly the shared base-class level, architecturally identical to CNA's own choice —
  a single default for all four sensor types, not a per-subclass override (the real API
  has no per-subclass constructor override point for this property at all, since
  `TimeBetweenUpdates` lives solely on the shared `SensorBase<T>` base). **Conclusion:
  CNA's existing single 2ms shared default is correct and requires no code change** —
  a single common default is not just acceptable but the only architecturally possible
  choice, matching the real API's own class hierarchy. Added one test per concrete
  sensor class (`AccelerometerTests`/`GyroscopeTests`/`CompassTests`/`MotionTests`
  `.DefaultTimeBetweenUpdatesIsTwoMilliseconds`) asserting this at the concrete-class
  level, not just the generic `SensorBase<T>` level `SensorBaseTests.cpp` already
  covered. Verified: 300/300 tests (up from 296) on plain `cmake-build-debug` and
  `devices-ubsan` (3 pre-existing UBSan findings unchanged, none in
  `Microsoft::Devices`).
- **Required work:**
  - Verify the expected default for each of the four sensor types individually. Done —
    no per-type default exists or is architecturally possible in the real API; one
    shared default at the base-class level is correct.
  - Decide whether one common default is acceptable, or whether per-class defaults are
    required for compatibility. Done — one common default, confirmed correct.
  - Add tests asserting whatever default is decided, per class. Done — 4 new tests.
- **Acceptance criteria:**
  - Default values are documented per sensor class, with rationale. Done — this closing
    note plus the 4 new tests' doc comments.
  - Tests assert the chosen defaults for each of the four classes. Done.
  - Backend startup actually uses those defaults (ties into `SENSORBASE-001`). Already
    true — `SensorBase()`'s constructor sets the default unconditionally for every
    derived class, confirmed by `SENSORBASE-001`'s own closing work.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp` (inspected, no change needed)
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### SENSORBASE-003 — Fix event reentrancy and self-destruction safety — CLOSED (2026-07-06, gap found and closed for Compass/Motion; one deeper risk documented, unverified)

- **Priority:** Critical
- **Area:** Lifecycle / Events
- **Problem:** Event handlers may call `Stop()`, `Dispose()`, or otherwise trigger
  destruction of the sensor object while a callback dispatch is still executing on some
  thread. This exact class of bug has been found and fixed multiple times in this
  codebase's history for `Detail::AndroidSensorBridge`; this task is to re-audit the
  *current* state (after those fixes) for `SensorBase<T>`'s own dispatch path and the
  SDL subsystem, not to assume prior fixes fully closed every angle.
- **Resolution (2026-07-06):** re-audited by grepping every sensor test file for
  existing self-destruction/reentrancy coverage first, rather than re-reading already
  extensively-hardened code from scratch. Found a real, concrete gap:
  `Accelerometer`/`Gyroscope` each have multiple such tests (`DisposeFromWithinOwnCallbackDoesNotDeadlock`,
  `SelfDestroyingFromOwnCallbackDuring...`, `DisposingDifferentInstanceDuringSameBatchDispatchDoesNotUseAfterFree`,
  from Tasks P5-3/P7-3/P8-1) — **`CompassTests.cpp`/`MotionTests.cpp` had zero**, even
  though `Compass`/`Motion` share the exact same `SensorBase<T>`-level
  `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` reentrancy machinery. This was
  never tested for these two classes at all, confirmed by grep before assuming either
  way.
  - Added `CompassTests.DisposeFromWithinOwnCallbackDoesNotDeadlock` and
    `MotionTests.DisposeFromWithinOwnCallbackDoesNotDeadlock` (via the existing fake-backend
    seam, `FakeCompassBackend`/`FakeMotionBackend`'s `CapturedOnReading`): a
    `CurrentValueChanged` handler calls `Dispose()` on its own sender from within the
    callback the sender itself triggered. Both pass cleanly (no deadlock, no throw,
    correct "already disposed" behavior on a subsequent external `Dispose()` call) —
    confirms `Compass`/`Motion`'s own `ClaimDisposalOnce()`/`Stop()` reentrancy handling
    is safe at the class level, same conclusion as the existing Accelerometer/Gyroscope
    tests.
  - **One deeper risk found by code-reading, explicitly NOT verified, matching this
    task's own acceptance criterion to document rather than silently assume safe:**
    `Compass`/`Motion` each own their real Android backend via
    `std::unique_ptr<ICompassBackend>`/`IMotionBackend` (`backend_`). If a game's
    `CurrentValueChanged` handler *destroys* (not just `Dispose()`s) the owning
    `Compass`/`Motion` instance from within its own callback, `backend_`'s destructor
    runs — tearing down the real `Detail::AndroidCompassBackend`/`AndroidMotionBackend`
    and its `AndroidSensorBridge`(s) — **while that same backend's own member function
    (`PublishReading()`, called from `HandleRotationVectorSample()`/etc.) is still on
    the call stack**, having called back into the very handler that triggered the
    teardown. This is architecturally the same class of bug Accelerometer's Task P8-1
    fixed (a callback destroying its own dispatcher mid-dispatch) — but `Compass`/
    `Motion`'s real backend is Android-only (`#if defined(__ANDROID__)`) and cannot be
    exercised in this container even with a sanitizer; the fake-backend tests above
    cannot reach this code path at all (the fake has no `PublishReading()`-equivalent
    call-stack structure). **Not fixed, not proven safe or unsafe — explicitly
    documented as an open, hardware-verification-only risk**, per this task's own
    acceptance criterion ("any remaining unsupported case is explicitly documented"),
    rather than silently left unmentioned the way it was before this pass (the existing
    Accelerometer/Gyroscope "destroying from within your own callback" boundary was
    already documented; this equivalent risk for Compass/Motion was not, until now).
  - Verified: 302/302 tests (up from 300) on plain `cmake-build-debug` and all three
    sanitizer presets (0 ASan; TSan 44 reports/UBSan 3 reports, both entirely the same
    pre-existing findings as before, none new).
- **Required work:**
  - Audit all sensor dispatch methods (`SensorBase<T>`'s own `CurrentValueChanged`
    raising, `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()`, and each
    Android backend's callback path). Done — `SdlSensorSubsystem`/`Accelerometer`/
    `Gyroscope` already extensively audited and hardened in prior phases (re-confirmed,
    not re-litigated); `Compass`/`Motion`'s own class-level logic newly audited and
    tested this pass; the two classes' real Android backend call-stack risk identified
    but not fixable/testable here.
  - Confirm `this`/captured pointers are not touched after raising user callbacks unless
    lifetime is provably still valid (shared ownership, or an established documented
    boundary). Done for `SensorBase<T>`'s own `setCurrentValueProperty()` and
    `Compass`/`Motion`'s own `onReading`/`onCalibrationNeeded` lambdas (neither touches
    `this` after the event/callback returns). Not verified for the real Android
    backend's own call stack (see above).
  - Add tests where event handlers call `Stop()`, `Dispose()`, and destroy the sensor
    object from inside `CurrentValueChanged`/`ReadingChanged`/`Calibrate`. Done for
    `Dispose()` on `Compass`/`Motion` (new tests above); full C++ `delete`-style
    destruction was already tested for `Accelerometer`/`Gyroscope` in prior phases and
    is architecturally not reachable the same way for `Compass`/`Motion` via the fake
    backend (no shared multi-instance batch dispatch to reproduce).
- **Acceptance criteria:**
  - No use-after-free occurs when event handlers stop/dispose sensors, for every
    documented-supported case. Done, for every class, at the level each class's own
    architecture makes testable in this container.
  - `devices-asan`/`devices-tsan` runs are clean for these specific tests. Done.
  - Any remaining unsupported case (e.g. destroying the owning object from within its
    own callback, on its own worker thread) is explicitly documented, matching this
    codebase's existing "accepted boundary" pattern rather than silently ignored. Done
    — see the `Compass`/`Motion` real-backend risk documented above.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp` (inspected, no change needed)
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` (inspected, no
    change needed — already hardened in prior phases)
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp` (edited)
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`

### SENSORBASE-004 — Clarify thread-safety contract — CLOSED (2026-07-06, real race found in Compass/Motion and fixed, contract written)

- **Priority:** High
- **Area:** Lifecycle / API
- **Problem:** Real .NET instance members are generally not guaranteed thread-safe by
  the framework itself, but this codebase's `SensorBase<T>` and the SDL/Android backends
  use mutexes in several places — the exact boundary of what is and is not promised
  thread-safe has not been written down as a single explicit contract.
- **What was found:** auditing all four sensor classes' locking found that
  `Compass`/`Motion`'s `Start()`/`Stop()`/`getStateProperty()` had **no locking at all**
  on `state_`/`started_` — unlike `Accelerometer`/`Gyroscope`, whose equivalent fields
  are guarded by their shared `Detail::SdlSensorSubsystem<TSensor>::mutex_`. This was
  confirmed as a real, reproducible data race (not just a theoretical audit finding) by
  writing `CompassTests.ConcurrentStartStopFromMultipleThreadsDoesNotCrash` (mirroring
  the existing `AccelerometerTests`/`GyroscopeTests` equivalent) and running it under
  `devices-tsan`, which reported a race between `Start()`/`Stop()`'s writes and
  `getStateProperty()`'s read at `Compass.cpp`. Added the identical test for `Motion`
  (structurally identical class) to confirm the same race there too.
- **Fix:** added a per-instance `mutable std::mutex mutex_` to both `Compass` and
  `Motion`, locked for the entire body of `Start()`/`Stop()`/`getStateProperty()`/
  `SetBackendForTesting()` (safe to hold across the `backend_->Start()`/`Stop()` call
  itself, since neither Android backend ever synchronously re-enters the sensor object).
  `Dispose(bool)` reads `started_` under a short-lived separate lock scope, then calls
  `Stop()` outside that scope (since `Stop()` acquires the same non-recursive mutex) —
  mirroring the exact pattern `Accelerometer::Dispose(bool)` already used.
- **Contract written:** `docs/devices-thread-safety.md` — states the real WP7 API's own
  documented baseline (fetched from the archived MSDN `Compass` class page,
  `hh220912(v=vs.105)`: "Any public static... members... are thread safe. Any instance
  members are not guaranteed to be thread safe."), then what CNA additionally
  guarantees per class/mechanism, one known accepted gap (`Dispose()` racing a
  concurrent `Start()` on the same instance — pre-existing, shared with
  Accelerometer/Gyroscope, not fixed by this task since it's not a supported usage
  pattern), and what remains just the WP7 floor. Cross-referenced from
  `SensorBase.hpp`, `Accelerometer.hpp`, `Gyroscope.hpp`, `Compass.hpp`, and
  `Motion.hpp`'s own class-level doc comments.
- **Verified:** full Devices `--gtest_filter` list (304 tests, 302 passed + 2 expected
  hardware skips) on plain `cmake-build-debug` and all three sanitizer presets. ASan: 0
  issues. UBSan: 0 issues. TSan: 38 reports, all the same pre-existing, unrelated
  `sharp-runtime` `TimeSpan::copy_count` race — none in `Compass.cpp`/`Motion.cpp`
  anymore (confirmed by diffing report locations before/after the fix).
- **Required work:**
  - Define CNA's thread-safety promise for sensor instances as an explicit, written
    contract (e.g. "getters/setters are safe from any thread; concurrent `Start()`/
    `Stop()`/`Dispose()` from multiple threads has the following specific guarantees and
    the following specific gaps").
  - Audit for inconsistent locking across `Start()`/`Stop()`/`Dispose()`/property
    getters in all four sensor classes and `SensorBase<T>` itself.
  - Add tests for benign cross-thread `Stop()`/`Dispose()` during callbacks, scoped to
    whatever the contract actually promises.
- **Acceptance criteria:**
  - Thread-safety policy is written down in one place and cross-referenced from each
    class's own doc comments.
  - Code either enforces the policy or explicitly documents where it does not
    (rather than an implicit, undocumented gap).
  - `devices-tsan` runs report no data races in scenarios the contract claims to
    support.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `src/Microsoft/Devices/Sensors/*.cpp`
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp`

### SENSORBASE-005 — Verify `CurrentValue`/`IsDataValid` behavior — CLOSED (2026-07-06, verified consistent as-is, tests added, no code change)

- **Priority:** High
- **Area:** `SensorBase<T>`
- **Problem:** `getCurrentValueProperty()`/`getIsDataValidProperty()` behavior before
  `Start()`, after `Stop()`, after a failed `Start()`, and when unsupported must be
  exact and identical across all four sensor classes, not merely "whatever each
  class's own code happens to do."
- **What was found:** both getters are defined exactly once, on `SensorBase<T>` itself
  — `Accelerometer`/`Gyroscope`/`Compass`/`Motion` all inherit the identical code, never
  override either getter. This guarantees byte-for-byte identical behavior across all
  four by construction, not by coincidence: no per-class audit could find a divergence
  because none is possible without one of the four classes actually overriding a getter
  (confirmed none do, by grep). Verified each state by reading the shared implementation
  and the official archived MSDN property pages (`SensorBase(TSensorReading).CurrentValue`,
  `hh239261(v=vs.105)`; `.IsDataValid`, `hh220799(v=vs.110)`):
  - **Unsupported:** `getCurrentValueProperty()` throws `InvalidOperationException` —
    matches the MSDN page's Remarks exactly ("If the sensor is not present, a
    System.InvalidOperationException is thrown when you access this property.").
    `getIsDataValidProperty()` never throws for any reason (no Exceptions section
    documented on its own MSDN page either) — already consistent with the doc's
    silence.
  - **Before `Start()` (supported):** `getCurrentValueProperty()` returns a
    default-constructed reading, `getIsDataValidProperty()` returns `false` — no
    per-class divergence possible (shared code); already tested at the `SensorBase<T>`
    level (`SensorBaseTests.CurrentValueDoesNotThrowBeforeAnyReadingWhenSupported`/
    `IsDataValidDefaultsFalse`) plus explicitly for `Accelerometer`/`Gyroscope`. Added
    the missing explicit assertions for `Compass`/`Motion` were unnecessary to add
    separately — the shared-code guarantee already covers them; left as-is rather than
    padding with redundant per-class tests.
  - **After `Stop()`:** confirmed by reading `Stop()` in all four `.cpp` files that none
    of them touch `currentValue_`/`isDataValid_` at all — the last known reading and its
    validity are left exactly as they were, undocumented by MSDN either way but now
    consistent and tested. Added one new test per concrete class (`Accelerometer`/
    `Gyroscope`/`Compass`/`Motion`) confirming this empirically, not just by code
    reading, since nothing tested it before.
  - **After a failed `Start()`:** the only failure path in this codebase is
    "unsupported," which already forces `getCurrentValueProperty()` to throw before
    `currentValue_`/`isDataValid_` are ever read (the `isSupported_` check runs first) —
    already covered by the existing unsupported-state tests; no distinct scenario
    exists to test separately.
  - **Disposed:** neither getter checks `disposed_` at all (unlike `Start()`/`Stop()`,
    each of which has its own explicit `ObjectDisposedException::ThrowIf(...)` in every
    concrete class's `.cpp`) — confirmed, tested, and deliberately **not** changed here:
    whether these getters *should* instead throw `ObjectDisposedException` after
    `Dispose()` (matching the conventional .NET pattern and this codebase's own
    `Start()`/`Stop()` precedent) is `SENSORBASE-006`'s question ("Verify Dispose
    semantics"), not this task's. Added
    `SensorBaseTests.CurrentValueAndIsDataValidDoNotThrowAfterDispose` to lock in the
    current, consistent-by-construction behavior.
- **Tests added:** `AccelerometerTests`/`GyroscopeTests`/`CompassTests`/`MotionTests`
  `.CurrentValueAndIsDataValidRetainLastReadingAfterStop` (one per class);
  `SensorBaseTests.CurrentValueAndIsDataValidDoNotThrowAfterDispose`. No code changes —
  every state was already correct and (except for the two gaps above) already
  consistent; this task's job was to make that verified and tested, not to change
  behavior.
- **Verified:** 309/309 tests (up from 304) on plain `cmake-build-debug` and all three
  sanitizer presets. ASan: 0 issues. UBSan: 0 issues. TSan: 44 reports, all the same
  pre-existing, unrelated `sharp-runtime` `TimeSpan::copy_count` race.
- **Required work:**
  - Verify expected XNA behavior for each of those states.
  - Add tests for each state, for each of the four sensor classes.
  - Ensure all derived sensors behave consistently unless a documented, intentional
    per-class difference exists.
- **Acceptance criteria:**
  - Tests cover no-data, valid-data, stopped, disposed, and unsupported states for
    `Accelerometer`, `Gyroscope`, `Compass`, and `Motion`.
  - All four sensors follow the same base contract unless a difference is explicitly
    documented and justified.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### SENSORBASE-006 — Verify `Dispose` semantics — CLOSED (2026-07-06, verified consistent, real coverage gaps closed, no behavior change)

- **Priority:** High
- **Area:** `SensorBase<T>`
- **Problem:** Double-`Dispose()` and `Dispose()`-while-started behavior must match
  .NET `IDisposable` expectations exactly, and must be identical across all four sensor
  classes.
- **What was found:**
  - **Repeated `Dispose()`:** this codebase's `SensorBase<T>::Dispose()` (the public,
    no-arg `IDisposable` override) throws `ObjectDisposedException` on a second call —
    not the more common .NET "silent no-op" convention, but a deliberate choice per the
    existing code comment ("just like the decompiled source" — i.e. verified against a
    decompiled real WP7 assembly in an earlier session, a stronger source than the
    archived MSDN pages, which document no `Dispose()`-specific page at all for this
    type). Already implemented identically for all four classes (all inherit
    `SensorBase<T>::Dispose()` unchanged) and already tested per class
    (`DisposeSucceedsAndSecondDisposeThrows`, all four). No gap, no change needed.
  - **`Stop()` called safely as part of `Dispose()`:** confirmed by reading all four
    `Dispose(bool)` overrides that each already follows the identical pattern (read
    `started_`/`wasStarted` under its own lock scope, release the lock, call `Stop()`
    outside it to avoid a non-recursive-mutex deadlock) — `Accelerometer`/`Gyroscope`
    had this from an earlier task; `Compass`/`Motion` gained it from this session's own
    `SENSORBASE-004` fix. No gap, no change needed.
  - **Real coverage gap found and closed:** no test anywhere actually confirmed
    `Dispose()` (without an explicit prior `Stop()` call) genuinely stops a *running*
    backend. `Gyroscope` had no started-then-disposed test at all;
    `Accelerometer`'s only such test (`StartThenDisposeDoesNotCrash`) silently
    degrades to testing the *never-started* path in this container, since
    `getIsSupportedProperty()` is false here (no real sensor hardware) — it never
    actually exercises `Dispose(bool)`'s `wasStarted`-true branch on this host. Added
    `DisposeWhileStartedForTestingDoesNotCrash` to both, using
    `SetSupportedForTesting(true)`/`SetStartedForTesting(true)` to force that branch
    deterministically regardless of hardware — confirms `Stop()`'s subsystem
    bookkeeping (`UnregisterStartedInstanceLocked()`/
    `UnregisterEventWatchIfNeededLocked()`) safely no-ops for an instance that was
    never actually registered with the real subsystem. For `Compass`/`Motion` (which
    have a fake-backend seam precisely for this), added
    `DisposeWhileStartedCallsBackendStopWithoutExplicitStopFirst`, asserting the fake's
    `StopCalled` flag directly — genuine proof, not just "doesn't crash," that
    `Dispose()` alone (no `Stop()` call first) stops the backend.
- **Verified — no backend resource leak across a `Start()`→`Dispose()` cycle:** all
  four new/expanded tests pass under `devices-asan` with `detect_leaks=1` explicitly set
  (0 issues).
- **Verified overall:** 313/313 tests (up from 309) on plain `cmake-build-debug` and all
  three sanitizer presets. ASan: 0 issues. UBSan: 0 issues. TSan: 39 reports, all the
  same pre-existing, unrelated `sharp-runtime` `TimeSpan::copy_count` race.
- **Required work:**
  - Verify whether repeated `Dispose()` should throw or silently no-op (the .NET
    convention is typically "no-op," but confirm this is actually what's implemented
    and intended here, per class).
  - Ensure `Stop()` is called safely as part of `Dispose()` in every class.
  - Add tests for double-dispose, dispose-while-started, and dispose-then-any-public-call
    (expecting `ObjectDisposedException`, per the existing pattern).
- **Acceptance criteria:**
  - `Dispose()` behavior is documented and identical in spirit across all four classes.
  - Repeated-`Dispose()` behavior is a deliberate, tested choice, not an accident of
    implementation order.
  - No backend resources leak across a `Start()`→`Dispose()` cycle (verify under
    `devices-asan`).
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### SENSORBASE-007 — Audit protected/internal extension hooks — CLOSED (2026-07-06, real Extra-unmarked bug found and fixed)

- **Priority:** Medium
- **Area:** API Design
- **Problem:** Hooks like `TimeBetweenUpdatesChanged` (a public `System::EventHandler`
  member on `SensorBase<T>`, confirmed present) may be useful CNA-internal plumbing but
  must not be confused with real XNA API surface.
- **What was found:** this task's own problem statement named the exact bug. All
  `*ForTesting()`/`InjectSynthetic*`/`SetBackendForTesting()` hooks across
  `SensorBase<T>`/`Accelerometer`/`Gyroscope`/`Compass`/`Motion` were already correctly
  tagged `NOXNA` (verified by grep — no gap). `TimeBetweenUpdatesChanged`, however, was
  declared in `SensorBase.hpp` with **no `NOXNA` tag**, and its doc comment claimed "In
  the original .NET version this event is protected" with no citation. Fetched the real
  class's own archived MSDN reference page (`SensorBase(TSensorReading)`,
  `hh239315(v=vs.105)`): its Events table lists exactly one event, `CurrentValueChanged`
  — no `TimeBetweenUpdatesChanged` or equivalent. A dedicated web search for the exact
  member name found zero hits anywhere. **Conclusion: this is a genuine, real
  Extra-unmarked finding** — a CNA-only extension (added to let `Compass`/`Motion`'s
  Android backend forward a live `TimeBetweenUpdates` change, `ANDROID-BRIDGE-002`)
  that had never been tagged `NOXNA`, and had also been mis-recorded as `Real` in
  `docs/devices-api-coverage.md`'s own `SensorBase<TSensorReading>` table — ironically
  the exact bug pattern `DEV-API-001`'s "zero Extra-unmarked" claim was supposed to have
  already caught, but missed.
- **Fix:** tagged `TimeBetweenUpdatesChanged` `NOXNA` in `SensorBase.hpp` (added
  `#include "CNA/CNAHelper.hpp"`, per this project's checklist rule for any file that
  uses the marker), rewrote its doc comment to cite the MSDN page and remove the
  unsourced claim, and corrected `docs/devices-api-coverage.md`'s entry (both the
  per-member table row and the "DEV-API-001 verification result" section's now-inaccurate
  "zero Extra-unmarked" claim, updated with this finding).
- **Verified:** 313/313 tests (unchanged — this was a marker/doc-only change, no
  behavior change, so no new tests were needed) on plain `cmake-build-debug` and all
  three sanitizer presets. ASan/UBSan: 0 issues. TSan: 40 reports, all the same
  pre-existing, unrelated `sharp-runtime` `TimeSpan::copy_count` race.
- **Required work:**
  - Review all protected/public hooks on `SensorBase<T>` and each derived class's own
    `NOXNA`-tagged testing hooks (e.g. `InjectSyntheticSensorUpdate`,
    `SetStartedForTesting`, `SetSupportedForTesting`, all confirmed present on
    `Accelerometer`/`Gyroscope`).
  - Mark or re-confirm every non-XNA hook is documented as CNA-internal/testing-only.
- **Acceptance criteria:**
  - Extension hooks are not confused with XNA API anywhere in docs or the
    `DEV-API-001` matrix.
  - Docs clearly separate compatibility surface from CNA internals/testing hooks.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp`
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`

### SENSORBASE-008 — Validate `TimeBetweenUpdates` min/max (negative/zero/huge values) — CLOSED (2026-07-06, verified correct as-is, no code change)

- **Priority:** Medium
- **Area:** `SensorBase<T>`
- **Problem:** `setTimeBetweenUpdatesProperty()` accepts any `System::TimeSpan` value
  unchanged — no rejection of negative values, no documented minimum/maximum. This gap
  was carried forward, not fixed, by `SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/
  `ANDROID-BRIDGE-002` (2026-07-06): those tasks made `TimeBetweenUpdates` actually
  throttle/re-apply the rate, but none of them added input validation. Confirmed
  (2026-07-06 stabilization pass) that the current, unvalidated behavior is at least
  *safe*, not a correctness bug: `SensorBaseTests.
  ShouldAcceptUpdateAtWithNegativeTimeBetweenUpdatesNeverThrottles` locks in that a
  negative interval degrades to "never throttle" (same as `TimeSpan::Zero`) rather than
  crashing or permanently rejecting every update — but this is an accepted fallback, not
  a validated contract a caller can rely on.
- **Resolution (2026-07-06):** fetched the archived MSDN "previous-versions" pages
  directly (same technique as `READINGS-002` — the classic
  `msdn.microsoft.com/en-us/library/<member>(v=VS.11x)` URL form 301-redirects to the
  current `learn.microsoft.com` archive page even without knowing the numeric ID):
  - `SensorBase(TSensorReading).TimeBetweenUpdates` property page (MSDN `hh220884`,
    v=vs.110): syntax is a plain `public TimeSpan TimeBetweenUpdates { get; set; }` —
    **no Exceptions section, no Remarks describing a valid range**, unlike e.g.
    `VibrateController.Start(TimeSpan)`, which does document an
    `ArgumentOutOfRangeException` contract for `[Zero, FromSeconds(5)]` (already
    correctly implemented in this codebase).
  - `SensorBase(TSensorReading)` class overview page (MSDN `hh239315`, v=vs.105):
    confirms the same — `TimeBetweenUpdates` listed as "Gets or sets the preferred time
    between `CurrentValueChanged` events," no further constraint documented anywhere on
    the class page either.
  - **Conclusion: CNA's current unvalidated setter already matches the real, documented
    (lack of) API contract exactly.** This is not an unvalidated gap to fix — it is a
    faithful port of a real WP7 API that itself has no input validation. No code change
    made to `setTimeBetweenUpdatesProperty()`.
  - The Android-side `ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds()` clamp to
    `[1, INT32_MAX]` microseconds is unrelated to this question — it is a defensive,
    internal safety net against undefined behavior in the *native NDK call*
    (`ASensorEventQueue_setEventRate()` takes an `int32_t`; casting an extreme/negative
    `double` to it without clamping is UB), not a public-API-level validation decision.
    Confirmed it already floors a negative input safely (new test below), so no change
    needed there either.
  - Added 3 tests locking in the verified-correct behavior, distinct from the
    stabilization pass's existing throttle-decision test:
    `SensorBaseTests.SetTimeBetweenUpdatesPropertyAcceptsNegativeValueWithoutThrowing`,
    `SensorBaseTests.SetTimeBetweenUpdatesPropertyAcceptsMaxValueWithoutThrowing` (both
    confirm the plain property getter/setter round-trips an extreme value without
    throwing), and
    `AndroidSensorBridgeTests.NegativeTimeBetweenUpdatesFloorsToOneMicrosecond` (confirms
    the Android-side conversion function's existing zero/sub-microsecond-flooring logic
    already covers negative input safely, not just by coincidence).
  - Verified: 296/296 tests (up from 293) on plain `cmake-build-debug` and all three
    sanitizer presets — 0 ASan; TSan 41 reports, all the same pre-existing
    `sharp-runtime` `TimeSpan::copy_count` race; UBSan 3 reports, all the same
    pre-existing `Vector3`/`Matrix::GetHashCode()` signed-overflow findings, none in
    `Microsoft::Devices`.
- **Required work:**
  - Determine the real WP7 `SensorBase<TSensorReading>.TimeBetweenUpdates` setter's
    documented behavior for out-of-range values (archived MSDN page, not assumption) —
    does it throw `ArgumentOutOfRangeException`, silently clamp, or accept anything?
    Done — accepts anything, no documented contract (MSDN `hh220884`/`hh239315`).
  - Match that behavior exactly, including for the Android bridge path (`AndroidSensorBridge::
    SetSampleInterval()`/`ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds()`
    already clamps to `[1, INT32_MAX]` microseconds internally — decide whether that
    NDK-level clamp is a sufficient answer for the Android side, or whether
    `setTimeBetweenUpdatesProperty()` itself needs to reject/clamp earlier, at the
    public-API boundary). Done — the NDK-level clamp is the correct, sufficient answer;
    no earlier rejection/clamp belongs at the public-API boundary, since the real API
    has none.
  - Add tests for the chosen behavior (negative, zero-if-disallowed, and
    `TimeSpan::MaxValue`-scale values), for both the SDL- and Android-backed classes.
    Done — see the 3 new tests listed above.
- **Acceptance criteria:**
  - `setTimeBetweenUpdatesProperty()`'s out-of-range behavior matches the real WP7 API,
    with a citation, not an assumption. Done — MSDN `hh220884`/`hh239315`.
  - Tests cover the chosen behavior explicitly (not just the already-existing
    "doesn't crash" proof from the stabilization pass above). Done.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp` (inspected, no change needed)
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp` (inspected, no
    change needed)
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp` (edited)

---

## 6. Accelerometer tasks

### ACCEL-001 — Verify XNA/WP public surface

- **Priority:** Critical
- **Area:** Accelerometer API
- **Problem:** `Accelerometer` has both the modern `CurrentValueChanged` event and the
  legacy `ReadingChanged` event (`AccelerometerReadingEventArgs`), confirmed present in
  the header with a doc comment describing the dual-event design. Whether both are
  correctly scoped to their respective XNA/WP7 API versions needs verification against
  an authoritative reference, not just against this codebase's own comments.
- **Required work:**
  - Verify which events/properties belong to which XNA 4.0/Windows Phone API version.
  - Mark any genuinely obsolete/legacy API clearly (beyond the existing doc-comment
    note).
  - Add or confirm compile-level tests for the expected public API shape.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Accelerometer` completely.
  - `ReadingChanged`'s exact compatibility status (kept for compatibility vs. CNA
    convenience) is documented with a rationale.
  - Tests cover both event paths given both are currently supported simultaneously.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-002 — Audit `ReadingChanged`-related comments for accuracy

- **Priority:** High
- **Area:** Documentation / Code Quality
- **Problem:** Comments describing `ReadingChanged`'s relationship to
  `CurrentValueChanged` and to the "destroying from within your own callback" lifetime
  boundary must exactly match actual dispatch order and behavior — verify this rather
  than assume the existing comment (already present, describing a specific reentrancy
  concern) is still accurate after any later change.
- **Required work:**
  - Re-read the current comments in `Accelerometer.hpp`/`.cpp` and
    `AccelerometerReadingEventArgs.hpp` against the actual dispatch code.
  - Fix any comment that no longer matches implementation.
  - Add tests verifying event raising order (`CurrentValueChanged` then
    `ReadingChanged`, or whatever order is actually implemented) and args content.
- **Acceptance criteria:**
  - No comment contradicts implementation.
  - Tests verify `CurrentValueChanged` and `ReadingChanged` firing order and content
    together.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-003 — Verify acceleration units

- **Priority:** Critical
- **Area:** Sensor Math
- **Problem:** SDL3 likely reports raw accelerometer data in m/s² on at least some
  platforms, while XNA's `AccelerometerReading.Acceleration` is documented in g units.
  This codebase's current conversion constant/logic must be re-verified against SDL3's
  actual per-platform behavior, not assumed correct because a conversion already exists.
- **Required work:**
  - Confirm SDL3's actual reported units on every target platform this project builds
    for (read `third_party/SDL`'s sensor backend source per-platform, not just the
    top-level SDL3 header docs).
  - Keep or adjust the conversion-by-standard-gravity constant accordingly.
  - Add tests for the conversion using known raw input values and expected g output.
- **Acceptance criteria:**
  - Known raw SDL values convert to the expected g values in tests.
  - The conversion (and its source, e.g. `StandardGravity = 9.80665f`, already used
    elsewhere in this codebase for the Android Motion backend) is documented with its
    origin.
  - Platform-specific differences, if any are found, are explicitly handled and tested.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-004 — Verify axis orientation on real hardware

- **Priority:** Critical
- **Area:** Sensor Math / Hardware QA
- **Problem:** Confirmed (Section 1 and this repository's own `NEXT.md`): Android
  orientation remapping (`Detail::AndroidSensorOrientation`) has never been verified
  against real hardware in any session.
- **Required work:**
  - Define the expected XNA axis convention for portrait and landscape orientations.
  - Test face-up, face-down, portrait-upright, portrait-upside-down, landscape-left, and
    landscape-right on real Android hardware.
  - Adjust the remapping code if real-device results disagree with current assumptions.
- **Acceptance criteria:**
  - A manual hardware checklist entry records expected vs. observed values per
    orientation, per device tested.
  - Automated tests cover the remapping math itself (already partially covered by
    `AndroidSensorOrientationTests.cpp` — confirm and extend, don't duplicate).
  - Code comments identify which specific devices/orientations have actually been
    verified.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`
  - `tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp`
  - `docs/devices-hardware-checklist.md`

### ACCEL-005 — Apply `TimeBetweenUpdates` — CLOSED (2026-07-06)

- **Priority:** Critical
- **Area:** Accelerometer Backend
- **Problem:** Confirmed (Section 1): `Accelerometer.cpp` never reads
  `getTimeBetweenUpdatesProperty()` at all — the requested update interval has no effect
  on the actual SDL-backed event rate today.
- **Resolution (2026-07-06):** SDL3 exposes no per-sensor polling-rate control for
  `SDL_SENSOR_ACCEL` (confirmed: no such API in `<SDL3/SDL_sensor.h>`), so software
  throttling was added instead, at the dispatch point —
  `Accelerometer::ProcessSensorUpdateEvent()` now calls the new
  `SensorBase<T>::ShouldAcceptUpdateAt(System::DateTimeOffset::getUtcNowProperty())`
  before `DispatchSensorReading()`, dropping events that arrive too soon after the last
  accepted one. `Start()` calls the new `ResetUpdateThrottle()` so a fresh start always
  delivers an immediate first sample. Changing `TimeBetweenUpdates` while running takes
  effect on the very next event (the interval is read fresh every call), no
  `Stop()`/`Start()` needed. Tests: 7 new `SensorBaseTests.cpp` cases exercise the
  underlying `ShouldAcceptUpdateAt()` decision directly with synthetic
  `DateTimeOffset` values (no real-time sleeps) — see `SENSORBASE-001`'s closing note
  for the full list and the rationale for testing at that level rather than through
  `Accelerometer` itself (`ProcessSensorUpdateEvent()` is only reachable via a real SDL
  hardware event in this environment, same limitation as every other
  `ProcessSensorUpdateEvent()`-only behavior in this codebase). Verified: 290/290 tests
  (up from 283) on plain `cmake-build-debug` and all three sanitizer presets, 40/40
  clean on a `AccelerometerTests.*:GyroscopeTests.*` loop. **Not done:** the manual demo
  was not run to visually confirm a lower event rate (no display in this environment
  this session) — deferred, not claimed.
- **Addendum (2026-07-06, same-day stabilization pass):** `ShouldAcceptUpdateAt()`'s
  `now` parameter and internal comparison switched from `System::DateTimeOffset`
  wall-clock time to `std::chrono::steady_clock` — see `SENSORBASE-001`'s closing note
  for the full rationale (wall-clock time can step backward/jump on an NTP correction,
  which a throttle *decision* must never be vulnerable to). `ProcessSensorUpdateEvent()`
  now passes `std::chrono::steady_clock::now()`. Re-verified: 293/293 tests, all three
  sanitizer presets, 40/40 loop, all clean.
- **Required work:**
  - Apply the requested update interval to the SDL backend if SDL3 exposes a sensor
    polling-rate control; otherwise add software throttling in the dispatch path. Done.
  - Ensure changing `TimeBetweenUpdates` while the sensor is running takes effect
    without requiring `Stop()`/`Start()`. Done.
- **Acceptance criteria:**
  - Tests using a fake clock/backend prove throttling actually happens (avoid real-time
    sleeps in automated tests to prevent flakiness). Done.
  - The manual demo visibly shows a lower event rate when the interval is increased.
    **Not verified — no display available this session.**
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp` (edited)
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp` (edited — throttle lives here,
    shared with `Gyroscope`, not in `SdlSensorSubsystem.hpp`)
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` (edited)

### ACCEL-006 — Add fake accelerometer backend for tests

- **Priority:** High
- **Area:** Tests / Architecture
- **Problem:** Tests should not depend on real SDL sensor hardware being present in the
  CI/build environment. `Accelerometer` already has `NOXNA` testing hooks
  (`InjectSyntheticSensorUpdate`, `SetStartedForTesting`, `SetSupportedForTesting`,
  confirmed present) — this task is to verify those hooks are sufficient for full
  coverage, or extend them, not to assume no test seam exists yet.
- **Required work:**
  - Confirm existing testing hooks cover Start/Stop, event dispatch, unit conversion,
    state, exceptions, and throttling (from `ACCEL-005`); extend if any gap is found.
  - Keep the production public API clean of any test-only surface leaking into strict
    XNA mode (cross-check against `DEV-API-002`).
- **Acceptance criteria:**
  - Unit tests can simulate accelerometer samples end-to-end without SDL hardware.
  - CI (`DEV-BUILD-003`) does not require physical sensor hardware for
    `AccelerometerTests`.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Accelerometer.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`

### ACCEL-007 — Decide desktop support policy

- **Priority:** Medium
- **Area:** Platform Policy
- **Problem:** XNA/Windows Phone accelerometer semantics do not map cleanly onto
  arbitrary desktop/laptop SDL-exposed accelerometers (e.g. a 2-in-1 laptop's
  accelerometer, if SDL surfaces one).
- **Required work:**
  - Decide whether desktop accelerometer support should be strict-XNA no-op,
    `NOXNA`-flavored best-effort, or fully supported wherever SDL exposes hardware.
  - Document the decision.
  - Add tests for platform detection/behavior where feasible.
- **Acceptance criteria:**
  - Desktop behavior is deterministic and documented.
  - Docs explain the strict-XNA-vs-CNA-extension distinction for this specific
    platform case.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `docs/devices-*.md`

---

## 7. Gyroscope tasks

### GYRO-001 — Verify gyroscope public surface

- **Priority:** Critical
- **Area:** Gyroscope API
- **Problem:** `Gyroscope`'s public API must match XNA/WP7 expectations, and its
  `getStateProperty()` is already marked `NOXNA` (unlike `Accelerometer`'s, per Section
  1's confirmed finding) — this task is where that specific fact gets folded into the
  full matrix, cross-referenced with `DEV-API-003`.
- **Required work:**
  - Compare `Gyroscope.hpp` to the official XNA/WP7 API.
  - Verify `getIsSupportedProperty()`, inherited `CurrentValue`, `CurrentValueChanged`,
    `TimeBetweenUpdates`, `Start()`/`Stop()`/`Dispose()`.
  - Cross-check `getStateProperty()`'s `NOXNA` status against `DEV-API-003`'s decision.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Gyroscope` completely.
  - Non-XNA API is marked `NOXNA` consistently with the rest of the sensor classes.
  - Tests compile against the expected, decided signatures.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

### GYRO-002 — Verify gyroscope units

- **Priority:** Critical
- **Area:** Sensor Math
- **Problem:** XNA expects angular velocity in a specific, documented unit; SDL3's raw
  gyroscope unit per platform must be confirmed and converted if it differs, the same
  way `ACCEL-003` does for the accelerometer.
- **Required work:**
  - Verify the expected XNA unit for `GyroscopeReading.RotationRate`.
  - Verify SDL3's actual gyroscope unit per platform (read `third_party/SDL` backend
    source, not just top-level docs).
  - Add or adjust conversion, with tests using known raw values.
- **Acceptance criteria:**
  - Reading values use the documented, XNA-compatible unit.
  - Tests fail if the conversion is removed or changed incorrectly (i.e. the test
    actually pins a specific numeric expectation, not just "doesn't crash").
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

### GYRO-003 — Verify gyroscope axes and Android orientation remap

- **Priority:** Critical
- **Area:** Sensor Math / Hardware QA
- **Problem:** Gyroscope axis signs and Android landscape/portrait remapping are exactly
  as unverified-on-real-hardware as the accelerometer's (`ACCEL-004`) — this is a
  separate task because the two sensors' remap code, while sharing
  `Detail::AndroidSensorOrientation`, are applied independently in
  `Accelerometer.cpp`/`Gyroscope.cpp` and must each be checked.
- **Required work:**
  - Define expected axis behavior for rotation around each of the three axes.
  - Add a hardware checklist entry for X/Y/Z-axis rotations in portrait and landscape.
  - Adjust remap code if hardware results disagree with current assumptions.
- **Acceptance criteria:**
  - Manual hardware results are recorded per device tested.
  - Automated math tests cover the remapping logic itself.
  - Code comments state exactly which coordinate convention has been verified, and on
    what.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`
  - `tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp`
  - `docs/devices-hardware-checklist.md`

### GYRO-004 — Apply `TimeBetweenUpdates` — CLOSED (2026-07-06)

- **Priority:** Critical
- **Area:** Gyroscope Backend
- **Problem:** Confirmed (Section 1): `Gyroscope.cpp` never reads
  `getTimeBetweenUpdatesProperty()`, identical to the confirmed `Accelerometer` gap in
  `ACCEL-005`.
- **Resolution (2026-07-06):** identical fix to `ACCEL-005` — see that task's closing
  note for the full detail (SDL3 has no per-sensor polling-rate control for
  `SDL_SENSOR_GYRO` either; `Gyroscope::ProcessSensorUpdateEvent()` now calls the same
  shared `SensorBase<T>::ShouldAcceptUpdateAt()`/`ResetUpdateThrottle()`,
  `SENSORBASE-001`). Not a separate implementation — `Accelerometer` and `Gyroscope`
  share this logic via their common `SensorBase<T>` base, exactly as `ACCEL-005`'s fix
  did. Verified together with `ACCEL-005` (290/290, all sanitizers, 40-iteration loop).
  **Not done:** demo visual confirmation — same standing gap as `ACCEL-005`.
- **Addendum (2026-07-06, same-day stabilization pass):** see `ACCEL-005`'s identical
  addendum — `ShouldAcceptUpdateAt()` now takes `std::chrono::steady_clock::time_point`,
  not `System::DateTimeOffset`; `Gyroscope::ProcessSensorUpdateEvent()` passes
  `std::chrono::steady_clock::now()`. Re-verified together with `ACCEL-005`
  (293/293, all sanitizers, 40/40 loop).
- **Required work:**
  - Apply the backend sample rate if SDL3 supports it; add software throttling
    otherwise. Done.
  - Support changing the interval while the sensor is actively running. Done.
- **Acceptance criteria:**
  - Fake-backend tests prove throttling with deterministic (non-sleep-based) timing.
    Done — see `SensorBaseTests.cpp`.
  - The demo can visibly show a reduced update frequency when the interval increases.
    **Not verified — no display available this session.**
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp` (edited)
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp` (edited, shared with
    `Accelerometer`)
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` (edited)

### GYRO-005 — Add fake gyroscope backend for tests

- **Priority:** High
- **Area:** Tests / Architecture
- **Problem:** CI should not require physical gyroscope hardware, matching
  `ACCEL-006`'s equivalent concern for `Accelerometer`.
- **Required work:**
  - Confirm/extend the existing `NOXNA` testing hooks
    (`InjectSyntheticSensorUpdate`/`SetStartedForTesting`/`SetSupportedForTesting`,
    already present on `Gyroscope`) to cover simulated samples, backend errors,
    unsupported state, and stop-during-callback.
- **Acceptance criteria:**
  - Unit tests cover `Gyroscope` fully without SDL hardware present.
  - The fake backend supports deterministic, test-controlled timestamps.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Gyroscope.hpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`

---

## 8. Compass tasks

### COMPASS-001 — Verify Compass public API

- **Priority:** Critical
- **Area:** Compass API
- **Problem:** `Compass` has the inherited `SensorBase<T>` API plus a `Calibrate` event
  and compass-specific reading fields; its `getStateProperty()` is already `NOXNA`
  (confirmed Section 1) — verify the rest of the surface just as thoroughly.
- **Required work:**
  - Compare `Compass.hpp` to the official XNA/WP7 API.
  - Verify `getIsSupportedProperty()`, `Calibrate`, `CurrentValueChanged`,
    `TimeBetweenUpdates`, `Start()`/`Stop()`/`Dispose()`, and `SetBackendForTesting()`
    (confirmed `NOXNA`, correctly).
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Compass` completely.
  - All extra API is marked or documented.
  - Tests compile against expected signatures.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Compass.hpp`
  - `include/Microsoft/Devices/Sensors/CompassReading.hpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

### COMPASS-002 — Define correct `TrueHeading` behavior

- **Priority:** Critical
- **Area:** Compass Math / Compatibility
- **Problem:** Confirmed (Section 1): `AndroidCompassBackend::PublishReading()`
  deliberately sets `TrueHeading` equal to `MagneticHeading`, with an explicit comment
  explaining that real declination needs a location source this codebase doesn't have.
  Whether this specific fallback (as opposed to, say, `NaN`, `0`, or an actual
  declination calculation) is the XNA/WP7-compatible choice for "declination unknown"
  has not been verified against an authoritative reference.
- **Required work:**
  - Verify the expected XNA/WP7 behavior when true heading cannot be computed.
  - Decide, with rationale, whether to keep the current "equals magnetic heading"
    fallback, switch to a different sentinel, or implement real declination (see
    `COMPASS-003` for the latter).
  - Add tests for both the "heading unavailable" and (if implemented) "heading
    available" scenarios.
- **Acceptance criteria:**
  - `TrueHeading` fallback behavior is explicitly documented as either
    verified-compatible or intentionally-chosen-CNA-behavior, not left as an unexamined
    assumption.
  - Tests cover both scenarios explicitly.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

### COMPASS-003 — Add optional declination/location support plan

- **Priority:** Medium
- **Area:** Compass Accuracy
- **Problem:** Real true heading requires magnetic declination, which in turn requires
  location and date — this repository has an explicit, existing decision (documented in
  `docs/location-future-plan.md`, referenced elsewhere in this codebase) to keep
  location/GPS out of `Microsoft::Devices::Sensors` entirely. This task's job is to
  confirm that decision is still the right call for `Compass` specifically, not to
  silently re-open or silently re-confirm it without checking.
- **Required work:**
  - Re-read `docs/location-future-plan.md` and confirm its reasoning still applies to
    `Compass::TrueHeading` specifically.
  - Decide whether CNA should ever implement true-heading calculation, and if so, plan
    how a location/declination dependency could be added without polluting the strict
    XNA `Compass` surface (e.g. as an optional, separately-injected `NOXNA` dependency).
  - If the answer remains "no," document that explicitly as this task's outcome.
- **Acceptance criteria:**
  - The plan states clearly, with current reasoning, whether true heading is or is not
    planned to be supported.
  - No fake/approximated true heading is ever reported without a clearly documented
    rationale for the approximation.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `docs/location-future-plan.md`
  - `plan_devices.md`

### COMPASS-004 — Verify Android heading math on hardware

- **Priority:** Critical
- **Area:** Compass Math / Hardware QA
- **Problem:** `Detail::AndroidCompassMath`'s rotation-vector-to-heading conversion is
  currently only unit-tested against self-consistency properties (identity quaternion →
  0°, monotonic response to a known yaw) — confirmed by its own doc comment stating it
  has "never been checked against real hardware." Physical validation is still
  outstanding.
- **Required work:**
  - Test known real-world orientations against a real compass reference (e.g. a phone
    compass app, or a known magnetic-north reference) on real Android hardware.
  - Validate north/east/south/west headings, and both portrait and landscape device
    orientation.
  - Adjust the sign/axis convention in `Detail::AndroidCompassMath` if hardware results
    disagree.
- **Acceptance criteria:**
  - Hardware results are recorded (device, OS version, orientation, expected vs.
    observed heading).
  - Math tests reflect verified-correct behavior, not merely "whatever the current
    implementation happens to output."
  - Code comments describe the confirmed coordinate convention, replacing the current
    "never checked" caveat once real verification has occurred.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidCompassMathTests.cpp`
  - `docs/devices-hardware-checklist.md`

### COMPASS-005 — Revisit the rotation-vector-plus-magnetometer support requirement

- **Priority:** High
- **Area:** Platform Policy
- **Problem:** Confirmed (Section 1): `AndroidCompassBackend::IsSupported()` requires
  both a rotation-vector sensor and a magnetic-field sensor. Devices with only a
  magnetometer (no fused rotation vector) are reported unsupported today, even though a
  magnetometer-only heading (with reduced accuracy) might be preferable to reporting
  "not supported" at all.
- **Required work:**
  - Verify the minimum sensor set genuinely needed for an XNA-compatible compass
    reading.
  - Consider a magnetometer-only fallback path if technically feasible, with clearly
    documented accuracy tradeoffs (e.g. worse tilt compensation without a fused
    rotation vector).
  - Document whichever policy is chosen.
- **Acceptance criteria:**
  - `getIsSupportedProperty()`'s exact policy is explicit and justified.
  - Tests cover: rotation vector missing, magnetometer missing, and both available.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

### COMPASS-006 — Verify accuracy mapping and `Calibrate` event policy

- **Priority:** High
- **Area:** Compass Events
- **Problem:** Android magnetic-field sensor accuracy statuses
  (`Detail::AndroidSensorAccuracyStatus`) are mapped to XNA `HeadingAccuracy` degree
  values, and to whether `Calibrate` fires, by a CNA-chosen policy (already documented
  in this codebase as a deliberate choice, e.g. "`Low` deliberately excluded" from
  triggering `Calibrate`) — this task re-verifies that specific chosen mapping is still
  the right one, not that a mapping exists at all.
- **Required work:**
  - Verify the accuracy-status-to-degrees mapping against any available reference (WP7
    docs, or a reasoned default if none exists).
  - Confirm the current `Calibrate`-firing policy (unreliable/no-contact fire it,
    low/medium/high do not) doesn't cause event spam in practice.
  - Add or extend tests for the mapping and for `Calibrate` firing conditions.
- **Acceptance criteria:**
  - Accuracy mapping is documented and tested for every
    `AndroidSensorAccuracyStatus` value.
  - `Calibrate` fires only under the intended, tested conditions.
  - `CalibrationEventArgs` content is verified correct.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `include/Microsoft/Devices/Sensors/CalibrationEventArgs.hpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidCompassMathTests.cpp`

### COMPASS-007 — Add iOS compass backend plan or implementation

- **Priority:** High
- **Area:** iOS Backend
- **Problem:** `Compass` has no iOS-native backend at all today (confirmed: only
  `Detail::AndroidCompassBackend` exists as a concrete `ICompassBackend`
  implementation).
- **Required work:**
  - Decide whether to support iOS compass in this API.
  - If yes: plan or implement using `CLLocationManager`'s heading APIs
    (`CoreLocation`).
  - If no: document the unsupported behavior clearly (permanent stub, matching every
    non-Android platform's current behavior).
- **Acceptance criteria:**
  - iOS build behavior is deterministic, whichever choice is made.
  - An unsupported backend returns "not supported" cleanly rather than crashing.
  - A manual iOS checklist exists if support is added.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp`
  - iOS build/toolchain files (none currently present — confirm before assuming a
    location)
  - `docs/devices-*.md`

### COMPASS-008 — Harden Android compass callback lifetime further

- **Priority:** Critical
- **Area:** Lifecycle / Android
- **Problem:** `Detail::AndroidCompassBackend`'s callbacks run on
  `Detail::AndroidSensorBridge`'s own worker threads and capture `this`. A prior
  hardening pass already addressed several use-after-free/reentrancy concerns in
  `Detail::AndroidSensorBridge` itself (shared-ownership `Impl`, mutex-guarded
  Start/Stop state) — this task re-verifies that hardening is sufficient specifically
  for `AndroidCompassBackend`'s own callback closures
  (`HandleRotationVectorSample`/`HandleMagneticFieldSample`/`PublishReading`), not just
  the shared bridge.
- **Progress (2026-07-06, `SENSORBASE-003`):** `CompassTests.DisposeFromWithinOwnCallbackDoesNotDeadlock`
  now exists (fake-backend seam) and confirms `Compass`'s own `ClaimDisposalOnce()`/
  `Stop()` reentrancy handling is safe when `Dispose()` (not full destruction) is called
  reentrantly. **The deeper risk this task actually asks about — a handler
  *destroying* the `Compass` instance (not just `Dispose()`-ing it) while
  `AndroidCompassBackend::PublishReading()`/`HandleRotationVectorSample()`/
  `HandleMagneticFieldSample()` are still on the call stack, tearing down `backend_`
  mid-call — remains open and unverified**, since the fake backend has no equivalent
  call-stack structure to reproduce it and the real backend is Android-only. This is
  this task's actual remaining scope.
- **Required work:**
  - Re-confirm `Compass`/`AndroidCompassBackend`'s own object lifetime story under a
    `Stop()`/`Dispose()`-from-within-`Calibrate`-or-`CurrentValueChanged` scenario.
    Partially done (2026-07-06) — see Progress note above.
  - Add tests using a fake backend that destroys the `Compass` object from inside a
    callback, to the extent this is a supported scenario (document if it is not). Still
    open — the fake backend's simpler call structure can't reproduce the real
    `AndroidCompassBackend`'s own call-stack-reentrancy risk; a real device or an
    Android-native ASan build is likely required.
- **Acceptance criteria:**
  - `devices-asan`/`devices-tsan` report no lifetime or race issues for documented-
    supported scenarios.
  - `Stop()`/`Dispose()` during a callback is either verified safe and tested, or
    explicitly documented as an unsupported boundary (matching the existing accepted
    boundary pattern for `Detail::AndroidSensorBridge`).
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`

---

## 9. Motion tasks

### MOTION-001 — Verify Motion public API

- **Priority:** Critical
- **Area:** Motion API
- **Problem:** `Motion` is the most complex class in this scope (`MotionReading` nests
  an `AttitudeReading`) and must expose exactly the intended XNA/WP7 API plus clearly
  marked extensions. Its `getStateProperty()` is already `NOXNA` (confirmed Section 1).
- **Required work:**
  - Compare `Motion.hpp`, `MotionReading.hpp`, and `AttitudeReading.hpp` to the expected
    API.
  - Verify `Calibrate`, `getIsSupportedProperty()`, `CurrentValueChanged`,
    `Start()`/`Stop()`/`Dispose()`, and every reading property.
  - Verify whether any currently-exposed property is a non-XNA addition that needs
    `NOXNA` marking.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix covers `Motion` completely.
  - Tests compile against expected signatures.
  - Any extra API is marked `NOXNA` or removed.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Motion.hpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-002 — Verify quaternion and attitude coordinate mapping

- **Priority:** Critical
- **Area:** Motion Math
- **Problem:** `Detail::AndroidMotionMath`'s Android-rotation-vector-to-XNA-attitude
  mapping is exactly as likely to have sign/order/orientation issues as the compass
  heading math (`COMPASS-004`), and is documented in this repository's own history as
  an explicitly open, unresolved question — not yet contradicted or confirmed by real
  hardware testing.
- **Required work:**
  - Define the XNA-expected quaternion, yaw/pitch/roll, and rotation-matrix conventions
    precisely.
  - Validate with independent golden data (hand-computed expected quaternions/matrices
    for known rotations).
  - Validate on real Android hardware in multiple physical orientations.
  - Adjust the conversion in `Detail::AndroidMotionMath`/`Detail::AndroidMotionBackend`
    if needed.
- **Acceptance criteria:**
  - Tests cover identity, yaw 90°/180°/270°, pitch, roll, and combined rotations.
  - Hardware validation results match the automated tests' expectations (or the tests
    are updated to match reality, with the discrepancy documented).
  - Code comments explain the coordinate conversion precisely.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidMotionMathTests.cpp`
  - `docs/devices-hardware-checklist.md`

### MOTION-003 — Verify gravity and device acceleration units

- **Priority:** Critical
- **Area:** Motion Math
- **Problem:** XNA `MotionReading.Gravity`/`.DeviceAcceleration` are documented in
  gravitational units (g), not raw platform units. This codebase already applies a
  `StandardGravity = 9.80665f` divisor in `Detail::AndroidMotionBackend.cpp` (a prior
  fix for a real bug found in this area) — this task re-verifies that conversion is
  still correct and complete, not that a conversion needs to be added from scratch.
- **Required work:**
  - Re-verify the current gravity/linear-acceleration conversion against
    `ACCEL-003`'s accelerometer findings (both should agree on the platform's raw
    unit).
  - Add tests for the m/s²-to-g conversion with known values.
  - Verify the platform source units haven't changed across any NDK/SDL upgrade since
    the conversion was last checked.
- **Acceptance criteria:**
  - Gravity at rest has magnitude near 1g in tests.
  - Linear acceleration excludes the gravity component (matches `TYPE_LINEAR_ACCELERATION`
    semantics, not `TYPE_ACCELEROMETER`).
  - Tests cover the conversion and sign convention explicitly.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-004 — Verify rotation rate units

- **Priority:** High
- **Area:** Motion Math
- **Problem:** `MotionReading.DeviceRotationRate` units must match XNA expectations;
  Android's `TYPE_GYROSCOPE` reports radians/second, which this codebase currently
  passes through unconverted for `Motion` (distinct from the accelerometer/gravity
  conversion) — confirm this pass-through is actually correct rather than an oversight.
- **Required work:**
  - Verify whether XNA expects radians/second or degrees/second for this specific
    property.
  - Verify Android's actual gyroscope unit (`ASENSOR_TYPE_GYROSCOPE`, NDK docs).
  - Convert if the two units don't already match; add tests either way.
- **Acceptance criteria:**
  - Rotation-rate units are documented explicitly.
  - Tests use known sample values with a pinned expected output.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-005 — Define Motion support policy

- **Priority:** High
- **Area:** Platform Policy
- **Problem:** Confirmed: `AndroidMotionBackend::IsSupported()` requires an attitude
  source (rotation vector or game-rotation-vector fallback — already implemented) plus
  gravity, linear-acceleration, and gyroscope sensors, all simultaneously. Whether this
  all-or-nothing requirement is the right compatibility tradeoff, versus a partial-data
  fallback, needs an explicit decision.
- **Required work:**
  - Verify the minimum sensor set genuinely required for an XNA-compatible `Motion`
    reading.
  - Decide fallback behavior when some (but not all) required sensors are missing.
  - Document the tradeoff between full XNA-shape compatibility (every field populated)
    and broader device support (partial data, clearly marked as such).
- **Acceptance criteria:**
  - `getIsSupportedProperty()` behavior is deterministic and documented.
  - Tests cover every missing-sensor combination (attitude missing, gravity missing,
    linear-acceleration missing, gyroscope missing, and combinations).
  - Docs explain why `Motion` is supported or unsupported on a given device.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-006 — Fix timestamp policy

- **Priority:** High
- **Area:** Motion Reading Semantics
- **Problem:** `MotionReading` fuses four independent Android sensor streams (attitude,
  gravity, linear acceleration, gyroscope), each with its own sample timestamp; the
  fused reading's own timestamp meaning (and `AttitudeReading`'s nested timestamp) must
  be defined precisely, not left as "whatever happened to be convenient when the code
  was written."
- **Required work:**
  - Define the timestamp meaning for `MotionReading` and nested `AttitudeReading`
    explicitly (e.g. "wall-clock time of the fused reading's publication" vs. "the
    attitude sample's own sensor timestamp").
  - Prefer platform event timestamps where they're compatible with the chosen
    `System::DateTimeOffset`-based representation; otherwise document the wall-clock
    substitution explicitly (mirroring the existing, already-documented rationale for
    `Detail::AndroidSensorSample::Timestamp` being wall-clock, not
    `ASensorEvent::timestamp`).
  - Ensure the fused reading's timestamp is internally consistent (not contradicted by
    its own nested `AttitudeReading`'s timestamp).
- **Acceptance criteria:**
  - Timestamp policy is documented for both `MotionReading` and `AttitudeReading`.
  - Tests verify monotonic timestamp progression across successive readings.
  - Fused readings never contain two different timestamps that claim to represent "now"
    inconsistently.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-007 — Prevent stale sample fusion

- **Priority:** High
- **Area:** Motion Fusion
- **Problem:** `AndroidMotionBackend::PublishReading()` currently publishes as soon as
  all four sources have delivered at least one sample ever (`hasAttitudeSample_` etc.,
  confirmed present) — it does not check whether those four most-recent samples were
  taken close together in time. A fast-changing gyroscope value could be fused with a
  stale gravity sample from much earlier.
- **Required work:**
  - Track a per-source last-sample timestamp.
  - Define a maximum acceptable age window across the four sources for them to be
    considered a valid fused reading.
  - Decide behavior when sources are outside that window (drop the stale one and wait,
    or publish anyway with a documented caveat) and implement that decision.
- **Acceptance criteria:**
  - Fused readings only combine samples that are fresh relative to each other, per the
    defined window.
  - Tests simulate stale gravity, stale acceleration, stale gyroscope, and stale
    attitude samples independently.
  - The chosen behavior (drop/wait vs. publish-with-caveat) is documented.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

### MOTION-008 — Apply `TimeBetweenUpdates` — CLOSED (2026-07-06, via `ANDROID-BRIDGE-002`)

- **Priority:** Critical
- **Area:** Motion Backend
- **Problem:** `Motion`'s update rate must honor `TimeBetweenUpdates`, including changes
  made while the sensor is running — the same class of gap already confirmed for
  `Compass`/`AndroidCompassBackend`'s Android event-rate handling (only applied at
  `Start()` time today), but `Motion` drives five separate
  `Detail::AndroidSensorBridge` instances simultaneously (rotation vector,
  game-rotation-vector fallback, gravity, linear acceleration, gyroscope), so all five
  must be kept in sync.
- **Resolution (2026-07-06, this stale cross-reference found and closed during a
  stabilization pass — the fix already existed from `ANDROID-BRIDGE-002` but this task
  itself was never marked closed):** `AndroidMotionBackend::SetSampleInterval()`
  (added by `ANDROID-BRIDGE-002`) forwards to all five owned bridges
  (`rotationVectorBridge_`, `gameRotationVectorBridge_`, `gravityBridge_`,
  `linearAccelerationBridge_`, `gyroscopeBridge_`) unconditionally —
  `AndroidSensorBridge::SetSampleInterval()` itself is a safe no-op on whichever ones,
  if any, weren't actually started by `Start()` (e.g. the game-rotation-vector fallback
  when the plain rotation vector is available), so no `usingGameRotationVector_` branch
  is needed at this call site. `Motion::Motion()` forwards its own
  `TimeBetweenUpdatesChanged` event to `backend_` exactly like `Compass` does.
  **Verification level, stated honestly:** the fake-backend test
  (`MotionTests.SetTimeBetweenUpdatesPropertyForwardsToBackend`) verifies
  `Motion`→`IMotionBackend` forwarding only — it exercises a single mock object, not the
  real `AndroidMotionBackend`'s 5-bridge fan-out. The 5-bridge forwarding itself is
  confirmed to *compile* (Android cross-compile + `llvm-nm` symbol check, done for
  `ANDROID-BRIDGE-002`) but has **no dedicated test** proving all five bridges
  individually receive the call — Android-only code, no host-testable seam exists for
  `AndroidSensorBridge` itself (same standing limitation as every other
  `Detail::Android*` class in this codebase). Not re-opened for this gap alone, since it
  matches the project's existing, accepted verification ceiling for this whole class of
  code — but noted here rather than silently claimed as fully tested.
- **Required work:**
  - Apply the requested interval to all five underlying Android sensor queues, or add
    software throttling at the fused-reading publish step. Done.
  - Ensure interval changes while running take effect across all five sources
    consistently. Done, to the extent stated above.
  - Add fake-backend tests. Done (`Motion`↔`IMotionBackend` level only).
- **Acceptance criteria:**
  - `Motion`'s published event rate follows the requested `TimeBetweenUpdates`. Done at
    the `SetSampleInterval()`-forwarding level described above.
  - Tests cover interval changes made during active streaming. Partially — see
    verification-level note above.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Motion.cpp` (edited, `ANDROID-BRIDGE-002`)
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp` (edited,
    `ANDROID-BRIDGE-002`)
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp` (edited,
    `ANDROID-BRIDGE-002`)
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp` (edited, `ANDROID-BRIDGE-002`)

### MOTION-009 — Add iOS Motion backend plan or implementation

- **Priority:** High
- **Area:** iOS Backend
- **Problem:** `Motion` is a natural fit for iOS's `CoreMotion` (`CMDeviceMotion`), but
  no iOS backend exists today (confirmed: only `Detail::AndroidMotionBackend` exists as
  a concrete `IMotionBackend`).
- **Required work:**
  - Plan (or implement, once an Apple toolchain is available) a `CMDeviceMotion`-backed
    `IMotionBackend`.
  - Map `attitude`, `gravity`, `userAcceleration`, and `rotationRate` to
    `AttitudeReading`/`MotionReading` fields, following the same unit-verification
    discipline as `MOTION-003`/`MOTION-004`.
  - Add an iOS manual test checklist entry.
- **Acceptance criteria:**
  - iOS behavior is documented, whichever choice is made.
  - The backend compiles behind the appropriate platform guard, or the unsupported path
    is deterministic if not implemented.
  - A manual checklist covers major orientations and movement patterns.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp`
  - iOS build/toolchain files (none currently present — confirm before assuming a
    location)
  - `docs/devices-*.md`

### MOTION-010 — Harden Motion callback lifetime further

- **Priority:** Critical
- **Area:** Lifecycle / Android
- **Problem:** Same class of concern as `COMPASS-008`, but for `Motion`/
  `AndroidMotionBackend`'s five independent bridge callbacks
  (`HandleAttitudeSample`/`HandleGravitySample`/`HandleLinearAccelerationSample`/
  `HandleGyroscopeSample`/`PublishReading`), which is more surface area than `Compass`'s
  two.
- **Progress (2026-07-06, `SENSORBASE-003`):** `MotionTests.DisposeFromWithinOwnCallbackDoesNotDeadlock`
  now exists (fake-backend seam), confirming `Motion`'s own reentrant-`Dispose()`
  handling is safe — same finding and same remaining scope as `COMPASS-008`'s identical
  progress note: the deeper risk (a handler *destroying* `Motion` while one of the five
  real `AndroidMotionBackend` callbacks is still on the call stack, tearing down
  `backend_` mid-call) remains open and unverified, Android-only, not reproducible via
  the fake backend.
- **Required work:**
  - Re-confirm `Motion`/`AndroidMotionBackend`'s object lifetime story under
    `Stop()`/`Dispose()`-from-within-`CurrentValueChanged` for each of the five
    callback paths. Partially done (2026-07-06) — see Progress note above.
  - Add tests for `Stop()`/`Dispose()`/destroy-from-within-callback using a fake
    backend, to the extent this is a supported scenario (document if not). `Dispose()`
    done; full destroy-from-within-callback still open (same limitation as `COMPASS-008`).
  - Audit all five callbacks' shared-state mutations
    (`attitude_`/`gravity_`/`deviceAcceleration_`/`deviceRotationRate_`, all
    mutex-guarded per existing code) for races introduced by concurrent delivery from
    multiple bridge worker threads.
- **Acceptance criteria:**
  - `devices-asan`/`devices-tsan` runs are clean for documented-supported scenarios.
  - No callback path touches a destroyed `Motion`/`AndroidMotionBackend`.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

---

## 10. Android sensor bridge tasks

### ANDROID-BRIDGE-001 — Verify per-sensor sample value counts

- **Priority:** High
- **Area:** Android Bridge
- **Problem:** `Detail::AndroidSensorSample::ValueCount` must reflect the real
  per-sensor-type value count (rotation vector up to 5, magnetic field/gravity/linear
  acceleration/gyroscope 3, etc.) — verify the current bridge implementation actually
  sets this correctly per sensor type, rather than a single generic constant.
- **Required work:**
  - Confirm `ValueCount` is set according to actual sensor type at every callsite in
    `Detail::AndroidSensorBridge.cpp`.
  - Ensure backends validate they've received enough values before reading indices
    (defensive bounds-checking against a short/malformed event).
  - Add tests for each consumed sensor type's expected value count.
- **Acceptance criteria:**
  - Rotation vector, magnetic field, gravity, linear acceleration, and gyroscope
    samples all expose the correct count.
  - Backends handle an incomplete sample safely (no out-of-bounds read).
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp`

### ANDROID-BRIDGE-002 — Support update-interval changes while running — CLOSED (2026-07-06)

- **Priority:** Critical
- **Area:** Android Bridge
- **Problem:** Confirmed (Section 1): `Detail::AndroidSensorBridge::Start()` converts
  `timeBetweenUpdates` to `ASensorEventQueue_setEventRate()`'s microsecond parameter
  only once, at `Start()` time — there is no code path to change an already-running
  bridge's sample rate today. This is the root cause underlying `ACCEL-005`,
  `GYRO-004`, `COMPASS`'s and `MOTION-008`'s `TimeBetweenUpdates` gaps for the Android
  side specifically.
- **Resolution (2026-07-06):** added `AndroidSensorBridge::SetSampleInterval(TimeSpan)` —
  stores the new interval (guarded by the existing `stateMutex_`) and sets an atomic
  `rateChangeRequested_` flag, waking the looper (`ALooper_wake()`, same technique
  `Stop()` already used). `Run()`'s own poll loop — the only code that ever touches
  `queue_`/`sensor_` — checks that flag once per iteration and calls
  `ASensorEventQueue_setEventRate()` again on the live queue from its own thread, with
  the same non-fatal-rejection handling the initial `Start()`-time call already had. A
  safe no-op if the bridge isn't currently started (nothing live to update; the next
  `Start()` call takes its own explicit parameter anyway). Added
  `SetSampleInterval()` to `ICompassBackend`/`IMotionBackend` (pure virtual — updated
  both interface implementers, `AndroidCompassBackend`/`AndroidMotionBackend`, which
  forward to every bridge they own — 2 for Compass, 5 for Motion, calling it
  unconditionally on all of them since a never-started bridge's own `SetSampleInterval()`
  already no-ops safely) and to the two test-only fakes in `CompassTests.cpp`/
  `MotionTests.cpp` (`FakeCompassBackend`/`FakeMotionBackend`, which were the only other
  implementers of these interfaces in the whole codebase, confirmed by grep). Wired
  `Compass`/`Motion`'s own constructors to subscribe to the inherited (protected)
  `SensorBase<T>::TimeBetweenUpdatesChanged` event and forward the new value to
  `backend_` (read fresh at invocation time, so it still reaches a backend swapped in
  later via `SetBackendForTesting()`) — mirrors `ACCEL-005`/`GYRO-004`'s SDL-side fix in
  spirit (both now honor a running `TimeBetweenUpdates` change without `Stop()`/`Start()`),
  though the actual mechanism differs (real hardware rate change here vs. software
  dispatch throttling there, since the NDK exposes a real per-sensor rate control SDL3
  does not). Added 6 new tests: 2 `AndroidSensorBridgeTests.cpp` (confirm
  `SetSampleInterval()` is a safe no-op on this non-Android host, matching every other
  method's existing convention), 2 `CompassTests.cpp` + 2 `MotionTests.cpp` (confirm the
  `TimeBetweenUpdatesChanged` wiring forwards to the fake backend on a real change, and
  does *not* forward when the value is unchanged, matching
  `setTimeBetweenUpdatesProperty()`'s own "only raise on actual change" contract).
  Verified: 296/296 tests (up from 290) on plain `cmake-build-debug` and all three
  sanitizer presets — 0 ASan, TSan/UBSan findings unchanged from previously known. Also
  cross-compiled the `CNA` library target for Android (`arm64-v8a`, NDK r30, API 24) and
  confirmed via `llvm-nm` that `AndroidSensorBridge::SetSampleInterval()`,
  `AndroidCompassBackend::SetSampleInterval()`, and
  `AndroidMotionBackend::SetSampleInterval()` are all present as real (not just
  `#ifdef`-stubbed) symbols in the cross-compiled object files — the real
  `#ifdef __ANDROID__` code path was never exercised at runtime (no Android
  device/emulator run this session), only confirmed to compile.
- **Required work:**
  - Add an API to `Detail::AndroidSensorBridge` (e.g. `SetSampleInterval(TimeSpan)`)
    that calls `ASensorEventQueue_setEventRate()` again on the live queue, from the
    correct thread. Done.
  - Wire `Compass`/`Motion`'s `TimeBetweenUpdates` setter through to every active
    underlying bridge. Done.
  - Add tests, using whatever host-testable seam is available (the real NDK path can't
    run in this development container — see Section 14). Done — 6 new tests, plus a
    non-runtime Android cross-compile + `llvm-nm` symbol check.
- **Acceptance criteria:**
  - Active Android-backed sensors update their sampling delay without requiring
    `Stop()`/`Start()` or object recreation. Done, to the extent host-verifiable — real
    hardware/emulator behavior still unverified (no Android device in this session).
  - Tests cover the new delay-update code path to the extent host-testable. Done.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp` (edited)
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp` (edited)
  - `include/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp` (edited)
  - `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp` (edited)
  - `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp` (edited)
  - `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp` (edited)
  - `include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp` (edited)
  - `include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp` (edited)
  - `src/Microsoft/Devices/Sensors/Compass.cpp` (edited)
  - `src/Microsoft/Devices/Sensors/Motion.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp` (edited)

### ANDROID-BRIDGE-003 — Improve Start/Stop lifecycle guarantees further

- **Priority:** High
- **Area:** Android Bridge
- **Problem:** `Detail::AndroidSensorBridge::Start()`/`Stop()` have already been through
  several hardening passes in this codebase's history (a startup handshake, a
  mutex-guarded state machine, shared-ownership `Impl`, a stale-looper-reset RAII
  guard) — this task re-audits the *current* state for any remaining gap, rather than
  assuming those passes closed every case. One specific, already-documented remaining
  gap: two or more distinct external (non-worker) threads calling `Stop()`
  concurrently on the same bridge is still unserialized and can race on `join()`.
- **Required work:**
  - Add tests for: `Start()` failure (queue creation/enable failure), `Stop()` before
    `Start()`, repeated `Stop()`, `Stop()` called reentrantly from the worker's own
    callback, and destructor cleanup.
  - Decide whether the still-open "two concurrent external `Stop()` callers" gap should
    finally be closed (it was previously left as a documented, deliberate boundary to
    avoid a different deadlock risk) or remains an accepted limitation — re-examine
    with fresh eyes rather than re-stating the old conclusion unchecked.
  - Prefer safe, deterministic cleanup over detach-and-abandon wherever a safe
    alternative can be found without reintroducing the previously-identified deadlock
    risk.
- **Acceptance criteria:**
  - No leaked event queues or worker threads across repeated Start/Stop cycles.
  - `Stop()` behavior is deterministic for every documented-supported calling pattern.
  - Tests cover every lifecycle edge case listed above.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp`

### ANDROID-BRIDGE-004 — Add Android API-level documentation

- **Priority:** Medium
- **Area:** Android Platform
- **Problem:** Android sensor and vibrator behavior depends on API level and runtime
  permissions (e.g. Android 12+/API 31+ restricting high-frequency sensor sampling
  without the `HIGH_SAMPLING_RATE_SENSORS` permission — already noted in this
  codebase's own doc comments in one place, but not centralized).
- **Required work:**
  - Consolidate documentation of minimum Android API level, sampling-rate
    restrictions, and vibrator permission requirements into `docs/devices-android.md`
    (already exists — extend it, don't create a duplicate).
  - Document which physical/emulated devices have actually been tested.
- **Acceptance criteria:**
  - `docs/devices-android.md` lists API levels, permissions, and known limitations for
    both sensors and vibration.
  - The demo app's Android manifest matches the documented required permissions.
- **Suggested files to inspect or edit:**
  - `docs/devices-android.md`
  - `examples/demo_devices/android/`

---

## 11. SDL sensor subsystem tasks

### SDL-SENSOR-001 — Verify SDL3 sensor units and axes

- **Priority:** Critical
- **Area:** SDL Backend
- **Problem:** CNA cannot assume SDL3's reported units and axis conventions without
  reading SDL3's own backend source per platform — this underlies `ACCEL-003`'s and
  `GYRO-002`'s unit-verification tasks, but is listed here as its own cross-cutting task
  because `Detail::SdlSensorSubsystem<TSensor>` is shared plumbing, not
  per-sensor-class code.
- **Required work:**
  - Check SDL3 documentation and `third_party/SDL`'s actual per-platform sensor backend
    source for both accelerometer and gyroscope units.
  - Confirm axis conventions match what `Detail::AndroidSensorOrientation` and the
    per-class conversion code assume.
  - Add code comments and tests recording exactly what was checked and where.
- **Acceptance criteria:**
  - SDL conversion code is backed by a specific documented source (SDL3 docs section,
    or a specific file/line in `third_party/SDL`), not an unstated assumption.
  - Tests cover the expected raw-to-XNA mapping.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `third_party/SDL/src/sensor/` (read-only research — vendored, do not edit)

### SDL-SENSOR-002 — Implement update-rate throttling — CLOSED (2026-07-06)

- **Priority:** Critical
- **Area:** SDL Backend
- **Problem:** Confirmed (Section 1): SDL sensor callbacks currently ignore
  `TimeBetweenUpdates` entirely — this is the SDL-side counterpart to
  `ANDROID-BRIDGE-002`.
- **Resolution (2026-07-06):** see `ACCEL-005`/`GYRO-004`/`SENSORBASE-001` for the full
  implementation detail. Placed the throttle in `SensorBase<T>` itself
  (`ShouldAcceptUpdateAt()`), not in `Detail::SdlSensorSubsystem<TSensor>` as originally
  suggested — `SdlSensorSubsystem<TSensor>` is per-*sensor-type* shared state
  (`sensor_`/`instanceCount_`/`startedInstances_`/...), while the throttle needs to be
  genuinely per-*instance*; keeping it on `SensorBase<T>` (each instance's own base
  subobject) makes the per-instance scoping structural rather than something a future
  reader has to re-verify by inspection. Confirmed independent throttling via
  `SensorBaseTests.ShouldAcceptUpdateAtThrottlesIndependentlyPerInstance` (two
  `TestSensorBase` instances with different `TimeBetweenUpdates` values, one accepts an
  update the other still rejects at the same synthetic timestamp).
- **Required work:**
  - Use SDL3's own sensor update-rate APIs if they exist for the sensor types in use;
    otherwise add software throttling in
    `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()` (or equivalent
    dispatch point). Done — SDL3 has no such API for these sensor types; throttling
    added in `SensorBase<T>`, called from each class's `ProcessSensorUpdateEvent()`
    (not `DispatchToInstances()` itself, which is shared dispatch-batch bookkeeping, not
    a natural per-instance-decision point).
  - Ensure throttling is scoped per sensor *instance*, not global. Done, verified by
    test above.
- **Acceptance criteria:**
  - `Accelerometer` and `Gyroscope` event rates follow their own instance's
    `TimeBetweenUpdates`. Done.
  - Tests use fake/injected timestamps to avoid real-time-based test flakiness. Done.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorBase.hpp` (edited — not
    `SdlSensorSubsystem.hpp`, see Resolution above)
  - `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` (edited)

### SDL-SENSOR-003 — Strengthen SDL event lifetime tests

- **Priority:** High
- **Area:** Lifecycle / Tests
- **Problem:** SDL event watches and callbacks are a classic source of
  use-after-free/reentrancy bugs; `SensorSubsystemOwnershipTests.cpp` already exists and
  covers some of this — this task confirms it covers the *current* dispatch
  implementation fully, rather than assuming existing coverage is complete.
- **Required work:**
  - Add tests for `Stop()`/`Dispose()`/destruction occurring during event dispatch.
  - Confirm no sensor object is touched after a user callback returns unless lifetime
    is provably still valid.
  - Run under `devices-asan`/`devices-tsan`.
- **Acceptance criteria:**
  - Lifecycle tests pass under both sanitizers.
  - Any remaining known-unsupported case is explicitly documented rather than silently
    left as an untested gap.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`
  - `tests/Microsoft/Devices/Sensors/SensorSubsystemOwnershipTests.cpp`

---

## 12. Reading structs and event-args tasks

### READINGS-001 — Verify all reading struct fields

- **Priority:** Critical
- **Area:** Reading Structs
- **Problem:** Every reading struct must expose the correct fields, types, and units —
  this is the concrete, per-field companion to `DEV-API-004`'s behavioral audit.
- **Required work:**
  - Audit `AccelerometerReading`, `GyroscopeReading`, `CompassReading`, `MotionReading`,
    and `AttitudeReading` field-by-field.
  - Verify field names, types, units (cross-reference `ACCEL-003`/`GYRO-002`/
    `MOTION-003`/`MOTION-004`'s unit findings), and mutability (`DEF_MEMBER`-style
    getter/setter pairs, confirmed pattern in `CompassReading.hpp`).
  - Add tests for construction and every property getter/setter.
- **Acceptance criteria:**
  - `DEV-API-001`'s matrix includes every reading struct's fields.
  - Tests cover every property on every reading struct.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/AccelerometerReading.hpp`
  - `include/Microsoft/Devices/Sensors/GyroscopeReading.hpp`
  - `include/Microsoft/Devices/Sensors/CompassReading.hpp`
  - `include/Microsoft/Devices/Sensors/MotionReading.hpp`
  - `include/Microsoft/Devices/Sensors/AttitudeReading.hpp`
  - `src/Microsoft/Devices/Sensors/AccelerometerReading.cpp`
  - `src/Microsoft/Devices/Sensors/GyroscopeReading.cpp`
  - `src/Microsoft/Devices/Sensors/CompassReading.cpp`
  - `src/Microsoft/Devices/Sensors/MotionReading.cpp`
  - `src/Microsoft/Devices/Sensors/AttitudeReading.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionReadingTests.cpp`
  - `tests/Microsoft/Devices/Sensors/AttitudeReadingTests.cpp`

### READINGS-002 — Verify event-args types — CLOSED (2026-07-06)

- **Priority:** High
- **Area:** Events
- **Problem:** Event-args classes must carry the correct reading type and be shaped
  correctly for their consumers. `DEV-API-001` (2026-07-06) flagged two wrong-visibility
  findings for this task to resolve: `AccelerometerReadingEventArgs`'s and
  `SensorReadingEventArgs<T>`'s setters are fully `public`, unlike every reading
  struct's `private`+`friend` convention (Task P3-2) — unclear, without an authoritative
  check, whether that's a real bug or the real API's own shape.
- **Resolution (2026-07-06):** fetched the archived MSDN "previous-versions" pages
  directly (via the classic `msdn.microsoft.com/en-us/library/<fully.qualified.member>
  (v=VS.105)` URL form, which 301-redirects to the current `learn.microsoft.com`
  archive page even without knowing its numeric ID in advance) rather than assuming
  either way:
  - `AccelerometerReadingEventArgs.X`/`Y`/`Z`: `public double X/Y/Z { get; }` (MSDN
    `ff707568`/`ff707712`/`ff708055`) — **no setter at all**, public or otherwise.
  - `AccelerometerReadingEventArgs.Timestamp`: `public DateTimeOffset Timestamp { get;
    private set; }` (MSDN `ff707430`) — `private set`, not `internal set`.
  - `SensorReadingEventArgs<T>.SensorReading`: `public T SensorReading { get; set; }`
    (MSDN `hh203225`) — genuinely fully public, both directions.

  This confirmed one real bug and one non-bug:
  - **`AccelerometerReadingEventArgs`'s `setXProperty()`/`setYProperty()`/
    `setZProperty()`/`setTimestampProperty()` were a genuine, confirmed Extra-unmarked
    finding** — CNA-only public setters with no real counterpart. Removed all four
    entirely (not tagged `NOXNA`, since the real API has no setter of *any* visibility
    to preserve access to). Confirmed by grep they were unused dead code —
    `Accelerometer::DispatchSensorReading()` only ever constructs this type via its
    4-argument constructor, never calls a setter. `Timestamp`'s real `private set`
    needs no dedicated C++ method: the constructor already assigns the private field
    directly, which is the literal equivalent of "settable only from within this
    class's own code." Updated `docs/devices-api-coverage.md`'s "Cross-cutting members
    — reading structs" table and "Flagged findings" section with the resolution and
    citations. Removed the 4 now-dead `SetX`/`SetY`/`SetZ`/`SetTimestamp` tests from
    `AccelerometerReadingEventArgsTests.cpp` (down to 11 tests from 15) — construction
    is already covered by `DefaultConstructorZeroValues`/
    `ParameterizedConstructorStoresValues`.
  - **`SensorReadingEventArgs<T>::setSensorReadingProperty()` needed no change** — CNA's
    existing fully-public setter (both copy and move overloads) already matches the
    real API exactly. This class is the one outlier in the *opposite* direction from
    `AccelerometerReadingEventArgs` — genuinely publicly mutable in the real API, not
    `internal set` like the reading structs.
  - `CalibrationEventArgs` was not touched — it is a genuinely empty marker class
    (already confirmed correct, `plan_devices_phase3.md` Task P3-12, MSDN `hh220788`),
    not part of this finding.

  Verified: 292/292 tests (down from 296 — 4 dead tests removed, none added) on plain
  `cmake-build-debug` and ASan/UBSan presets (0 ASan; UBSan's 3 findings unchanged,
  all pre-existing `Vector3`/`Matrix::GetHashCode()`, none in this class). TSan not
  re-run for this pass — no concurrency-relevant code touched (pure removal of unused,
  single-threaded setter methods).
- **Required work:**
  - Audit `SensorReadingEventArgs<T>`, `AccelerometerReadingEventArgs`, and
    `CalibrationEventArgs`. Done.
  - Verify property names and inheritance against expected XNA/WP7 shape. Done, via
    direct archived-MSDN-page fetches, not assumption.
  - Add or extend tests. Done — removed 4 dead tests exercising now-removed methods;
    existing construction/equality/hash/`ToString()`/`GetTypeName()` tests already
    cover the real, remaining API surface.
- **Acceptance criteria:**
  - Event-args classes match the intended XNA/WP7 API. Done for all three.
  - Tests cover construction, property retrieval, and actual use in event dispatch
    (not just standalone construction). Already true before this task (`Accelerometer`'s
    own tests exercise `ReadingChanged` dispatch); not extended further by this task.
- **Suggested files to inspect or edit:**
  - `include/Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp` (inspected, no
    change needed)
  - `include/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp` (edited)
  - `include/Microsoft/Devices/Sensors/CalibrationEventArgs.hpp` (inspected, no change
    needed)
  - `src/Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/AccelerometerReadingEventArgsTests.cpp` (edited)
  - `tests/Microsoft/Devices/Sensors/CalibrationEventArgsTests.cpp` (inspected, no
    change needed)
  - `docs/devices-api-coverage.md` (edited)

### READINGS-003 — Verify timestamp source consistently

- **Priority:** High
- **Area:** Reading Semantics
- **Problem:** Some readings use wall-clock timestamps
  (`System::DateTimeOffset::getUtcNowProperty()`, confirmed used in
  `Detail::AndroidSensorBridge.cpp`) rather than platform sensor timestamps
  (`ASensorEvent::timestamp`, a monotonic boot-time value) — this is already a
  documented, deliberate choice in one place; this task confirms the policy is applied
  consistently across every sensor class, not just where it happened to be documented.
- **Required work:**
  - Write down one timestamp policy that applies to all four sensor classes'
    readings.
  - Use monotonic/platform timestamps only where genuinely compatible with
    `System::DateTimeOffset`'s semantics; otherwise document wall-clock use explicitly,
    consistent with the existing rationale already present for
    `Detail::AndroidSensorSample::Timestamp`.
  - Ensure timestamps are stable and testable (i.e. tests can inject a fixed clock
    rather than relying on real elapsed time).
- **Acceptance criteria:**
  - Timestamp policy is documented once and referenced from every sensor class's own
    docs.
  - Tests cover monotonic progression and correct propagation from backend sample to
    public event.
- **Suggested files to inspect or edit:**
  - `src/Microsoft/Devices/Sensors/Accelerometer.cpp`
  - `src/Microsoft/Devices/Sensors/Gyroscope.cpp`
  - `src/Microsoft/Devices/Sensors/Compass.cpp`
  - `src/Microsoft/Devices/Sensors/Motion.cpp`
  - `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`
  - `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
  - `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`
  - `tests/Microsoft/Devices/Sensors/CompassTests.cpp`
  - `tests/Microsoft/Devices/Sensors/MotionTests.cpp`

---

## 13. Demo and manual QA tasks

### DEMO-001 — Make `demo_devices` show all sensor states

- **Priority:** Medium
- **Area:** Demo
- **Problem:** Manual QA needs visible support status, started/stopped state, data
  validity, latest reading, and current `TimeBetweenUpdates` for every sensor, plus
  vibration controls — verify what `DevicesDemo.cpp` currently shows before assuming it
  is missing something specific.
- **Required work:**
  - Update the demo's UI/logging to show every sensor's support status, started/stopped
    state, data validity, and latest reading.
  - Add interactive controls for `TimeBetweenUpdates` (ties directly into verifying
    `SENSORBASE-001`/`ACCEL-005`/`GYRO-004`/`MOTION-008` on real hardware).
  - Add vibration controls covering `Start(TimeSpan)`, the `NOXNA` intensity/left-right
    variants, and `getIsSupportedProperty()`/`getDeviceNameProperty()` display.
- **Acceptance criteria:**
  - The demo can be used standalone for manual Android/iOS/desktop testing without
    needing to read source code alongside it.
  - The demo logs enough data to write a useful bug report from its output alone.
- **Suggested files to inspect or edit:**
  - `examples/demo_devices/src/DevicesDemo.cpp`
  - `examples/demo_devices/src/DevicesDemo.hpp`
  - `examples/demo_devices/src/Main.cpp`
  - `examples/demo_devices/android/`

### DEMO-002 — Add a hardware QA report template

- **Priority:** Medium
- **Area:** QA
- **Problem:** Sensor and vibration correctness requires physical-device validation
  that only a human can perform; `docs/devices-hardware-checklist.md` already exists as
  a checklist of *what* to verify, but there is no standard template for *recording*
  results in a reusable, comparable format across devices/sessions.
- **Required work:**
  - Create a markdown report template (as a new file,
    `docs/devices_sensor_hardware_qa_template.md`, distinct from the existing
    checklist — cross-link the two rather than merging them, since one is "what to
    check" and the other is "how to record a specific run's results").
  - Include device model, OS version, orientation, expected values, observed values,
    backend, and commit hash fields.
  - Include sections for accelerometer, gyroscope, compass, motion, and vibration.
- **Acceptance criteria:**
  - The template exists under `docs/`.
  - `plan_devices.md` links to it from every hardware-verification task above.
  - Manual tests can be recorded consistently, session over session.
- **Suggested files to inspect or edit:**
  - `docs/devices_sensor_hardware_qa_template.md` (new)
  - `docs/devices-hardware-checklist.md` (cross-link, do not duplicate)
  - `plan_devices.md`

---

## 14. Final verification tasks

### VERIFY-001 — Run the full Devices/Sensors test suite

- **Priority:** Critical
- **Area:** Verification
- **Problem:** No task above is considered complete until the tests for it were
  actually built and actually run — this plan must never claim a passing result that
  wasn't observed directly.
- **Required work:**
  - Build and run every test file under `tests/Microsoft/Devices/` and
    `tests/Microsoft/Devices/Sensors/` (including `Detail/`).
  - Record the exact commands used and their exact results (pass/fail counts, not just
    "it worked").
  - Fix failures, or create a new follow-up task with the failure's exact log excerpt
    if the fix is out of scope for the change being made.
- **Acceptance criteria:**
  - `CnaTests` passes with the Devices/Sensors filter from `DEV-BUILD-002`.
  - Results are recorded (in this plan, `NEXT.md`, or a linked verification log) with
    the exact command and exact pass/fail/skip counts observed.
- **Suggested commands** (verify these preset names still exist in `CMakePresets.json`
  before relying on them — confirmed present as of 2026-07-05):
  ```bash
  cmake --preset devices-ubsan
  cmake --build --preset devices-ubsan --target CnaTests
  ./cmake-build-devices-ubsan/CnaTests --gtest_filter='*Devices*:*Sensors*:*Vibrate*:*Accelerometer*:*Gyroscope*:*Compass*:*Motion*:*Android*:*ScopeExit*:*SensorBase*:*SensorFailed*:*SensorSubsystemOwnership*'
  ```

### VERIFY-002 — Run sanitizer verification

- **Priority:** High
- **Area:** Verification
- **Problem:** Sensor callbacks and backend worker threads specifically need sanitizer
  coverage — this is the area of this codebase where prior real bugs (use-after-free,
  thread-termination races, undefined-behavior integer casts) have actually been found
  before, not a generic precaution.
- **Required work:**
  - Run `devices-asan`, `devices-tsan`, and `devices-ubsan` configurations.
  - Prioritize lifecycle/callback tests (`SENSORBASE-003`, `COMPASS-008`,
    `MOTION-010`, `ANDROID-BRIDGE-003`, `SDL-SENSOR-003`) when interpreting results.
  - Record any failure as a new follow-up task with the sanitizer's exact report
    excerpt, not just "sanitizer found something."
- **Acceptance criteria:**
  - Sanitizer logs are clean, or every remaining finding is a knowingly-tracked,
    already-documented, out-of-scope issue (e.g. a `sharp-runtime` race, not a
    `Microsoft::Devices` one) — verify that classification is still accurate rather than
    assuming an old classification still applies.
  - Callback/destruction tests are included in every sanitizer run.
- **Suggested files to inspect or edit:**
  - `CMakePresets.json`
  - `tests/Microsoft/Devices/`
  - `tests/Microsoft/Devices/Sensors/`

### VERIFY-003 — Run a strict XNA API compile check

- **Priority:** High
- **Area:** Verification
- **Problem:** CNA extensions must not leak into strict XNA API surface — this closes
  the loop on `DEV-API-002`'s enforcement task with an actual executable check, not just
  a documented policy.
- **Required work:**
  - Add or run a compile-only test/check for a "strict XNA surface" mode, if the
    project's build system can express one; if it cannot yet, this task includes adding
    that capability (even minimally, e.g. a dedicated test translation unit that must
    fail to compile if it references a `NOXNA` member while some macro is defined).
  - Ensure `NOXNA` APIs are unavailable (or at least clearly flagged) when strict mode
    is enabled.
  - Ensure every genuine XNA API remains available and unaffected by strict mode.
- **Acceptance criteria:**
  - The strict-mode check passes.
  - A deliberately-introduced "leaked extension" (e.g. temporarily removing a `NOXNA`
    marker) is caught by this check, proving it actually works rather than trivially
    passing.
- **Suggested files to inspect or edit:**
  - `tests/Microsoft/Devices/`
  - `tests/Microsoft/Devices/Sensors/`
  - `CMakeLists.txt` / relevant build config files

---

## 15. Definition of done for this plan

The Devices/Sensors work driven by this plan is not done until:

- The public API matrix (`DEV-API-001`) is complete and covers every class in Section 0.
- `VibrateController`'s strict XNA behavior is separated from CNA haptic extensions
  (`VIB-002`), and the class is referred to correctly as `VibrateController` everywhere
  (`VIB-001`).
- Android phone vibration works through a proper phone-vibrator backend, or is
  explicitly and deliberately re-confirmed to be adequately served by SDL3's existing
  Android haptic backend (`VIB-003`) — not left as an unexamined assumption either way.
- `TimeBetweenUpdates` actually works for every sensor (`Accelerometer`, `Gyroscope`,
  `Compass`, `Motion`) and can be changed while the sensor is running
  (`SENSORBASE-001`, `ACCEL-005`, `GYRO-004`, `ANDROID-BRIDGE-002`, `MOTION-008`,
  `SDL-SENSOR-002`) — confirmed fixed, not just planned, given Section 1's concrete
  finding that this is currently broken for every SDL-backed sensor.
- Accelerometer and Gyroscope units and axes are verified against real hardware
  (`ACCEL-003`, `ACCEL-004`, `GYRO-002`, `GYRO-003`), not merely unit-tested against
  the current implementation's own output.
- Compass heading math, accuracy mapping, calibration policy, and true-heading policy
  are verified (`COMPASS-002` through `COMPASS-006`).
- Motion's quaternion/attitude mapping, gravity/acceleration/rotation-rate units,
  timestamp policy, and stale-sample-fusion behavior are verified (`MOTION-002` through
  `MOTION-007`).
- Android callback lifetime issues are fixed or explicitly documented with tests, for
  both `Compass` (`COMPASS-008`) and `Motion` (`MOTION-010`), and the shared bridge's
  own remaining known gap (`ANDROID-BRIDGE-003`) is either closed or re-confirmed as a
  deliberate, documented boundary.
- Desktop, Android, and iOS support policies are documented for every sensor and for
  vibration (`ACCEL-007`, `COMPASS-005`, `COMPASS-007`, `MOTION-005`, `MOTION-009`,
  `VIB-004`).
- CI runs hardware-free unit tests for this entire area (`DEV-BUILD-003`).
- A manual hardware QA checklist and report template exist, and have at least one real
  Android device result recorded against them (`ACCEL-004`, `GYRO-003`, `COMPASS-004`,
  `MOTION-002`, `DEMO-002`).
- Strict XNA builds do not expose CNA-only `NOXNA` APIs, verified by an actual
  executable check, not just documentation (`DEV-API-002`, `VERIFY-003`).
- All relevant tests pass in normal and sanitizer configurations where supported
  (`VERIFY-001`, `VERIFY-002`), with results actually observed and recorded, not
  assumed.

This plan is not implemented as of 2026-07-05. No task above has been started.
