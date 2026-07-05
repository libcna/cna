# NEXT.md — CNA Handoff

> Concise handoff for resuming work later (Claude Code or a human). Based only on the current repo
> state, recent changes, and the last real build/test run. No invented features.
>
> **Two active tracks share this repo.** This document's active context is the **Input** track
> (current branch `feature/input-stabilization`). The **Graphics** track lives on `develop` and has
> its own, larger backlog in **`GRAPHICS_TASKS.md`** — including the repo's most severe open bug
> (see §4). This NEXT.md does not replace `GRAPHICS_TASKS.md`.

## 1. Project summary

- **What:** **CNA** is a C++23 reimplementation of the **XNA 4.0** programming model
  (`Microsoft::Xna::Framework`), on **SDL3** with a pluggable 3D backend layer (EasyGL/OpenGL ES,
  Vulkan, Bgfx, SDL_Renderer). Framework/runtime, not a game.
- **Main goal:** full XNA 4.0 API coverage with FNA-faithful behavior, backed by unit + pixel-readback
  tests, verified against the FNA reference (`/rv/data/library/github.com/FNA-XNA/FNA/src`).
- **Current phase (this branch):** Input stabilization on `feature/input-stabilization` (off
  `develop`). Done here: **Phase I13/I14** (FNA-fidelity fixes, SDL edge cases, test determinism),
  **Phase I15** (injectable SDL gamepad seam + device-level tests), and an explicit
  **gamepad-subsystem startup-init** cleanup. `develop` already has the earlier `feature/input` and
  `feature/graphics` merges.
- **Key architectural decisions:**
  - FNA is the authoritative behavioral reference.
  - Input is **event-driven**: `SdlInputBridge::ProcessEvent` → `InputManager` (+ `TouchPanel`) →
    `Get*State()`. (FNA is poll-based; CNA updates state at SDL-event time.)
  - Non-XNA members inside `Microsoft::Xna` are tagged `NOXNA`; `.NET` types live in `sharp-runtime`.
  - SDL gamepad calls go through an internal injectable seam (`ISdlGamepadBackend`) so gamepad runtime
    behavior is unit-testable with a fake — never exposed in the XNA public API.

## 2. Current status

- **Input branch build:** clean on **all 4 backends** in this checkout (Debian 13). 3-backend gap CLOSED
  on 2026-07-05 (plan task INPUT-BUILD-002): EasyGL, Vulkan, bgfx, and SDL_RENDERER each build `CnaTests`
  clean and pass the input filter, order-independent under shuffle — input is confirmed backend-agnostic.
- **Input tests (last run, 2026-07-05):** full `CnaTests` **3269 pass / 2 skipped** (EasyGL; the 2 skips
  are Devices sensor tests, not input); canonical input filter **280** (base **274**), identical on all 4
  backends; order-independent under `--gtest_shuffle --gtest_repeat=3`. **Authoritative counts + the
  canonical filter string live in `docs/input-build-and-test.md` (§Test counts)** — cite that, don't
  restate. (Earlier 2234/257/259 numbers were stale.)
- **Available artifacts:** the `CNA` library; the `CnaTests` GoogleTest binary; examples under
  `examples/` (`demo_input`, `input_smoke`, and many graphics `easygl_*`/`vulkan_*`/`bgfx_*` samples).
  Backend chosen at configure time via `-DCNA_GRAPHICS_BACKEND=`.
- **Recently implemented & working (input):** injectable SDL gamepad seam + fake backend; central
  `InputManager::ResetAllForTests()`; six real input bug fixes (see §3); explicit gamepad-subsystem
  init at startup with a lazy fallback.
- **Graphics track status (on `develop`, for context):** Phases 1–37 complete per `GRAPHICS_TASKS.md`,
  but with **known unfixed backend bugs** (Task 870/871/872 — see §4/§5). Not verified this session.
