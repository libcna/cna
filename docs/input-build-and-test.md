<!-- SPDX-License-Identifier: MS-PL -->
# Building and Testing CNA Input

> **Related input docs (INP-0003):** [plan](../plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

This document explains how to build and run the CNA `Input` unit tests from a **complete** checkout,
and — importantly — what **cannot** be verified from a source-only archive or in a headless CI
environment. (Phase I13/I14, tasks 883/884.)

## What a complete checkout needs

CNA input depends on three kinds of external code:

| Dependency | How it is obtained | Notes |
|---|---|---|
| `third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer` | **git submodules** of this repo | `git submodule update --init --recursive` |
| `vendor/googletest` | **git submodule** | test framework |
| `../sharp-runtime` | **sibling repository** (NOT a submodule) | CNA's C++ `System.*` runtime; must sit next to `cna_input` |
| `../easy-gl` | **sibling repository** (NOT a submodule) | only for the `EASYGL` backend |

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
git -C .. clone <easy-gl-url> easy-gl        # only needed for -DCNA_GRAPHICS_BACKEND=EASYGL

# 3. System packages (Debian/Ubuntu) for VideoPlayer (unrelated to input, but part of the lib):
sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

## Configure, build, run

The backend does not change input behavior (input is backend-agnostic), so any backend works for
running the input tests. EasyGL is the default.

```bash
# Configure with tests enabled (pick ONE backend):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
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

Recorded **2026-07-06** in this checkout: Debian 13, g++ 14.2.0, CMake 3.31.6, Ninja 1.12.1. Input is
backend-agnostic — the input-filter count is identical on EasyGL / Vulkan / bgfx / SDL_RENDERER.

**Pinned versions (INPUT-DOC-014).** Reference toolchain as above (g++ 14.2.0 / CMake 3.31.6 /
Ninja 1.12.1, Debian 13). The **SDL** dependency is the `third_party/SDL` git submodule
(`libsdl-org/SDL`), pinned at commit **`cbe3fbe9f367340dcd924de29c225c9f4ffea1f5`** in this checkout
(alongside the `SDL_image`/`SDL_mixer` submodules); `git submodule update --init --recursive` restores
that exact revision. (Pinning the submodule to a named upstream *tag* rather than a raw commit is tracked
by INPUT-BUILD-004.)

### Headless run inventory (INPUT-BUILD-008)

`ctest -L input` must run under a display server (`xvfb-run` + `SDL_VIDEODRIVER=x11` in CI and on
headless boxes) — a few `MouseCursor`/`SetCursor` cases need real SDL cursors. Behavior of the input
subset by video driver:

| Video driver | MouseCursor/SetCursor cursor-handle cases | Result |
|--------------|-------------------------------------------|--------|
| `x11` (Xvfb or real display) | run | **100% green** |
| `dummy` (fully headless) | **5 GTEST_SKIP** + **3 fail** | not green — do not gate on `dummy` |

- **Skipped under `dummy`** (need a valid `SDL_Cursor` handle to exercise ownership/disposal):
  `MouseCursorTest.DisposeReleasesHandleAndIsIdempotent`, `.MoveConstructorTransfersOwnershipAndNullsSource`,
  `.MoveAssignmentDisposesPreviousHandleAndTransfersOwnership`, `.NonOwningConstructorDoesNotDestroyCursorOnDestruction`,
  and `MouseTest.SetCursorIsSafeNoOpForDisposedCursor`.
- **Fail under `dummy`, pass under `x11`** (stock/default cursor creation returns null on the dummy
  driver): `MouseCursorTest.StockCursorsAreNonNullWhenVideoAvailable`, `.DisposingAStockSingletonIsANoOpAndKeepsItUsable`,
  `.DefaultConstructorCreatesNonNullOwningCursor`.

So the always-portable input count is stable; only these display-dependent cursor cases vary by driver,
which is why CI standardizes on `xvfb-run … SDL_VIDEODRIVER=x11`.

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
cmake -S . -B build -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build build --target CnaTests
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir build -L input --output-on-failure
```

If a dependency is missing, the configure step fails fast with a copy-pasteable remedy (INPUT-BUILD-005):
a missing `third_party/SDL*` submodule prints `Run: git submodule update --init --recursive`; a missing
`sharp-runtime`/`easy-gl` sibling prints the exact `git clone … <path>` command (and, for `easy-gl`, the
option to pick another backend). Verified in `cmake/ThirdPartySDL.cmake` and `CMakeLists.txt`.

| Metric | Count |
|--------|-------|
| Full `CnaTests` suite | **3303 passed / 2 skipped** |
| Canonical input filter (the filter above) | **314 passed** |

Notes:
- The 2 skipped tests are Devices sensor tests (`AccelerometerTests` / `GyroscopeTests`
  `GetCurrentValuePropertyDoesNotThrowWhenSupported`) — **not** input.
- The canonical filter's last four tokens (`*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*`) were
  added 2026-07-05 to catch the pure-enum value suites and the public-API header-hygiene suite that the
  older filter missed; without them the same run reports **294**.
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
  rumble/LED/sensor support, GUID formatting) IS headless-tested via an injectable fake SDL backend
  (`ISdlGamepadBackend` / `FakeSdlGamepadBackend`, `*FakeGamepad*` tests). What the fake cannot prove
  is that the physical device *acts* — that stays manual/hardware-gated. See `plan_input.md`
  (Phase I15) and `docs/input-manual-verification-results.md`.
  - **Startup invariant:** the SDL gamepad subsystem is initialized explicitly in
    `Game::DoInitialize()` — once, before the first event pump and the first `Update()` — via
    `SdlInputBridge::EnsureGamepadSubsystemInitialized()`, with a defensive lazy call still in
    `SdlInputBridge::ProcessEvent()`. So gamepads connected before the first frame are enumerated
    from startup (the fake test `SdlGamepadSubsystemInit.*` checks the idempotent init primitive;
    the pre-connected-visibility path is covered by `FakeGamepadTest.PadConnectedBeforeFirstFrame*`).
- **IME / composition** — real `TextEditing` composition, cursor, and selection over a physical IME.
- **Wayland OS-cursor landing** — `SDL_GetGlobalMouseState` is compositor-restricted, so the absolute
  landing pixel of `Mouse::SetPosition` is only readable under X11.

See `docs/input-fna-fidelity.md` for the per-area FNA-fidelity status and the full list of intentional
deviations.
