# NEXT.md — CNA Project Handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3
with a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It preserves
XNA-style public APIs (`Microsoft::Xna::Framework`, `Microsoft::Devices`) while using
modern C++ internally. Targets desktop Linux/Windows/macOS and Android; iOS is planned
but has no toolchain in this environment. Branch: `feature/devices`.

**Two parallel efforts on this branch:**
- **`Microsoft::Devices`/`Microsoft::Devices::Sensors`/`VibrateController`** — bringing
  real XNA 4.0 / Windows Phone 7 APIs to verified compatibility. Tracked in
  `plan_devices.md` (74+ tasks). **Effectively done** — every task is CLOSED except
  one new follow-up (`MOTION-012`, see Section 4).
- **`CNA::Devices`** — a brand-new, non-XNA namespace for CNA-only device capabilities
  SDL3 exposes that WP7 never had (battery, clipboard, native dialogs, camera, ...).
  Tracked in `plan_cna_devices.md`. **All 12 tasks CLOSED.** Analysis in
  `noxna_devices.md`, including a "Round 2" survey (Section 8) of further SDL3
  capabilities, one of which (`MessageBox`) is now implemented.

**Important architectural decisions:**
- Public API names/signatures match XNA 4.0 (or, for `Microsoft::Devices`, the archived
  WP7 SDK docs) exactly; C# properties become `getXProperty()`/`setXProperty()`.
- Non-XNA extensions inside `Microsoft::Devices`/`Sensors` are tagged `NOXNA` on the
  public declaration, compile-time enforced (see Section 6, `VERIFY-003`).
- `CNA::Devices` is a **separate, sibling namespace**, not part of `Microsoft::Devices`.
  Gated behind its own `CNA_DEVICES` CMake option (default `OFF`). Its members do
  **not** carry the `NOXNA` tag — the namespace itself signals "not XNA."
- Any `CNA::Devices` (or `Microsoft::Devices`) class whose real backend has a side
  effect an automated test cannot safely trigger (a real dialog, a real tray icon, a
  real camera device) gets a `Detail::I<X>Backend` test-injection seam **from the
  first line of implementation** — this project has hit real incidents from
  retrofitting this after the fact (see Section 3).

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly under `EASYGL` (`cmake-build-debug`),
both with `CNA_DEVICES=ON` (this session's default working configuration) and
previously confirmed with `CNA_DEVICES=OFF` (not re-verified again this session after
the latest additions — see Section 5).

**Tests:** full suite is **3388 tests, 3386 passed, 2 expected `GTEST_SKIP()`s** (no
accelerometer/gyroscope hardware in this container) — zero regressions across this
entire session's work. `CNA::Devices`-only filter: **48 tests across 10 suites**
(`PowerInfoTests`, `LocaleTests`, `ClipboardTests`, `UrlLauncherTests`,
`SystemInfoTests`, `DisplayInfoTests`, `FileDialogTests`, `SystemTrayTests`,
`MessageBoxTests`, `CameraTests`), all passing.

**Sanitizers:** `devices-ubsan` clean on everything touched this session. `devices-asan`
clean on everything **except** a newly-surfaced, pre-existing leak in the
EasyGL/OpenGL graphics backend (see Section 4) — confirmed unrelated to any of this
session's own code. `devices-tsan` was run only for `SDL-SENSOR-004`'s own repro
suites (clean, 0 races, see Section 3); it has **not** been re-run against
`AndroidCompassMathTests`/`CameraTests` specifically this session.

**`CNA::Devices` — implemented and working:** `PowerInfo`, `Locale`, `Clipboard`,
`UrlLauncher`, `SystemInfo`, `DisplayInfo`, `FileDialog`, `SystemTray`, `MessageBox`,
`Camera` (narrow first-implementation scope — single device, RGBA-only, no
event-queue integration; see `docs/cna-devices-camera-design.md` for what's
deliberately deferred).

**`Microsoft::Devices::Sensors` — working, changed this session:** `Accelerometer`/
`Gyroscope`'s Android landscape-remap is now explicitly documented as a CNA-only
deviation from real WP7 behavior, with a runtime opt-out
(`Detail::SetAndroidLandscapeRemapEnabled(false)`). `Compass` on Android now switches
heading-computation axis based on device tilt (upright vs. flat), matching a
documented real-WP7 behavior that had no implementation before this session.

**Does not work / not implemented (by design, not bugs):** iOS backend for anything
in this scope. `Camera`'s `CameraState::Lost` is never reached (no event-queue
integration in this first pass). `Motion`'s `Gravity`/`DeviceAcceleration`/
`DeviceRotationRate` still receive no landscape remap at all (tracked as
`MOTION-012`, not yet started). No physical Android/iOS hardware or real camera
device has ever been used to verify anything in this scope, in any session.

