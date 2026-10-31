<!-- SPDX-License-Identifier: MS-PL -->
# Building and Testing CNA Input

> **Related input docs (INP-0003):** [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

This document explains how to build and run the CNA `Input` unit tests from a **complete** checkout,
and — importantly — what **cannot** be verified from a source-only archive or in a headless CI
environment. (Phase I13/I14, tasks 883/884.)

## What a complete checkout needs

CNA input depends on three kinds of external code:

| Dependency | How it is obtained | Notes |
|---|---|---|
| `third_party/SDL`, `third_party/SDL_mixer` | **git submodules** of this repo | `git submodule update --init`. Alternative: `-DCNA_USE_SYSTEM_SDL=ON` builds against installed system SDL3/SDL3_mixer packages instead (`find_package(SDL3 REQUIRED)` etc., `cmake/ThirdPartySDL.cmake`) — no Input behavior differs either way, it is purely a build-time source-of-headers/libs choice (P9-025). |
| `vendor/googletest` | **git submodule** | test framework |
| `../sharp-runtime` | **sibling repository** (NOT a submodule) | CNA's C++ `System.*` runtime; must sit next to `cna` |
| `../easy-gl` | **sibling repository** (NOT a submodule) | only for the EasyGL renderer family (`OPENGLES3`, `OPENGL33`, `WEBGL2`) |

> A source-only `.zip` of just `cna` is **not** buildable: it is missing the submodules and the
> two sibling repositories. The CMake configure step now fails early with an actionable message
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

> `CnaInputModuleTests` needs no filter: every translation unit under `modules/input/tests` belongs to
> it automatically. The second entry uses the suite-level `CNA_INPUT_CROSS_MODULE_TEST_FILTER` in
> `cmake/UnitTests.cmake`. Do not replace it with broad substrings such as `*Touch*`; GoogleTest matches
> the whole `Suite.Method` name and would again pull unrelated rendering, video and network methods into
> the input gate.

> **Headless note:** the `MouseCursor` tests need real cursors, which the SDL `dummy` video driver
> cannot create. In CI they run under `xvfb-run` with `SDL_VIDEODRIVER=x11`; do the same locally on a
> headless box (`xvfb-run -a ctest --test-dir <build> -L input`).

## Test counts (authoritative baseline)

Recorded **2026-10-31** in `cmake-build-headless`: Debian 13, g++ 14.2.0, CMake 3.31.6 and
Ninja 1.12.1. This is a current local headless baseline, not a claim that physical hardware or every
renderer was exercised.

**Pinned versions (INPUT-DOC-014 / INP-0196).** Reference toolchain as above (g++ 14.2.0 / CMake 3.31.6 /
Ninja 1.12.1, Debian 13).

The **SDL** dependency is the `third_party/SDL` git submodule (`libsdl-org/SDL`), pinned at commit
**`86296ac8f01597076979630667472a846b04ce46`** (alongside `SDL_mixer`);
`git submodule update --init` restores it. **Tag lookup:**
`git -C third_party/SDL describe --tags` = **`release-3.4.0-752-g86296ac8f`** — 752 commits past the
`release-3.4.0` tag on SDL `main`, self-reporting as SDL **3.5.0** (in-development). It is **not** a tagged
release; the nearest **stable release tags** are the `release-3.4.x` line (latest **`release-3.4.18`**).

**Minimum SDL3 API relied upon:** standard SDL3 gamepad and raw joystick APIs (including hotplug,
axes/buttons/hats/trackballs and joystick-to-haptic correlation), keyboard
(scancode/keycode), mouse (relative mode, warp), touch (`SDL_EVENT_FINGER_*`), text-input, and gamepad
sensor/rumble/trigger-rumble — all present since SDL **3.2 / 3.4.0**, so a `release-3.4.x` build has
everything the input layer uses.

> **Before bumping to the tag:** the build does **not** compile SDL from the submodule directly —
> `CNA_SDL_PREBUILT_ROOT` points at a shared **`.sdl-prebuilt`** cache (currently SDL 3.5.0-dev, built from
> the commit above and shared by every `cmake-build-*` dir + all renderers). Moving the submodule to
> `release-3.4.8` is a *deliberate* infra step: it also needs the shared prebuilt rebuilt
> and `ctest -L input` re-run on the current CI renderer matrix to confirm the
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
does `git submodule update --init` + clones the `sharp-runtime`/`easy-gl` siblings, configures,
builds `cna_input_test_gate`, checks both generated input reports, and runs
`xvfb-run -a ctest -L input`. Its matrix has two renderer identities (`OPENGLES3`, `SDLGPU`) plus an
ASan+UBSan `OPENGLES3` job.

To reproduce locally in a throwaway clone (siblings present next to it):

```bash
git clone <cna-url> /tmp/cna-fresh && cd /tmp/cna-fresh
git submodule update --init
cmake -S . -B build -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON
cmake --build build --target cna_input_test_gate --parallel 8
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir build -L input --output-on-failure
```

If a dependency is missing, the configure step fails fast with a copy-pasteable remedy (INPUT-BUILD-005):
a missing `third_party/SDL*` submodule prints `Run: git submodule update --init`; a missing
`sharp-runtime`/`easy-gl` sibling prints the exact `git clone … <path>` command (and, for `easy-gl`, the
option to pick another renderer). Verified in `cmake/ThirdPartySDL.cmake` and `CMakeLists.txt`.

| Metric | Count |
|--------|-------|
| `CnaInputTests` / `CnaInputModuleTests` | **563 tests**: 560 passed, 3 environment skips; shuffle ×5 clean |
| `CnaInputCrossModuleTests` | **70 passed**, 0 skipped/failed; shuffle ×5 clean |
| Canonical input label total | **633 distinct tests per iteration**, 630 passed + 3 environment skips; both lanes green |
| Full `CnaTests` suite (unfiltered) | not rerun for this input-only update; the historical P9-031 crash is fixed — see below |

Notes:
- The former **524-test** figure was a dated 2026-07-17 count from one broad name filter. By
  2026-10-31 that filter selected 624 tests, including unrelated methods merely containing words such
  as `Touching`. The module-focused executable plus explicit cross-module lane replace that unstable
  count; no input-owned suite is selected by name anymore.
- The three HEADLESS skips are two clipboard round trips (no clipboard service) and the SDL3 relative
  mouse-mode real-window case. The X11 CI lanes provide the corresponding services.
- **The full-suite crash P9-031 recorded on 2026-07-17 is fixed.** Running the whole unfiltered
  `CnaTests` binary in one process aborted with `double free or corruption (fasttop)` beside two
  `ENetBackendTest` cases that explicitly disposed their session. Their fixture disposed it again
  during destruction; the tested `feature/input` lineage did not contain sibling-branch commit
  `dd4f8a3cc` (2026-07-16), whose AddressSanitizer-confirmed fix makes `NetworkSession::Dispose()`
  idempotent and clears its non-owning gamer views. The isolated cases could pass because the second
  disposal was undefined behavior whose manifestation depended on the preceding allocation history.
  Current verification on 2026-10-31 passed all 565 `CnaNetTests`, plus 100 shuffled repetitions of
  both original cases and the direct double-dispose regression (300/300). `e140c3531` (AM4-322) and
  `b80f5bb0a` (AM4-323) fixed two separate, later lifetime defects found on 2026-10-09; neither caused
  the July report. The 2026-07-16 full-suite figure (4623 passed / 20 failed / 2 skipped) is a dated
  record, not today's count.
- The 20 pre-2026-07-17 full-suite failures (before the crash was discovered) were pre-existing and
  unrelated to Input: all 20 were in the XNB/Content/Model/Effect/Texture3D pipeline
  (`"SDL_Renderer does not support 3D: CreateVertexBuffer"`, a documented limitation of the
  since-retired `SDL_RENDERER` renderer), not an input regression.
- **This table is the single source of truth for input test counts.** Other docs reference it rather than
  restating numbers. Re-run and update it whenever input tests are added.

## Input verification checklist (task 884)

1. `git submodule update --init` completes clean.
2. `../sharp-runtime` and (for EasyGL) `../easy-gl` exist.
3. Configure succeeds with `-DCNA_BUILD_TESTS=ON`.
4. `cna_input_test_gate` builds with no errors.
5. Full `CnaTests` run is green.
6. Both input-label lanes are green.
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
  that stays manual/hardware-gated. See the input plan's
  Phase I15 (Git history) and `docs/input-manual-verification-results.md`.
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
| CMake configure fails: `Missing vendored 'SDL' …` | submodules not initialized | `git submodule update --init` |
| CMake configure fails: `required sibling repository 'sharp-runtime' … not found` | sibling repo not checked out next to `cna` | `git -C .. clone <sharp-runtime-url> sharp-runtime` |
| CMake configure fails: `the OPENGLES3 renderer requires … 'easy-gl'` | `easy-gl` sibling missing | clone it next to `cna`, or use `-DCNA_GRAPHICS_RENDERER=SDLGPU` (or `SOFTWARE`) |
| `ctest -L input` fails on 3 `MouseCursor` cases | headless `SDL_VIDEODRIVER=dummy` (null cursors) | run under a display: `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest -L input` |
| ASan reports leaks in `libGLX_mesa` | third-party Mesa GLX at process exit (not CNA) | run with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` (what CI uses) |
| Gamepad panels stay "disconnected" in `demo_input` | no controller / SDL can't map the device | attach a controller SDL knows (see `gamecontrollerdb`); Steam Input presents a virtual pad |
| `SDL_GetGlobalMouseState` returns `(0,0)` | Wayland compositor security policy | force `SDL_VIDEODRIVER=x11`/XWayland, or use relative mouse mode |
