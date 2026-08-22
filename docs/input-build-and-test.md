<!-- SPDX-License-Identifier: MS-PL -->
# Building and Testing CNA Input

> **Related input docs (INP-0003):** [plan](../plans/plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

This document explains how to build and run the CNA `Input` unit tests from a **complete** checkout,
and — importantly — what **cannot** be verified from a source-only archive or in a headless CI
environment. (Phase I13/I14, tasks 883/884.)

## What a complete checkout needs

CNA input depends on three kinds of external code:

| Dependency | How it is obtained | Notes |
|---|---|---|
| `third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer` | **git submodules** of this repo | `git submodule update --init --recursive`. Alternative: `-DCNA_USE_SYSTEM_SDL=ON` builds against installed system SDL3/SDL3_image/SDL3_mixer packages instead (`find_package(SDL3 REQUIRED)` etc., `cmake/ThirdPartySDL.cmake`) — no Input behavior differs either way, it is purely a build-time source-of-headers/libs choice (P9-025). |
| `vendor/googletest` | **git submodule** | test framework |
| `../sharp-runtime` | **sibling repository** (NOT a submodule) | CNA's C++ `System.*` runtime; must sit next to `cna_input` |
| `../easy-gl` | **sibling repository** (NOT a submodule) | only for the `OPENGLES3` backend |

> A source-only `.zip` of just `cna_input` is **not** buildable: it is missing the submodules and the
> two sibling repositories. The CMake configure step now fails early with an actionable message
> naming exactly what is missing (see `CMakeLists.txt` — the `sharp-runtime`/`easy-gl` guards and
> `cmake/ThirdPartySDL.cmake`'s per-submodule check). Do not report "tests pass" from such an
> archive — there is nothing to run.

### Bootstrap from a fresh clone

```bash
# 1. The vendored SDL family + googletest (submodules of this repo):
git submodule update --init --recursive

# 2. The sibling runtime + (for EasyGL) the GL helper, checked out NEXT TO cna_input:
#    <parent>/cna_input, <parent>/sharp-runtime, <parent>/easy-gl
git -C .. clone <sharp-runtime-url> sharp-runtime
git -C .. clone <easy-gl-url> easy-gl        # only needed for -DCNA_GRAPHICS_RENDERER=OPENGLES3

# 3. Optional VideoPlayer backend (unrelated to input); omit this and use
#    -DCNA_ENABLE_VIDEO=OFF when the input build does not need video:
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

## Configure, build, run

The backend does not change input behavior (input is backend-agnostic), so any backend works for
running the input tests. EasyGL is the default.

```bash
# Configure with tests enabled (pick ONE backend):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON
# or VULKAN / BGFX / SDL_RENDERER

# Build the test binary:
cmake --build cmake-build-input-easygl --target CnaTests

# Run everything:
./cmake-build-input-easygl/CnaTests

# Run ONLY the input tests — the canonical way (INPUT-BUILD-003).
# `ctest -L input` runs the CnaInputTests entry, which invokes the single-source-of-truth filter
# (CNA_INPUT_TEST_FILTER in CMakeLists.txt) shuffled x5 for order-independence. This is what CI runs.
ctest --test-dir cmake-build-input-easygl -L input --output-on-failure
```

> **Determinism gate (INPUT-BUILD-009).** `ctest -L input` is not just the way to run the subset — it
> **is** the required order-independence check. Its `CnaInputTests` entry bakes in
> `--gtest_shuffle --gtest_repeat=5`, so every invocation runs the filtered subset five times, each under
> a fresh shuffle seed. The input state is a process-wide singleton (`InputManager`, `GestureDetector`,
> the `MouseCursor` stock cursors), so a future static-state leak would resurface as an order-dependent
> failure here. This gate must stay **green on every built backend** (EasyGL / Vulkan / bgfx /
> SDL_RENDERER — the command is identical and backend-agnostic). For a deeper sweep, invoke the binary
> directly with a higher `--gtest_repeat` and the `CNA_INPUT_TEST_FILTER` value.

> Do **not** hand-copy a `--gtest_filter` string to select the input subset — it drifts (a new suite
> whose name matches none of the tokens is silently skipped). The one authoritative token list lives in
> `CMakeLists.txt` as `CNA_INPUT_TEST_FILTER`; `ctest -L input` is the stable command. If you must invoke
> the binary directly (e.g. to pass extra gtest flags), read the current filter from that variable.

> **Headless note:** the `MouseCursor` tests need real cursors, which the SDL `dummy` video driver
> cannot create. In CI they run under `xvfb-run` with `SDL_VIDEODRIVER=x11`; do the same locally on a
> headless box (`xvfb-run -a ctest --test-dir <build> -L input`).

## Test counts (authoritative baseline)

Recorded **2026-07-16** (updated from the 2026-07-06 baseline as part of `plans/plan_input.md` P13-006, the
Input subsystem grew substantially in between — including the `feature/xnb` merge and the
`audit_input.md` Phase 13 defect-remediation pass) in this checkout: Debian 13, g++ 14.2.0, CMake
3.31.6, Ninja 1.12.1. Input is backend-agnostic — the input-filter count is identical on EasyGL /
Vulkan / bgfx / SDL_RENDERER.

**Pinned versions (INPUT-DOC-014 / INP-0196).** Reference toolchain as above (g++ 14.2.0 / CMake 3.31.6 /
Ninja 1.12.1, Debian 13).

The **SDL** dependency is the `third_party/SDL` git submodule (`libsdl-org/SDL`), pinned at commit
**`cbe3fbe9f367340dcd924de29c225c9f4ffea1f5`** (alongside `SDL_image`/`SDL_mixer`);
`git submodule update --init --recursive` restores it. **Tag lookup:**
`git -C third_party/SDL describe --tags` = **`release-3.4.0-685-gcbe3fbe9f`** — 685 commits past the
`release-3.4.0` tag on SDL `main`, self-reporting as SDL **3.5.0** (in-development). It is **not** a tagged
release; the nearest **stable release tags** are the `release-3.4.x` line (latest **`release-3.4.8`**).

**Minimum SDL3 API relied upon:** standard SDL3 gamepad and raw joystick APIs (including hotplug,
axes/buttons/hats/trackballs and joystick-to-haptic correlation), keyboard
(scancode/keycode), mouse (relative mode, warp), touch (`SDL_EVENT_FINGER_*`), text-input, and gamepad
sensor/rumble/trigger-rumble — all present since SDL **3.2 / 3.4.0**, so a `release-3.4.x` build has
everything the input layer uses.

> **Before bumping to the tag:** the build does **not** compile SDL from the submodule directly —
> `CNA_SDL_PREBUILT_ROOT` points at a shared **`.sdl-prebuilt`** cache (currently SDL 3.5.0-dev, built from
> the commit above and shared by every `cmake-build-*` dir + all backends). Moving the submodule to
> `release-3.4.8` is a *deliberate* infra step: it also needs the shared prebuilt rebuilt
> (`rm -rf .sdl-prebuilt* && reconfigure`) and `ctest -L input` re-run on all four backends to confirm the
> main→release-branch change is behavior-neutral. It is intentionally **not** done as a blind checkout
> (that would silently downgrade the shared prebuilt under other build dirs). The submodule move is tracked
> by INPUT-BUILD-004; INP-0196 records the tag/version/min-API findings above.

### Headless run inventory (INPUT-BUILD-008)

`MouseCursor` and `Mouse::SetCursor` tests are display-independent as of PLAT-81: they verify the
platform-neutral system/custom cursor descriptions through a canned `IPlatformMouse`. SDL-native
cursor creation remains covered at the SDL platform edge. Other input tests that create real windows
still require `xvfb-run` + `SDL_VIDEODRIVER=x11` in CI; the dummy driver may skip those window cases.

### Fresh-clone reproducibility (INPUT-BUILD-001)

The **continuous** fresh-clone proof is the CI workflow `.github/workflows/input-ci.yml`: every run
starts from a clean `ubuntu-24.04` runner with **no** warm build dir and **no** `.sdl-prebuilt` cache,
does `git submodule update --init --recursive` + clones the `sharp-runtime`/`easy-gl` siblings, configures,
builds `CnaTests`, and runs `xvfb-run -a ctest -L input` — green across the 5-backend matrix. That is the
recorded transcript of a clean environment building and passing with no manual patching.

To reproduce locally in a throwaway clone (siblings present next to it):

```bash
git clone <cna_input-url> /tmp/cna-fresh && cd /tmp/cna-fresh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON
cmake --build build --target CnaTests
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir build -L input --output-on-failure
```

If a dependency is missing, the configure step fails fast with a copy-pasteable remedy (INPUT-BUILD-005):
a missing `third_party/SDL*` submodule prints `Run: git submodule update --init --recursive`; a missing
`sharp-runtime`/`easy-gl` sibling prints the exact `git clone … <path>` command (and, for `easy-gl`, the
option to pick another backend). Verified in `cmake/ThirdPartySDL.cmake` and `CMakeLists.txt`.

| Metric | Count |
|--------|-------|
| Canonical input filter (the filter above) | **524 passed**, 0 failed, 5x `--gtest_shuffle --gtest_repeat` clean (updated 2026-07-17, `plans/plan_input.md` Phases 1-9) |
| Full `CnaTests` suite (unfiltered) | **does not currently complete** — see below |

Notes:
- **Updated 2026-07-17 (`plans/plan_input.md` P9-027):** the input-filter count grew from the 2026-07-16
  baseline of 496 to **524** across the `feature/input` audit. The Phase 1-9 sessions covered by this
  update added 7 tests directly (`KeyboardStateTest.GetPressedKeysHasNoDuplicateWhenSameKeyGivenTwice`
  P2-011; the former P2-056 modifier combination is now covered by
  `PlatformInputBridgeKeyboardTest.ModifierAndLockKeysStayDistinct`;
  `PlatformInputBridgeMouseTest.MotionEventUpdatesAbsolutePosition`,
  `.MotionEventRelativeDeltaReachesInputManagerThroughBridge`, and
  `.MotionEventConvertsWindowCoordinatesToLogicalForLetterboxedRenderer` P3-013/P3-039;
  `GestureDetectorTest.GestureTimestampIsNonNegativeAndAdvancesWithTheClock` P6-012;
  `SdlGamepadSubsystemInit.ShutdownQuitsSubsystemAndIsSafeToCallRepeatedly` P8-002); the remaining
  21 were added by earlier session work (the Phase 1 per-type audits) between the 496 baseline and
  this update — see each task's own `plans/plan_input.md` Result for its exact test name.
- **The unfiltered full-suite count above is deliberately NOT restated as a stable N-passed/N-failed
  figure, because it is no longer accurate to give one.** `plans/plan_input.md` P9-031 (2026-07-17) found
  that running the full unfiltered `CnaTests` binary now **crashes** with `double free or corruption
  (fasttop)` (SIGABRT) inside `ENetBackendTest` (the Net subsystem) before reaching a final summary —
  confirmed, via isolation testing, to be **unrelated to Input** (`ENetBackendTest.*` alone passes
  cleanly; the corruption requires ~800 preceding tests' allocation history to manifest) and **not**
  present in any Input-filtered run this session (including under AddressSanitizer+UndefinedBehaviorSanitizer,
  P9-005/006). This is flagged as a real, separate, out-of-Input-scope memory-safety defect requiring
  dedicated bisection — see `plans/plan_input.md`'s P9-031 Result and `NEXT.md` for the full record. Until
  it is root-caused and fixed, do not trust or restate a "full suite N/N" figure from this checkout;
  the previously-recorded 2026-07-16 figure (4623 passed / 20 failed / 2 skipped, 4645 ran) predates
  this crash's discovery and should not be treated as still achievable.
- The 20 pre-2026-07-17 full-suite failures (before the crash was discovered) were pre-existing and
  unrelated to Input: all 20 were in the XNB/Content/Model/Effect/Texture3D pipeline
  (`"SDL_Renderer does not support 3D: CreateVertexBuffer"`, a known, documented `SDL_RENDERER`
  backend limitation — see `docs/sdl-renderer-2d-completeness.md` Task 725), not an input regression.
- The canonical filter's last four tokens (`*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*`) were
  added 2026-07-05 to catch the pure-enum value suites and the public-API header-hygiene suite that the
  older filter missed.
- **This table is the single source of truth for input test counts.** Other docs reference it rather than
  restating numbers. Re-run and update it whenever input tests are added.

## Input verification checklist (task 884)

1. `git submodule update --init --recursive` completes clean.
2. `../sharp-runtime` and (for EasyGL) `../easy-gl` exist.
3. Configure succeeds with `-DCNA_BUILD_TESTS=ON`.
4. `CnaTests` builds with no errors.
5. Full `CnaTests` run is green.
6. Input filter run is green.
7. Shuffle + repeat run is green (order-independence).
8. Record the exact counts and any failures (see the **Test counts** table above — the authoritative baseline).

## What CANNOT be verified headless (honest limitations)

These are **not** missing code — they require real hardware, a real IME, or a specific windowing
environment that the headless test binary does not provide. They are covered by targeted unit tests
of the translation logic only, and are otherwise manual/hardware-gated:

- **Real gamepad *actuation*** — a physical rumble motor spinning, real trigger haptics, a real
  sensor's live values, and genuine OS hot-plug / per-controller GUID. As of **Phase I15** the SDL
  gamepad *translation and bookkeeping* (hot-plug/slot assignment, button/axis mapping, capabilities,
  rumble/LED/sensor support, GUID formatting) is headless-tested at both sides of the platform
  contract: pure SDL mapping helpers plus a complete canned `IPlatformGamepad` drive public
  `GamePad` without hardware. What these tests cannot prove is that the physical device *acts* —
  that stays manual/hardware-gated. See `plans/plan_input.md`
  (Phase I15) and `docs/input-manual-verification-results.md`.
  - **Startup invariant:** the SDL gamepad subsystem is initialized explicitly in
    `Game::DoInitialize()` — before the first event pump and snapshot update — through
    `IPlatform::AcquireSubsystem(Gamepad)`. The matching reference is released by explicit disposal
    or the destructor, and `Sdl3Gamepad::Update()` enumerates pre-connected devices in frame one.
- **IME / composition** — real `TextEditing` composition, cursor, and selection over a physical IME.
- **Wayland OS-cursor landing** — `SDL_GetGlobalMouseState` is compositor-restricted, so the absolute
  landing pixel of `Mouse::SetPosition` is only readable under X11.

See `docs/input-fna-fidelity.md` for the per-area FNA-fidelity status and the full list of intentional
deviations.

## Troubleshooting (INP-0213)

| Symptom | Cause | Fix |
|---------|-------|-----|
| CMake configure fails: `Missing vendored 'SDL' …` | submodules not initialized | `git submodule update --init --recursive` |
| CMake configure fails: `required sibling repository 'sharp-runtime' … not found` | sibling repo not checked out next to `cna_input` | `git -C .. clone <sharp-runtime-url> sharp-runtime` |
| CMake configure fails: `the OPENGLES3 backend requires … 'easy-gl'` | `easy-gl` sibling missing | clone it next to `cna_input`, or use `-DCNA_GRAPHICS_RENDERER=VULKAN` (or BGFX / SDL_RENDERER) |
| `ctest -L input` fails on 3 `MouseCursor` cases | headless `SDL_VIDEODRIVER=dummy` (null cursors) | run under a display: `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest -L input` |
| ASan reports leaks in `libGLX_mesa` | third-party Mesa GLX at process exit (not CNA) | run with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` (what CI uses) |
| Gamepad panels stay "disconnected" in `demo_input` | no controller / SDL can't map the device | attach a controller SDL knows (see `gamecontrollerdb`); Steam Input presents a virtual pad |
| `SDL_GetGlobalMouseState` returns `(0,0)` | Wayland compositor security policy | force `SDL_VIDEODRIVER=x11`/XWayland, or use relative mouse mode |