- **Does NOT work / not headless-verifiable (input, not code gaps):** real gamepad **actuation**
  (motor spinning, trigger haptics, live sensor values, real OS hot-plug/GUID); real **IME**
  composition; **Wayland** OS-cursor-landing readback (X11-only). All documented as manual/hardware-gated.

## 3. Recent changes (input track, most recent first)

- **Explicit gamepad startup init (uncommitted as of writing → being committed now):**
  `Game::DoInitialize()` calls `SdlInputBridge::EnsureGamepadSubsystemInitialized()` once (after
  graphics-device creation, before the first event pump/`Update`); the lazy call in `ProcessEvent`
  stays as a fallback. Files: `src/Microsoft/Xna/Framework/Game.cpp`,
  `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp` (`SdlGamepadSubsystemInit` test),
  `docs/input-fna-fidelity.md`, `docs/input-build-and-test.md`.
- **Phase I15 (`ef99208`):** `ISdlGamepadBackend` seam + `RealSdlGamepadBackend`
  (`include|src/CNA/Internal/Input/SdlGamepadBackend.hpp|.cpp`); routed all SDL gamepad/joystick calls
  in `SdlInputBridge.cpp` through it; `FakeSdlGamepadBackend.hpp` + 20 device-level tests
  (`SdlGamepadBackendTests.cpp`): hot-plug/slots, `FNA_GAMEPAD_NUM_GAMEPADS`, all 21 button mappings,
  axis Y-inversion/trigger normalization, capabilities, rumble/LED/sensor, GUID.
- **Phase I13/I14 (`4723576`):** 6 real bug fixes (§5), central `ResetAllForTests()`, new docs
  (`input-build-and-test.md`, `input-fna-fidelity.md`), CMake sibling-repo guards.

## 4. Current blocker / main problem

**The input branch has no hard blocker** — it builds and every test passes on **all 4 backends**; no
failing command or test. The former **`needs verification`** 3-backend gap is now CLOSED (2026-07-05,
plan task INPUT-BUILD-002: EasyGL/Vulkan/bgfx/SDL_RENDERER all green, input filter green, shuffle-stable).
Remaining open input-track items: (a) a human merge-vs-continue decision; (b) the larger `plan_input.md`
backlog (265 tasks) — next up are sanitizer run (INPUT-BUILD-006), fresh-clone repro (INPUT-BUILD-001),
DragComplete tests (INPUT-GESTURE-007), and the open design decisions (DEC-04/09/15/21).

**The repo's most severe open problem is on the Graphics track (`develop`), NOT input** — recorded so
it is not forgotten:

- **`confirmed bug` — Task 870:** Vulkan `DepthStencilState` is largely non-functional — stencil
  testing is effectively bypassed and `DepthBufferFunction` is ignored (found via genuine contrast
  tests, reconfirmed ~5×). **Not fixed.**
- **`confirmed bug` — Task 871:** `GraphicsDevice::Clear` ignores `ClearOptions::Stencil`.
- **`confirmed bug` — Task 872:** `GraphicsDevice.ReferenceStencil` independent-override has no
  backend wiring on all 3 backends.
- Symptom/repro + full analysis: see `GRAPHICS_TASKS.md` (Tasks 868–872) and
  `docs/depthstencilstate-support.md`. These are **owned by the Graphics track**, not this input
  branch — do not fix them here without switching context deliberately.

## 5. Known bugs and limitations

**Input (this branch):**
- `incomplete` / by-design — real-hardware gamepad **actuation** is not headless-verifiable (fake
  proves translation/bookkeeping only); `needs verification` on real controllers.
- `incomplete` — exhaustive **gesture matrix** (tap/double-tap/hold/drag/flick/pinch × interruption)
  only partially covered (`plan_input.md` task 906).