**Working tree:** clean, all work through this session pushed to `feature/devices`.

---

## 3. Recent changes (this session, 2026-07-07)

Most recent first. Full detail (citations, exact test counts, sanitizer output) is in
`plan_devices.md`/`plan_cna_devices.md`'s per-task resolution notes and git commit
messages — this section is a factual summary.

- **`DEVICES-CNA-012` (closed):** implemented `CNA::Devices::Camera` — single camera
  device, synchronous permission polling, RGBA8-only. New:
  `include/CNA/Devices/{Camera,CameraState,CameraPosition,CameraDeviceInfo}.hpp`,
  `Detail/{ICameraBackend,SdlCameraBackend}.hpp`, matching `.cpp` files,
  `tests/CNA/Devices/CameraTests.cpp` (8 tests). Handled two real SDL3 behaviors found
  during implementation: `SDL_GetCameraSupportedFormats()` legally returns an empty
  list on Emscripten (handled, not treated as "unsupported"); `SDL_Surface` rows
  aren't guaranteed tightly packed (compacted before upload). Added
  `getFrameWidthProperty()`/`getFrameHeightProperty()` (not in the original design
  sketch) since `Texture2D` has no resize API. **Found and correctly attributed** a
  pre-existing, unrelated ASan leak in the EasyGL/OpenGL graphics backend — see
  Section 4.
- **`COMPASS-009` (closed):** implemented Android `Compass`'s device-tilt-dependent
  heading-axis switch (`Detail::IsDeviceInUprightCompassMode()`,
  `ConvertRotationVectorToUprightMagneticHeadingDegrees()`,
  `ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode()` in
  `AndroidCompassMath.hpp`), derived fresh from Android's own documented
  `remapCoordinateSystem`/`getOrientation` contract (not a port of WP7's own
  axis-selection logic, which uses a different coordinate convention). 9 new tests
  with hand-derived, documented-inline test quaternions.
- **`ACCEL-008` (closed):** decision made (kept the existing Android landscape-remap
  rather than removing it) and implemented: `Detail::SetAndroidLandscapeRemapEnabled()`/
  `IsAndroidLandscapeRemapEnabled()` (new `AndroidSensorOrientation.cpp`, previously
  header-only), wired into `Accelerometer.cpp`/`Gyroscope.cpp`. Doc comments updated to
  state explicitly this is a CNA-only deviation from real WP7 behavior. **Opened a new
  follow-up, `MOTION-012`**, for `Motion`'s still-unremapped vector fields (not bundled
  into this task — needs its own verification first).
- **`SDL-SENSOR-004` (closed, cross-repo):** fixed in the sibling `sharp-runtime`
  repo — `TimeSpan::copy_count`/`move_count` changed from plain `int` to
  `std::atomic<int>` (relaxed ordering). Re-verified via `devices-tsan`: 0 races,
  down from 33 originally found.
- **`DEVICES-CNA-011` (closed):** implemented `CNA::Devices::MessageBox`
  (`Detail::IMessageBoxBackend`/`SdlMessageBoxBackend`, `MessageBoxType` enum),
  approved from `noxna_devices.md`'s Section 8 "Round 2" SDL3-capability survey as the
  strongest new candidate (broadest cross-platform reach of anything surveyed). 4 new
  tests.
- **`noxna_devices.md` Round 2 (Section 8):** surveyed further SDL3 capabilities
  beyond the original Phase 1-4 analysis. User approved `MessageBox`; explicitly
  rejected `Monitors`/multi-display enumeration, `Process`, and a pen-input candidate
  as not a fit for this project — removed from the document entirely, not just
  marked rejected.
