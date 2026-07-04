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

# Run only input tests:
./cmake-build-input-easygl/CnaTests \
  --gtest_filter='*Keyboard*:*Mouse*:*GamePad*:*Touch*:*Gesture*:*TextInput*:*SdlInputBridge*:*InputResetAllForTests*'

# Prove order-independence (Phase I13/I14 task 891) — repeat + shuffle:
./cmake-build-input-easygl/CnaTests \
  --gtest_filter='*Touch*:*Gesture*:*SdlInputBridge*:*Mouse*:*TextInput*:*InputResetAllForTests*' \
  --gtest_shuffle --gtest_repeat=5
```

## Input verification checklist (task 884)

1. `git submodule update --init --recursive` completes clean.
2. `../sharp-runtime` and (for EasyGL) `../easy-gl` exist.
3. Configure succeeds with `-DCNA_BUILD_TESTS=ON`.
4. `CnaTests` builds with no errors.
5. Full `CnaTests` run is green.
6. Input filter run is green.
7. Shuffle + repeat run is green (order-independence).
8. Record the exact counts and any failures (see `plan_input.md` task 959 for the last recorded run).

## What CANNOT be verified headless (honest limitations)

These are **not** missing code — they require real hardware, a real IME, or a specific windowing
environment that the headless test binary does not provide. They are covered by targeted unit tests
of the translation logic only, and are otherwise manual/hardware-gated:

- **Real gamepad runtime paths** — hot-plug, live axis/button/trigger streams, rumble, trigger
  rumble, light bar, gyro/accelerometer, GUID, and live `GamePadCapabilities`. There is currently
  **no fake/injectable SDL gamepad layer** (Phase I13/I14 task 909 is open), so every SDL-device-bound
  gamepad path is exercised only in its disconnected-fallback form. See `plan_input.md` (907–926) and
  `docs/input-manual-verification-results.md`.
- **IME / composition** — real `TextEditing` composition, cursor, and selection over a physical IME.
- **Wayland OS-cursor landing** — `SDL_GetGlobalMouseState` is compositor-restricted, so the absolute
  landing pixel of `Mouse::SetPosition` is only readable under X11.

See `docs/input-fna-fidelity.md` for the per-area FNA-fidelity status and the full list of intentional
deviations.