- Documented **intentional FNA deviations** (not bugs — see `docs/input-fna-fidelity.md`):
  `FNA_GAMEPAD_NUM_GAMEPADS` clamped to 4; `PacketNumber` per-field/event-driven (within-dead-zone
  wobble can bump it — `not asserted` in tests, task 916); `GamePadState::GetHashCode` partial-field;
  single-subscriber `ClickedEXT`/`TextInput`/`TextEditing`; `TextEditing` UTF-8; malformed UTF-8
  skipped vs U+FFFD; `MaximumTouchCount=8` reported vs XNA's 4 and uncapped event-driven touch path;
  GUID/caps live vs FNA cached-at-connect.
- `suspected` (cannot occur via CNA's own API) — Mouse relative-mode cache could desync if SDL
  relative mode is toggled outside `Mouse::setIsRelativeMouseModeEXTProperty`.
- `incomplete` (decision open) — no input-state clear on `WINDOW_FOCUS_LOST` (matches FNA, but
  event-driven CNA can leave a key/button stuck; a clear would be *beyond* FNA — task 951).

**Graphics (on `develop`, for awareness):** Task 870/871/872 (see §4) — `confirmed bug`, unfixed.

**Housekeeping:** working tree carries an unrelated `D .claude/settings.json` (never commit); build
dirs `cmake-build-input-*` are gitignored.

## 6. Architecture notes

- **Input modules:**
  - `src/CNA/Internal/Input/SdlInputBridge.cpp` — single funnel: SDL events → `InputManager` /
    `TouchPanel` / `Mouse` / `TextInputEXT`. Owns gamepad slot maps, finger-id map, env-count parse,
    and the gamepad-subsystem init.
  - `src/CNA/Internal/Input/InputManager.cpp` — accumulated input state singleton (keyboard / mouse /
    4 gamepads / touch) + `Get*State()` snapshots + `ResetAllForTests()`.
  - `src/CNA/Internal/Input/GestureDetector.cpp` — gesture state machine (has a test clock).
  - `include|src/CNA/Internal/Input/SdlGamepadBackend.*` — `ISdlGamepadBackend` seam + real impl.
  - `Microsoft/Xna/Framework/Input/**` — the XNA-facing API.
- **Data flow:** host loop → `Game::PollEvents` → `SdlInputBridge::ProcessEvent(event)` → mutate
  `InputManager`/`TouchPanel` → game reads `GamePad::GetState` / `Keyboard::GetState` / etc.
- **Invariants (keep true):** input state updates at event-processing time; the gamepad subsystem is
  initialized once at startup (`Game::DoInitialize`) + lazily in `ProcessEvent` (both idempotent);
  `ResetAllForTests()` returns all input statics to a deterministic baseline (tests must stay
  order-independent); production gamepad path uses the **real** SDL backend (the fake is test-only,
  restored to real by the central reset).
- **Boundaries / rules that must stay stable:**
  - Do **not** expose `ISdlGamepadBackend` (or any `CNA::Internal` type) in the XNA API.
  - Do **not** expand the XNA public API; XNA/FNA names, signatures, enum values are frozen.
  - Match **FNA behavior**; document any intentional deviation in `docs/input-fna-fidelity.md`.
  - `sharp-runtime` and `easy-gl` are **sibling repos** (not submodules); the SDL family are submodules.

## 7. Useful commands

```bash
# Configure + build tests (EasyGL; swap backend for VULKAN / BGFX / SDL_RENDERER):
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-input-easygl --target CnaTests

# Run everything:
./cmake-build-input-easygl/CnaTests

# Input-only tests, canonical way (INPUT-BUILD-003) — runs the single-source-of-truth filter
# (CNA_INPUT_TEST_FILTER in CMakeLists.txt) shuffled x3; this is what CI runs. Order-independence
# is therefore covered by the same command.
ctest --test-dir cmake-build-input-easygl -L input --output-on-failure

# Focused device-level gamepad tests:
./cmake-build-input-easygl/CnaTests --gtest_filter='FakeGamepad*:FakeGamepadEnvCount*:FakeGamepadGuidFormat*:SdlGamepadSubsystemInit*'

# 3-backend verification (the §4 input gap): repeat configure+build+run with VULKAN and BGFX.
```
No lint/format config for input. There is **no failing input command to reproduce** — §4's input item
is a verification gap, not a failure. Graphics bugs (§4) reproduce via the pixel tests referenced in
`GRAPHICS_TASKS.md` / `docs/depthstencilstate-support.md`.

## 8. Next smallest tasks (input track, ordered)

1. ~~**3-backend verification of this branch.**~~ **DONE 2026-07-05 (INPUT-BUILD-002).** Vulkan, bgfx, and
   SDL_RENDERER all build `CnaTests` clean and pass the input filter (280 canonical), shuffle-stable → all 4
   backends green; input confirmed backend-agnostic. See `plan_input.md` INPUT-BUILD-002 for the recorded
   result. The authoritative task backlog is now `plan_input.md` (this NEXT list is a short pointer).
2. **Assert `PacketNumber` does not bump on a within-dead-zone axis wobble (or test the documented gap).**
   - Goal: close the `not asserted` note in task 916.
   - Files: `tests/Microsoft/Xna/Framework/Input/GamePad*Tests.cpp` (+ `FakeSdlGamepadBackend` if via events).
   - Verify: `CnaTests --gtest_filter='*GamePad*:*FakeGamepad*'`.
3. **Decide + implement focus-loss handling (task 951).**
   - Goal: add a `SDL_EVENT_WINDOW_FOCUS_LOST` transient-clear (documented as beyond-FNA) **or** record
     the explicit decision to match FNA with a test of current behavior.
   - Files: `src/Microsoft/Xna/Framework/Game.cpp` (PollEvents), `InputManager` (a `ClearTransientState`),
     `docs/input-fna-fidelity.md`.
   - Verify: a new focus-loss test in `tests/**Input**`; input filter green.
4. **Add drag + pinch-interruption gesture regression cases (task 906).**
   - Files: `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp` (uses `ResetAllForTests`).
   - Verify: `CnaTests --gtest_filter='*Gesture*:*Touch*'` (also under shuffle).

## 9. Do not do yet

- **No broad refactor** of `SdlInputBridge`/`InputManager` — keep the single-funnel event design.
- **No XNA public-API changes** (names/signatures/enum values frozen); do **not** expose any
  `CNA::Internal` type (esp. `ISdlGamepadBackend`) in the XNA layer.
- **No behavior changes that diverge from FNA** without checking the FNA source and recording the
  deviation in `docs/input-fna-fidelity.md`.
- **No graphics work on this input branch** — Task 870/871/872 belong to the Graphics track
  (`GRAPHICS_TASKS.md`). Don't fix them here or edit `GRAPHICS_TASKS.md` without switching context.
- **No merge to `master`**, and no merge of this branch to `develop`, without explicit confirmation.
- **No committing** `D .claude/settings.json`, `vendor/wgpu-native/`, or `cmake-build-*` dirs.
- **No new large abstractions** for the hardware-gated items — they are manual/hardware, not missing code.

## 10. Resume prompt

```
Read NEXT.md first. Then, working on branch feature/input-stabilization:
- Inspect ONLY the files needed for the first task in NEXT.md §8 (do not read the whole tree).
- Do NOT refactor unrelated code, do NOT expand the XNA public API, and do NOT touch the Graphics
  track (GRAPHICS_TASKS.md / Task 870–872) unless explicitly asked.
- Make ONE small, verified improvement (start with §8 task 1: the 3-backend verification).
- Run the relevant build/test command from NEXT.md §7 and confirm it passes (input filter must stay
  green; input tests must stay order-independent under --gtest_shuffle).
- Update NEXT.md (§2 status, §3 recent changes, §8 task list) after finishing, then stop and report.
Do not merge anything without explicit confirmation.
```