- **Unrelated build fix, found opportunistically:** `GamerProfile.cpp` called
  `System::Globalization::RegionInfo::CurrentRegion()`, renamed to
  `getCurrentRegionProperty()` by a concurrent, unrelated upstream `sharp-runtime`
  commit. One-line fix, committed separately from any of the above.

---

## 4. Current blocker / main problem

**No build-blocking bug exists.** Every task assigned this session is closed, and the
full suite is green. The most significant unresolved technical finding is:

**A real, pre-existing ASan (LeakSanitizer) leak in the EasyGL/OpenGL graphics
backend**, surfaced because `CameraTests.cpp` is the first `CNA::Devices` test file
to construct a real `GraphicsDevice`/`Texture2D` under the `devices-asan` preset.

- **Symptom:** `ASAN_OPTIONS=detect_leaks=1 ./cmake-build-devices-asan/CnaTests
  --gtest_filter="CameraTests.*"` reports ~12KB leaked across 16 allocations. Stack
  traces point into `libdrm.so.2` and unresolved/unknown modules — **not** any
  `CNA::Devices` or `Camera` code path.
- **Confirmed unrelated to `Camera`:** the identical leak pattern (~30KB / 40
  allocations, same stack shape) reproduces from a completely unrelated,
  already-existing test, `DrawUserPrimitivesArgumentGuardTest`
  (`tests/Microsoft/Xna/Framework/Graphics/DrawUserPrimitivesTests.cpp`), which has
  no `Camera`/`CNA::Devices` involvement at all — it also just constructs a real
  `GraphicsDevice`. This is a pre-existing gap: no `CNA::Devices` test had ever
  constructed a real `GraphicsDevice` under `devices-asan` before `CameraTests.cpp`,
  so nothing had surfaced it until now.
- **Not investigated further:** whether this is a real EasyGL bug, a Mesa/libdrm
  driver-level non-issue (GPU drivers commonly hold onto some allocations LSan flags
  as "leaked" without a full driver teardown), or something else. Out of scope for
  the `Camera` task that found it.
- **Practical workaround in place:** `CameraTests`/any `GraphicsDevice`-constructing
  test under `devices-asan` must currently be run with
  `ASAN_OPTIONS=detect_leaks=0` to get a signal on *real* memory-safety bugs (that run
  is clean, exit 0). `devices-ubsan` is unaffected and clean.

This is not blocking any current task, but it means **`devices-asan`'s leak detection
is not currently trustworthy for anything that touches `GraphicsDevice`** until
someone investigates it properly.

---

## 5. Known bugs and limitations

- **Confirmed, unfixed, out of scope for now:** the ASan graphics-backend leak
  above (Section 4).
- **Incomplete:** `MOTION-012` (new this session) — `Motion.Gravity`/
  `DeviceAcceleration`/`DeviceRotationRate` don't get the same Android
  landscape-remap `Accelerometer`/`Gyroscope` now explicitly document, which is now
  an inconsistency per `ACCEL-008`'s own decision. Not started — needs to first
  verify whether Android's `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION` sensors report
  in the same raw portrait-frame convention as the plain accelerometer/gyroscope
  before reusing the same remap formula (see `plan_devices.md` `MOTION-012` for the
  full writeup). `Motion.Attitude` (the quaternion) is explicitly out of scope for
  that task too.
- **Needs verification (real hardware, never done for anything in this project):**
  `ACCEL-008`'s remap-vs-real-WP7 behavior, `COMPASS-009`'s new tilt-mode math,
  `Camera`'s RGBA format-negotiation reliability across real platforms/devices — all
  self-consistency-tested only, never checked against real hardware or a real camera.
- **Incomplete by design (documented, not a bug):** `Camera`'s first-implementation
  scope deliberately excludes: multi-camera device selection (always opens the
  first device `SDL_GetCameras()` reports), `CameraState::Lost` detection (needs SDL
  event-queue integration), and any pixel-format conversion beyond RGBA8 (fails to
  `NotSupported` instead). See `docs/cna-devices-camera-design.md`'s open questions.
- **By design, not a bug (`Microsoft::Devices::Sensors`):** `Compass.TrueHeading`
  always equals `MagneticHeading` (no declination source — see
  `docs/location-future-plan.md`). `Motion.Calibrate` is never raised by any backend.
