<!-- SPDX-License-Identifier: MS-PL -->
# Building and Testing CNA Input

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
# (CNA_INPUT_TEST_FILTER in CMakeLists.txt) shuffled x3 for order-independence. This is what CI runs.
ctest --test-dir cmake-build-input-easygl -L input --output-on-failure
```

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

| Metric | Count |
|--------|-------|
| Full `CnaTests` suite | **3286 passed / 2 skipped** |
| Canonical input filter (the filter above) | **297 passed** |

Notes:
- The 2 skipped tests are Devices sensor tests (`AccelerometerTests` / `GyroscopeTests`
  `GetCurrentValuePropertyDoesNotThrowWhenSupported`) — **not** input.
- The canonical filter's last four tokens (`*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*`) were
  added 2026-07-05 to catch the pure-enum value suites and the public-API header-hygiene suite that the
  older filter missed; without them the same run reports **290**.
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
