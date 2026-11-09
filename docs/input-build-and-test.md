<!-- SPDX-License-Identifier: MS-PL -->
# Building and Testing CNA Input

> **Related input docs (INP-0003):** [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual hardware checks](demo-input-checklist.md#hardware-verification-record) · [demo checklist](demo-input-checklist.md)

This document explains how to build and run the CNA `Input` unit tests from a **complete** checkout,
and what **cannot** be verified from a source-only archive or in a headless CI environment
(tasks 883/884).

## What a complete checkout needs

CNA input depends on three kinds of external code:

| Dependency | How it is obtained | Notes |
|---|---|---|
| `third_party/SDL`, `third_party/SDL_mixer` | **git submodules** of this repo | `git submodule update --init`. Alternative: `-DCNA_USE_SYSTEM_SDL=ON` builds against installed system SDL3/SDL3_mixer packages instead (`find_package(SDL3 REQUIRED)` etc., `cmake/ThirdPartySDL.cmake`) — no Input behavior differs either way, it is purely a build-time source-of-headers/libs choice (P9-025). |
| `vendor/googletest` | **git submodule** | test framework |
| `../sharp-runtime` | **sibling repository** (NOT a submodule) | CNA's C++ `System.*` runtime; must sit next to `cna` |
| `../easy-gl` | **sibling repository** (NOT a submodule) | only for the EasyGL renderer family (`OPENGLES3`, `OPENGL33`, `WEBGL2`) |

> A source-only `.zip` of just `cna` is **not** buildable: it is missing the submodules and the
> two sibling repositories. The CMake configure step fails early with an actionable message
> naming exactly what is missing (see `CMakeLists.txt` — the `sharp-runtime`/`easy-gl` guards and
> `cmake/ThirdPartySDL.cmake`'s per-submodule check). Do not report "tests pass" from such an
> archive — there is nothing to run.

### Bootstrap from a fresh clone

```bash
# 1. The vendored SDL family + googletest (submodules of this repo):
git submodule update --init

# 2. The sibling runtime + (for EasyGL) the GL helper, checked out NEXT TO cna:
#    <parent>/cna, <parent>/sharp-runtime, <parent>/easy-gl
git -C .. clone <sharp-runtime-url> sharp-runtime
git -C .. clone <easy-gl-url> easy-gl        # only needed for -DCNA_GRAPHICS_RENDERER=OPENGLES3

# 3. Optional VideoPlayer decoder backend (unrelated to input); omit this and use
#    -DCNA_ENABLE_VIDEO=OFF when the input build does not need video:
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

## Configure, build, run

The graphics renderer does not change input behavior, so any compatible renderer can run these
tests. The dedicated input CI uses `OPENGLES3` and `SDLGPU` plus an `OPENGLES3` sanitizer job.

```bash
# Configure with tests enabled (pick one renderer):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON
# or SDLGPU / SOFTWARE

# Build both canonical input lanes (maximum eight compile jobs):
cmake --build cmake-build-input-easygl --target cna_input_test_gate --parallel 8

# Run everything:
./cmake-build-input-easygl/CnaTests

# Run only the input gate — the canonical way (INPUT-BUILD-003).
# The label contains the complete module-owned executable and one explicit cross-module lane.
ctest --test-dir cmake-build-input-easygl -L input --output-on-failure
```

> **Determinism gate (INPUT-BUILD-009).** `ctest -L input` is not just the way to run the subset — it
> **is** the required order-independence check. Both entries bake in
> `--gtest_shuffle --gtest_repeat=5`, so every invocation runs each lane five times, each under
> a fresh shuffle seed. The input state is a process-wide singleton (`InputManager`, `GestureDetector`,
> the `MouseCursor` stock cursors), so a future static-state leak would resurface as an order-dependent
> failure here. This gate must stay green on every renderer in `.github/workflows/input-ci.yml`.

> `CnaInputModuleTests` (registered as the `CnaInputTests` CTest) needs no filter: every translation
> unit under `modules/input/tests` belongs to it automatically. The second entry,
> `CnaInputCrossModuleTests`, uses the suite-level `CNA_INPUT_CROSS_MODULE_TEST_FILTER` in
> `cmake/UnitTests.cmake`. Do not replace it with broad substrings such as `*Touch*`; GoogleTest
> matches the whole `Suite.Method` name and would pull unrelated rendering, video and network methods
> into the input gate. `ctest` reports the current per-lane counts; no fixed count is maintained in
> the documentation.

### Headless run inventory (INPUT-BUILD-008)

`MouseCursor` and `Mouse::SetCursor` tests are display-independent (PLAT-81): they verify the
platform-neutral system/custom cursor descriptions through a canned `IPlatformMouse`. SDL-native
cursor creation is covered at the SDL platform edge (`modules/platform/tests`). Input tests that
create real windows still need a display: CI runs them under `xvfb-run` with
`SDL_VIDEODRIVER=x11`, and the dummy driver may skip those window cases. On HEADLESS, the two
clipboard round trips (no clipboard service) and the SDL3 relative-mouse-mode real-window case
skip; the X11 CI lanes provide those services. On a developer desktop, run window-creating tests
through `tools/platform/run_gpu_tests_private.sh`.

## SDL pin and minimum API (INPUT-DOC-014 / INP-0196)

The **SDL** dependency is the `third_party/SDL` git submodule (`libsdl-org/SDL`), pinned at commit
**`86296ac8f01597076979630667472a846b04ce46`** (alongside `SDL_mixer`);
`git submodule update --init` restores it. It is an untagged commit on SDL `main` past
`release-3.4.0` (`git -C third_party/SDL describe --tags`), self-reporting as SDL **3.5.0**
(in development), not a tagged release.

**Minimum SDL3 API relied upon:** standard SDL3 gamepad and raw joystick APIs (including hotplug,
axes/buttons/hats/trackballs and joystick-to-haptic correlation), keyboard
(scancode/keycode), mouse (relative mode, warp), touch (`SDL_EVENT_FINGER_*`), text-input, and gamepad
sensor/rumble/trigger-rumble — all present since SDL **3.2 / 3.4.0**, so a `release-3.4.x` build has
everything the input layer uses.

> **Before moving to a release tag:** the build does **not** compile SDL from the submodule
> directly — `CNA_SDL_PREBUILT_ROOT` points at a shared **`.sdl-prebuilt`** cache, built from the
> commit above and shared by every `cmake-build-*` dir and all renderers. Moving the submodule to a
> `release-3.4.x` tag is a *deliberate* infra step: it also needs the shared prebuilt rebuilt and
> `ctest -L input` re-run on the current CI renderer matrix to confirm the main→release-branch change
> is behavior-neutral. It is intentionally **not** done as a blind checkout (that would silently
> downgrade the shared prebuilt under other build dirs). The submodule move is tracked by
> INPUT-BUILD-004.

## Fresh-clone reproducibility (INPUT-BUILD-001)

The **continuous** fresh-clone proof is the CI workflow `.github/workflows/input-ci.yml`: every run
starts from a clean `ubuntu-24.04` runner with **no** warm build dir and **no** `.sdl-prebuilt` cache,
does `git submodule update --init` + clones the `sharp-runtime`/`easy-gl` siblings, configures,
builds `cna_input_test_gate`, checks both generated input reports, and runs
`xvfb-run -a ctest -L input`. Its matrix has two renderer identities (`OPENGLES3`, `SDLGPU`) plus an
ASan+UBSan `OPENGLES3` job.

To reproduce locally, clone once next to the existing `sharp-runtime`/`easy-gl` siblings (never under
`/tmp` or a session scratchpad; reuse that clone and its `build/` for later reruns and remove it when
it is no longer needed), with your usual ccache configuration:

```bash
git clone <cna-url> ../cna-fresh && cd ../cna-fresh
git submodule update --init
cmake -S . -B build -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build --target cna_input_test_gate --parallel 8
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir build -L input --output-on-failure
```

If a dependency is missing, the configure step fails fast with a copy-pasteable remedy (INPUT-BUILD-005):
a missing `third_party/SDL*` submodule prints `Run: git submodule update --init`; a missing
`sharp-runtime`/`easy-gl` sibling prints the exact `git clone … <path>` command (and, for `easy-gl`, the
option to pick another renderer). See `cmake/ThirdPartySDL.cmake` and `CMakeLists.txt`.

## Input verification checklist (task 884)

1. `git submodule update --init` completes clean.
2. `../sharp-runtime` and (for EasyGL) `../easy-gl` exist.
3. Configure succeeds with `-DCNA_BUILD_TESTS=ON`.
4. `cna_input_test_gate` builds with no errors.
5. Full `CnaTests` run is green.
6. Both input-label lanes are green.
7. Shuffle + repeat run is green (order-independence).
8. Record the counts `ctest` reports and any failures or skips in the change description.

## What CANNOT be verified headless (honest limitations)

These are **not** missing code — they require real hardware, a real IME, or a specific windowing
environment that the headless test binary does not provide. They are covered by targeted unit tests
of the translation logic only, and are otherwise manual/hardware-gated:

- **Real gamepad *actuation*** — a physical rumble motor spinning, real trigger haptics, a real
  sensor's live values, and genuine OS hot-plug / per-controller GUID. The SDL gamepad *translation
  and bookkeeping* (hot-plug/slot assignment, button/axis mapping, capabilities, rumble/LED/sensor
  support, GUID formatting) is headless-tested at both sides of the platform contract: pure SDL
  mapping helpers plus a complete canned `IPlatformGamepad` drive public `GamePad` without
  hardware. What these tests cannot prove is that the physical device *acts* — that stays
  manual/hardware-gated; see the hardware verification record of `docs/demo-input-checklist.md`.
  The gamepad startup invariant is stated in `docs/input-fna-fidelity.md` (GamePad).
- **IME / composition** — real `TextEditing` composition, cursor, and selection over a physical IME.
- **Wayland OS-cursor landing** — `SDL_GetGlobalMouseState` is compositor-restricted, so the absolute
  landing pixel of `Mouse::SetPosition` is only readable under X11.

See `docs/input-fna-fidelity.md` for the per-area FNA-fidelity status and the full list of intentional
deviations.

## Troubleshooting (INP-0213)

| Symptom | Cause | Fix |
|---------|-------|-----|
| CMake configure fails: `Missing vendored 'SDL' …` | submodules not initialized | `git submodule update --init` |
| CMake configure fails: `required sibling repository 'sharp-runtime' … not found` | sibling repo not checked out next to `cna` | `git -C .. clone <sharp-runtime-url> sharp-runtime` |
| CMake configure fails: `the OPENGLES3 renderer requires … 'easy-gl'` | `easy-gl` sibling missing | clone it next to `cna`, or use `-DCNA_GRAPHICS_RENDERER=SDLGPU` (or `SOFTWARE`) |
| Window-creating input cases skip or fail headless | no display, or `SDL_VIDEODRIVER=dummy` | run under a display: `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest -L input` (the private runner on a desktop) |
| ASan reports leaks in `libGLX_mesa` | third-party Mesa GLX at process exit (not CNA) | run with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` (what CI uses) |
| Gamepad panels stay "disconnected" in `demo_input` | no controller / SDL can't map the device | attach a controller SDL knows (see `gamecontrollerdb`); Steam Input presents a virtual pad |
| `SDL_GetGlobalMouseState` returns `(0,0)` | Wayland compositor security policy | force `SDL_VIDEODRIVER=x11`/XWayland, or use relative mouse mode |