- **Unknown:** whether `CNA_DEVICES=OFF` (the default) still builds clean after this
  session's additions (`MessageBox`, `Camera`) — the `#ifdef CNA_DEVICES` pattern was
  followed consistently, but a fresh OFF-default build was not re-run this session to
  confirm (it was last explicitly confirmed at `DEVICES-CNA-006`, before `MessageBox`/
  `Camera` existed).
- **Needs verification, likely permanent:** iOS cross-compilation — no Apple
  toolchain in this Linux container.
- **Out of scope, not this repo's concern:** none currently — `SDL-SENSOR-004`
  (the one sibling-repo item) was fixed and closed this session.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/          ← XNA WP7 sensor API headers
include/Microsoft/Devices/Sensors/Detail/   ← internal-only, never in public headers
src/Microsoft/Devices/Sensors/              ← sensor implementations (SDL3-backed + Android-native)
tests/Microsoft/Devices/Sensors/            ← Google Test suites per class
include/Microsoft/Devices/                  ← VibrateController.hpp
src/Microsoft/Devices/                      ← VibrateController.cpp
tools/devices/                              ← StrictXnaApiSurfaceCheck.cpp (VERIFY-003, standalone, not gtest)
examples/demo_devices/                      ← DevicesDemo (cna_demo_devices target, not touched this session)
docs/devices-hardware-checklist.md          ← manual real-hardware verification steps
docs/devices_sensor_hardware_qa_template.md ← manual real-hardware QA report template
docs/devices-native-backend-design.md       ← Compass/Motion native backend design
docs/devices-api-coverage.md                ← per-member API coverage table + timestamp policy
docs/location-future-plan.md                ← why GPS/location isn't here
plan_devices.md                             ← the Microsoft::Devices::Sensors plan; effectively done, 1 task OPEN (MOTION-012)

include/CNA/Devices/                        ← CNA::Devices public headers (non-XNA, no NOXNA tag)
include/CNA/Devices/Detail/                 ← internal-only backend interfaces (I<X>Backend pattern)
src/CNA/Devices/                            ← CNA::Devices implementations
tests/CNA/Devices/                          ← Google Test suites per class
docs/cna-devices-camera-design.md           ← Camera design note (superseded in part by the actual implementation)
noxna_devices.md                            ← the CNA::Devices analysis, including Round 2 (Section 8)
plan_cna_devices.md                         ← the CNA::Devices plan; all 12 tasks CLOSED
```

- **`SensorBase<T>`** owns `CurrentValue`, `IsDataValid`, `TimeBetweenUpdates`,
  `CurrentValueChanged`, `Dispose()`. Every field is mutex-guarded. **Do not
  restructure without a concrete, newly-found bug** — stable across many hardening
  passes, not touched this session.
- **`NOXNA` is compile-time enforced** (`VERIFY-003`, from a prior session) —
  `CNAHelper.hpp`'s `NOXNA` macro expands to `[[deprecated]]` under
  `CNA_STRICT_XNA_API`; `cna_strict_xna_api_check` builds
  `tools/devices/StrictXnaApiSurfaceCheck.cpp` with `-Werror=deprecated-declarations`.
  This applies only inside `Microsoft::Devices`/`Sensors` — `CNA::Devices` members are
  never `NOXNA`-tagged (the namespace itself is the signal).
- **`Compass`/`Motion`** each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`.
  `AndroidCompassBackend` now calls `ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode()`
  (this session's change) instead of the flat-mode-only formula directly — the
  original flat-mode function is unchanged and still independently tested.
- **Android landscape-remap opt-out (`ACCEL-008`, new this session):**
  `Detail::SetAndroidLandscapeRemapEnabled(bool)`/`IsAndroidLandscapeRemapEnabled()`
  — a process-wide `std::atomic<bool>`, default `true`. `Accelerometer.cpp`/
  `Gyroscope.cpp` check it at runtime inside their `#ifdef __ANDROID__` blocks.
  `Motion` does **not** yet check it (`MOTION-012`).
- **`CNA::Devices` backend-injection pattern — two shapes, pick based on the real
  backend's side-effect timing:** a process-wide swappable static
  (`FileDialog`/`MessageBox` — real backend construction is inert, side effect only
  on the actual call) vs. constructor-injection (`SystemTray`/`Camera` — the real
  backend's side effect, opening a tray icon or a camera device, happens immediately
  on construction, so a post-construction swap would be too late).
- **`Camera`'s texture-upload path:** `Texture2D::SetDataRGBA(data, pixelCount)`
  assumes `width * 4` stride with **no row padding** and uses the `Texture2D`'s own
  stored `width`, not any width the caller passes — the caller **must** construct
  the `Texture2D` with dimensions matching `Camera::getFrameWidthProperty()`/
  `getFrameHeightProperty()` in advance; `Texture2D` has no resize-after-construction
  API. `Camera::TryAcquireFrame()` returns `false` without touching the texture if
  the dimensions don't match, rather than silently uploading with the wrong stride.
- **Boundaries — do not cross:**
  - `third_party/SDL` is vendored with its own `CLAUDE.md` forbidding AI-authored
    contributions — read-only for research.
  - `sharp-runtime` is a sibling repo under separate, concurrent development (another
    active worktree/agent has been observed there this session) — only edit it for a
    specific, well-scoped, cited fix (as `SDL-SENSOR-004` was), never a broad change.
  - Do not fake `Compass`/`Motion` data from other sensors, and do not add
    GPS/location to `Microsoft::Devices::Sensors` under any circumstances — see
    `docs/location-future-plan.md`.
  - The XNA 4.0 class name is `VibrateController`, not "VibrationController."
  - Do not move `CNA::Devices` types into `Microsoft::Devices` or vice versa — they
    are deliberately separate namespaces with different tagging conventions.

---

## 7. Useful commands

```bash
# Configure (only if CMakeCache.txt is stale/missing) -- CNA_DEVICES defaults OFF,
# turn it on explicitly to build/test anything under CNA::Devices:
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_DEVICES=ON

# Build:
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# CNA::Devices-only filter (10 suites / 48 cases):
./cmake-build-debug/CnaTests --gtest_filter="PowerInfoTests.*:LocaleTests.*:ClipboardTests.*:UrlLauncherTests.*:SystemInfoTests.*:DisplayInfoTests.*:FileDialogTests.*:SystemTrayTests.*:MessageBoxTests.*:CameraTests.*"

# Full suite:
./cmake-build-debug/CnaTests

# Sanitizer builds (separate build dirs, CMakePresets.json):
cmake --build --preset devices-asan --target CnaTests
cmake --build --preset devices-ubsan --target CnaTests
cmake --build --preset devices-tsan --target CnaTests

# Re-run the ASan graphics-backend leak (Section 4) -- reproduces with or without Camera:
ASAN_OPTIONS=detect_leaks=1 ./cmake-build-devices-asan/CnaTests --gtest_filter="CameraTests.*"
ASAN_OPTIONS=detect_leaks=1 ./cmake-build-devices-asan/CnaTests --gtest_filter="DrawUserPrimitivesArgumentGuardTest.*"
# Workaround for a clean signal on real memory-safety bugs in the meantime:
ASAN_OPTIONS=detect_leaks=0 ./cmake-build-devices-asan/CnaTests --gtest_filter="CameraTests.*"

# Strict XNA API surface check:
cmake --build cmake-build-debug --target cna_strict_xna_api_check -j$(nproc)
./cmake-build-debug/cna_strict_xna_api_check

# sharp-runtime (sibling repo) tests, if touching that repo again:
cd ../sharp-runtime && cmake --build build --target SharpRuntimeTests -j$(nproc)
./build/SharpRuntimeTests --gtest_filter="TimeSpan*"
```

No dedicated lint/format tooling is configured for this project. No CI has actually
been run on a real provider from this container (a workflow spec exists,
`.github/workflows/devices-tests.yml`, from an earlier session).

---

## 8. Next smallest tasks

1. **Investigate the ASan graphics-backend leak (Section 4).**
   Goal: determine whether the leak reported when constructing a real `GraphicsDevice`
   under `devices-asan` is a real EasyGL bug or a benign Mesa/libdrm driver artifact,
   and either fix it or document a definitive reason it's safe to ignore.
   Files: `src/CNA/Internal/Backends/EasyGL/*`, possibly `third_party/easy-gl`.
   Verify: `ASAN_OPTIONS=detect_leaks=1 ./cmake-build-devices-asan/CnaTests --gtest_filter="DrawUserPrimitivesArgumentGuardTest.*"` — goal is 0 leaked bytes, or a documented reason why not.

2. **Confirm `CNA_DEVICES=OFF` (default) still builds clean.**
   Goal: close the "Unknown" item in Section 5 — a fresh configure+build with the
   flag left at its default OFF value, after this session's `MessageBox`/`Camera`
   additions.
   Files: none expected (this is a verification task, not a code change, unless it
   reveals a real `#ifdef` gap).
   Verify: `cmake -S . -B /tmp/cna-off-check -DCNA_BUILD_TESTS=ON && cmake --build /tmp/cna-off-check --target CNA CnaTests -j$(nproc)`.

3. **Re-run `devices-tsan` against `AndroidCompassMathTests`/`CameraTests`.**
   Goal: close the "not yet re-run" gap noted in Section 2 — these two new suites
   were verified under `devices-asan`/`devices-ubsan` but not `devices-tsan`.
   Files: none expected (verification only).
   Verify: `cmake --build --preset devices-tsan --target CnaTests && ./cmake-build-devices-tsan/CnaTests --gtest_filter="AndroidCompassMathTests.*:CameraTests.*"`.

4. **Start `MOTION-012`** (see `plan_devices.md` for the full task writeup) — first
   step is research only: confirm via SDL3/Android NDK sensor documentation whether
   `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION` report in the same raw portrait-frame
   convention as `TYPE_ACCELEROMETER`/`TYPE_GYROSCOPE`, before writing any remap code.
   Files: `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp` (read-only
   for this first step).
   Verify: no code change expected for this first step; a documented finding in
   `plan_devices.md`'s `MOTION-012` entry is the deliverable.

---

## 9. Do not do yet

- Do not implement the rest of `Camera`'s deferred scope (multi-camera selection,
  `Lost`-state event-queue integration, non-RGBA format conversion) without a
  concrete need — the current narrow scope was a deliberate choice, not an oversight.
- Do not apply `Detail::ConvertAndroidPortraitToXnaLandscape()` to `Motion`'s vector
  fields without first doing `MOTION-012`'s own research step (confirming Android's
  gravity/linear-acceleration sensors use the same raw frame) — reusing the formula
  blind repeats a mistake this project has hit before with unverified Android sensor
  math.
- Do not "fix" the ASan graphics-backend leak by just adding
  `ASAN_OPTIONS=detect_leaks=0` somewhere permanent (e.g. a CMake preset default) —
  that would silently hide future *real* leaks too. Investigate first.
- Do not restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem<TSensor>`, or
  `Detail::AndroidSensorBridge`'s locking scheme without a concrete, newly-found bug.
- Do not edit anything under `third_party/SDL` — vendored, forbids AI-authored
  contributions per its own `CLAUDE.md`.
- Do not make broad, unscoped edits to `sharp-runtime` — it's under active,
  concurrent development by another session; only well-scoped, cited fixes.
- Do not add GPS/location to `Microsoft::Devices::Sensors`, or move `CNA::Devices`
  types into `Microsoft::Devices` (or vice versa).
- Do not claim real Android/iOS hardware or real-camera verification unless it was
  actually done in the current session.
- Do not push to the remote unless the user explicitly asks (this session pushed
  after each closed task per the user's own standing instruction for this branch —
  confirm that's still the expectation before assuming it by default).

---

## 10. Resume prompt

```
Read NEXT.md first. Both plans on this branch (plan_devices.md,
plan_cna_devices.md) are effectively done -- only one task is open
(MOTION-012, Microsoft::Devices::Sensors::Motion's landscape-remap
consistency question) and it needs a research step before any code change.

The most concrete unresolved technical finding is a pre-existing ASan leak in
the EasyGL/OpenGL graphics backend (NEXT.md Section 4) -- not blocking, but
worth investigating (NEXT.md Section 8, task 1) since it currently makes
devices-asan's leak detection untrustworthy for anything touching
GraphicsDevice.

Pick ONE task from NEXT.md Section 8. Inspect only the files it names. Do not
refactor unrelated code, and do not start any deferred/out-of-scope work
listed in Section 9. Make one small, verified improvement, run the exact
verification command listed for that task, and update NEXT.md's relevant
section (2, 3, 5, and/or 8) to reflect what changed before ending the
session.
```
