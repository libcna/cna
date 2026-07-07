# NEXT.md — CNA Input Handoff

> Concise handoff for resuming work in a fresh context (Claude Code or a human). Based only on the
> current repo state, recent commits, and this session's build/test runs. No invented features.
>
> **Active branch: `feature/input`.** Two backlogs have run on it, in order:
> 1. **`plan_input.md`** — the XNA 4.0 Input **completion/hardening** pass (Phases 0–12). **COMPLETE**
>    (automated scope; only Phase 11 manual-hardware `[!]` remains). Merge-ready, not merged.
> 2. **`input_noxna.md`** — the **NOXNA/SDL3 extension** pass: surface SDL3 input capability *beyond*
>    XNA 4.0 as `NOXNA`/`EXT` members and new `CNA::Input` types. **← THIS IS THE ACTIVE WORK.**
>    Live tracker + per-task log: **`input_noxna_progress.md`** (repo root). Analysis/design doc:
>    **`input_noxna.md`** (repo root).
>
> **As of 2026-07-07:** 20 of the input_noxna.md tasks are done (one commit each, all pushed).
> **N-012 Pen was cancelled by the owner** (explicit scope cut, not an engineering conflict) — it will
> not be implemented. What remains is exactly **one** task: N-013 Haptics (see §6/§8). Branch in sync
> with `origin/feature/input` at `8e99997c`.

## 1. Project summary

- **What:** **CNA** is a C++23 reimplementation of the **XNA 4.0** programming model
  (`Microsoft::Xna::Framework`) on **SDL3**, with a pluggable 3D backend layer (EasyGL/OpenGL ES,
  Vulkan, bgfx, SDL_Renderer). A framework/runtime, not a game.
- **FNA reference** (authoritative XNA behavior — its *code*, not its comments):
  `/rv/data/library/github.com/FNA-XNA/FNA`. NOXNA extensions have no FNA counterpart; for those, SDL3
  is the reference.
- **Input is event-driven:** `SdlInputBridge::ProcessEvent` → `InputManager` (+ `TouchPanel`) →
  `Get*State()`. Public XNA API must NEVER expose SDL or `CNA::Internal` types.
- **NOXNA rule:** anything inside `Microsoft::Xna` that is not XNA 4.0 carries the `NOXNA` macro and
  usually an `EXT` name suffix. Brand-new extension types live in the public `CNA::Input` namespace
  (`include|src/CNA/Input/`), whole class marked `NOXNA`. `.NET`-style helpers live in the sibling
  repo `sharp-runtime` (`System::MulticastAction<...>`, aliases, etc.).

## 2. Current status (input_noxna.md pass)

- **Build:** clean. Every task this pass builds on **EasyGL** (`cmake-build-input-easygl`) and the
  **ASan** build (`cmake-build-input-asan`); `ctest -L input` = 100% green after each.
- **20 tasks DONE** (each = its own commit, tested, pushed). See §3 for the commit list and
  `input_noxna_progress.md` for the full per-task log. Grouped:
  - **GamePad EXT (poll via `ISdlGamepadBackend`):** N-009 player-index · N-009b battery/power (shared
    `PowerStateEXT`) · N-010 metadata (name/path/serial/firmware/steam-handle) · N-010b connection-state
    · N-011 button-labels · N-008 touchpad fingers.
  - **Keyboard EXT:** N-003 `GetModStateEXT` (+ `KeyModifiersEXT` flags) · N-002 scancode-name helpers ·
    N-002b keycode-name helpers.
  - **Mouse / Touch EXT:** N-005 horizontal scroll-wheel · N-006 `TouchLocation::getPressureEXT` ·
    N-016 capture + global-position/warp.
  - **New `CNA::Input` types:** N-001 `Clipboard` · N-018 `Power` · N-015 `Sensors` · N-017
    `InputDevices` (enumeration) · N-017b InputDevices hot-plug events · N-007 `Joysticks` (raw
    joystick, its own `ISdlJoystickBackend` seam separate from the gamepad seam).
  - **Text:** N-014 `TextInputEXT::TextEditingCandidatesEXT` (IME candidate list) · N-014b
    `StartTextInputWithTypeEXT` (input-type hints).
- **Skipped:** **N-004** (Mouse cursor-visibility EXT) — would conflict with the existing
  `Game::IsMouseVisible` path (which already calls `SDL_ShowCursor`). Engineering decision, not an
  owner question. Recorded in `input_noxna_progress.md`.
- **Cancelled:** **N-012 `CNA::Input::Pen`** (stylus support) — explicit owner request (2026-07-07),
  not an engineering conflict. Will not be implemented in this pass.
- **Not headless-verifiable (by design):** real gamepad/joystick/haptic **actuation**, real **IME**
  composition, live **sensors**, OS **hot-plug** on physical hardware. The injectable seams + fakes
  prove translation/plumbing; real-device wiring is manual `[!]`.

## 3. This session's commits (most recent first, all on `feature/input`, all pushed)

```
8e99997c input(N-007):  CNA::Input::Joysticks raw-joystick access
95db2319 input(N-014b): TextInputEXT input-type hint EXT
f92dc71a fix(GamerServices): follow sharp-runtime's RegionInfo::CurrentRegion rename
462ca465 input(N-014):  TextInputEXT IME candidate-list event
8096705d input(N-017b): InputDevices mouse/keyboard hot-plug events
653915bc input(N-008):  GamePad touchpad-finger EXT (poll-based)
2194b7b9 input(N-015):  CNA::Input::Sensors host-device motion sensors
913276e5 input(N-016):  Mouse capture + global-position/warp EXT
5744fbbe input(N-017):  CNA::Input::InputDevices enumeration
4931b85c input(N-002b): Keyboard keycode-name EXT helpers
a88cd367 input(N-002):  Keyboard scancode-name EXT helpers
d1a9082e input(N-003):  Keyboard::GetModStateEXT + KeyModifiersEXT flags
978315ea input(N-006):  TouchLocation finger-pressure EXT
f702aa54 input(N-018):  CNA::Input::Power system battery extension
3a0c362c input(N-010b): GamePad connection-state EXT (wired/wireless)
fc9836c8 input(N-010):  GamePad device-metadata EXT getters
67b6269b input(N-011):  GamePad button-label EXT (glyph per controller type)
d9c6c77a input(N-009b): GamePad battery/power EXT + shared PowerStateEXT enum
a0325dc8 input(N-009):  GamePad player-index EXT via injectable gamepad seam
d6aa0e23 input(N-005):  Mouse horizontal scroll wheel EXT
ca88dd23 input(N-001):  CNA::Input::Clipboard — SDL3 clipboard text
```

## 4. Reusable patterns (follow these — the remaining task fits one)

**Pattern 1 — GamePad-seam extension (poll-based).** For "more of an existing gamepad concept."
1. Add a `virtual` to `ISdlGamepadBackend` (`include/CNA/Internal/Input/SdlGamepadBackend.hpp`); real
   impl = the SDL call (`src/.../SdlGamepadBackend.cpp`); fake impl reads a new
   `FakeGamepadConfig` field + records calls (`tests/CNA/Internal/Input/FakeSdlGamepadBackend.hpp`).
2. `SdlInputBridge::X(PlayerIndex, …)` resolves the slot (`get_sdl_gamepad_for_player`) and returns a
   safe default (0 / false / Unknown, out-params reset) when disconnected.
3. Public `GamePad::XEXT(...)` (NOXNA) delegates to the bridge.
4. Pin in `PublicApiInputSignatureFreezeTests.cpp` + document in `docs/input-public-api-frozen.md`.
5. Tests in `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp` (fixture `FakeGamepadTest`).
Examples: N-008/009/009b/010/010b/011.

**Pattern 2 — NOXNA member on a frozen XNA type.** For adding data XNA dropped (e.g. pressure).
- Keep the new field OUT of `Equals`/`GetHashCode`/`ToString` (those stay FNA-frozen —
  `has_frozen_equality` still holds); add NOXNA **constructor overload(s)**, not a public setter.
- Pin the getter + new ctors in the freeze test + document, SAME commit.
Examples: N-005 (`MouseState` horizontal wheel), N-006 (`TouchLocation` pressure).

**Pattern 3 — Standalone `CNA::Input` type + injectable *system* seam.** For host-level features.
- New `ISystemXBackend` (SDL-typed, internal) in `include|src/CNA/Internal/Input/SystemXBackend.*`,
  real = SDL, plus `SetSystemXBackendForTests(...)`. Public type/methods in `include|src/CNA/Input/`.
- **Whole-class-NOXNA `CNA::Input` types are ADDITIVE → NO freeze pin** (like `Clipboard`). Test
  suites MUST be named `CnaInput*` (matched by `CNA_INPUT_TEST_FILTER`). Tests drive the fake.
- Seams already built this way: `ISystemPowerBackend` (N-018), `ISystemKeyboardBackend` (N-003
  mod-state), `ISystemDeviceBackend` (N-017), `ISystemMouseBackend` (N-016), `ISystemSensorBackend`
  (N-015). Mirror any of them for a new one.

**Pattern 4 (N-007 Joystick) — standalone type + its OWN device-scoped seam, hot-plug through the
bridge.** For a raw-device subsystem that needs an *open handle* (not just a stateless system query)
and has its own hot-plug lifecycle independent of gamepad slots.
- New `ISdlJoystickBackend` (`include|src/CNA/Internal/Input/SdlJoystickBackend.hpp/.cpp`) —
  deliberately SEPARATE from `ISdlGamepadBackend` even though both wrap `SDL_Joystick*`; a gamepad is
  a *mapped* view of the same device, this is the raw one, and both may be opened independently.
- `SdlInputBridge` opens every device on its own `_ADDED` event into a plain
  `std::unordered_map<SDL_JoystickID, SDL_Joystick*>` (no slot/player-index concept needed — unlike
  gamepad, XNA has no 4-controller constraint here), closes on `_REMOVED`, and fires the public type's
  `Connected/DisconnectedEXT` directly (mirrors N-017b's direct-invoke style, no `INTERNAL_On` needed).
- **No handling needed for per-frame motion events** (axis/button/hat/ball): SDL's own event pump
  already updates its internal per-device state cache regardless of what the bridge's switch does with
  a dequeued event, so state getters just poll the seam live on demand (same principle as Pattern 1's
  gamepad EXT metadata getters) — don't add motion-event cases unless you have a concrete reason to.
- Public type is whole-class NOXNA and additive (no freeze pin), same as Pattern 3.

**Enums / flags** live in their own `include/CNA/Input/*.hpp` headers (`PowerState`, `KeyModifiers`
w/ constexpr bit-ops like `Buttons`, `GamePadButtonLabel`, `GamePadConnectionState`, `SensorType`,
`JoystickType`, `JoystickHatPosition`).
**Events** use `System::MulticastAction<Args...>` (`sharp-runtime`) fired from the bridge via an
`INTERNAL_On…` dispatcher (see N-014 candidates) or a direct `.Invoke(...)` call for simple hot-plug
id-only events (see N-017b, N-007).

## 5. Per-task discipline (do this for EVERY task — do not batch)

1. Implement one task (one `N-xxx`). If a task is large, split it (e.g. N-009→N-009b) — still one
   commit each. NEVER bundle unrelated tasks.
2. Build: `ninja -C cmake-build-input-easygl CnaTests`.
3. Test: `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input`
   (must be 100%). Run your new tests directly by `--gtest_filter` too.
4. ASan: `ninja -C cmake-build-input-asan CnaTests` + run your new tests under it.
5. If the API surface changed on a **frozen/EXT-on-XNA** member → update the signature-freeze test +
   `docs/input-public-api-frozen.md` in the same commit. (Additive whole-NOXNA `CNA::Input` types
   need neither.)
6. Mark the task `[x]` in `input_noxna_progress.md` and add a Log entry (what/why/files/tests).
7. Commit staging files **by explicit name** (NEVER `git add -A`), message `input(N-xxx): …` with the
   standard trailer (`Co-Authored-By:` + `Claude-Session:`). Then `git push`. **Do NOT merge.**
- **Skip any task that needs an owner decision** (like N-004) — record why in the tracker, move on.
- New source files under `src/**` and `tests/**` are auto-globbed (CONFIGURE_DEPENDS); `ninja` will
  reconfigure. New `CNA::Input` test suites must be named `CnaInput<Type>Test`.

## 6. Remaining tasks

- **N-013 `CNA::Input::Haptics` (SDL_haptic — the only remaining task).** Force-feedback beyond simple
  rumble. SDL: `SDL_haptic.h` — `SDL_OpenHapticFromJoystick`/`SDL_OpenHaptic`, `SDL_CreateHapticEffect`,
  `SDL_RunHapticEffect`, `SDL_UpdateHapticEffect`, `SDL_StopHapticEffect`, `SDL_SetHapticGain`,
  `SDL_SetHapticAutocenter`, effect types constant/periodic/ramp/condition/custom (`SDL_HapticEffect`
  union). Shape: `CNA::Input::Haptics` with an effect-builder + play/stop, behind an injectable seam +
  fake — `N-007`'s new `ISdlJoystickBackend` is a nearby precedent for "own device-scoped seam,
  separate from the gamepad one" if haptic devices need to be opened from a joystick handle
  (`SDL_OpenHapticFromJoystick`). **Real actuation is manual `[!]`** — the fake verifies the
  effect-struct building + call plumbing. Platform: Win/Lin ✓, mac ~, Android/Web ✗.

**Cancelled, not remaining:** N-012 `CNA::Input::Pen` (stylus) — owner cut this from scope
(2026-07-07); do not implement it even opportunistically.

(`input_noxna.md` §6.2 has the full Haptics analysis, platform matrix, and effect-family breakdown.)

## 7. Useful commands

```bash
# Build the input test binary (EasyGL is the primary dev backend for this pass):
ninja -C cmake-build-input-easygl CnaTests
# (First-time configure, if a build dir is missing — swap EASYGL for VULKAN/BGFX/SDL_RENDERER:)
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# THE gate — input subset (baked --gtest_shuffle --gtest_repeat=5), must be 100%:
xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure

# Run your new tests directly:
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-easygl/CnaTests --gtest_filter='FakeGamepadTest.*'

# ASan build + run your subset (after any src/ change):
ninja -C cmake-build-input-asan CnaTests
xvfb-run -a env SDL_VIDEODRIVER=x11 ./cmake-build-input-asan/CnaTests --gtest_filter='<your suite>*'
```
`CNA_INPUT_TEST_FILTER` (CMakeLists.txt ~1952) selects the `-L input` set; it already includes
`*CnaInput*`, `*GamePad*`, `*Keyboard*`, `*Mouse*`, `*Touch*`, `*SdlInputBridge*`, `*PublicApiInput*`,
etc. Name new suites so one of those globs matches.

## 8. Order to resume (input_noxna.md)

1. **Read `input_noxna_progress.md`** (task list + Log + notes) and `input_noxna.md` §6.2 (Haptics).
2. **N-013 Haptics** — the only task left. Effect-builder behind an injectable seam (Pattern 4-style,
   own seam per §4), real actuation `[!]`. Split it further if it grows (e.g. rumble/simple effects in
   one commit, richer effect families in follow-ups) — one task = one commit still applies (§5).
3. Once N-013 lands, the `input_noxna.md` pass is complete (modulo N-012's cancellation and N-004's
   skip) — update NEXT.md's header/status to say so and stop looking for more work here.

## 9. Boundaries / guards that must stay stable

- **Public XNA names/signatures are frozen** — `PublicApiInputSignatureFreezeTests.cpp` + golden
  `docs/input-public-api-frozen.md`. An EXT-on-XNA member must update BOTH in the same commit.
  Whole-NOXNA standalone `CNA::Input` types are additive → not pinned (like `Clipboard`/`Power`).
- **Enum values frozen** (exhaustive enum-value suites); no renumbering.
- **No public header may leak SDL** — `PublicApiInputCompileTests.cpp` `#error` guard. Opaque
  `uintptr_t` / forward-decl only in public headers; SDL types live only in `CNA::Internal`/seams.
- **Do NOT expose** `ISdlGamepadBackend` or any `CNA::Internal` type in the XNA layer.
- **Single-funnel event design** — keep SDL→`SdlInputBridge::ProcessEvent`→`InputManager`; don't
  refactor it broadly.
- **Reset discipline:** anything with process-wide static state (events, seams) needs a
  `ResetForTests`/`SetSystem*BackendForTests(nullptr)` in the test fixture — the `-L input` gate runs
  shuffled ×5, so tests must be order-independent.

## 10. Do NOT do

- **No merge** of `feature/input` to `develop`/`master` without explicit confirmation.
- **No `git add -A` / `git add .`** — stage by explicit name (repo carries unrelated local changes,
  e.g. vendored dirs).
- **No batching** — one `N-xxx` = one commit; skip (don't guess) anything needing an owner decision.
- **No public XNA API change** without updating the freeze test + frozen-API doc in the same commit.
- **No Graphics work here** — the repo's most severe open bug is on the Graphics track (`develop`):
  Task **870** (Vulkan `DepthStencilState` largely non-functional), **871** (`Clear` ignores
  `ClearOptions::Stencil`), **872** (`ReferenceStencil` unwired). See `GRAPHICS_TASKS.md` /
  `docs/depthstencilstate-support.md`. Do NOT fix here.
- **No large abstraction for hardware-gated bits** — real actuation/IME/sensors are manual `[!]`,
  not missing code; the seam+fake proves the plumbing.

## 11. Resume prompt

```
Read NEXT.md, then input_noxna_progress.md. You are continuing the input_noxna.md NOXNA/SDL3 input-
extension pass on branch feature/input (the plan_input.md XNA-completion pass is already done).

- The only remaining task is N-013 Haptics (NEXT.md §6). N-012 Pen was cancelled by the owner — do
  not implement it. Implement N-013 following Pattern 4 in NEXT.md §4 (own device-scoped seam,
  separate from the gamepad/joystick seams), splitting into multiple commits if the effect-family
  surface is too large for one.
- Follow the per-task discipline in NEXT.md §5: build cmake-build-input-easygl + ASan, keep
  `ctest -L input` 100% green (shuffle x5), pin any EXT-on-XNA member in the signature-freeze test +
  docs/input-public-api-frozen.md (a whole-class-NOXNA CNA::Input type needs neither), update
  input_noxna_progress.md ([x] + Log entry), commit `input(N-xxx): ...` with the standard trailer
  staging files by explicit name, then push.
- Respect NEXT.md §9 boundaries and §10 do-nots: no SDL in public headers, no CNA::Internal in the
  XNA layer, no merge, no Graphics work, one task = one commit, split a task if it grows.
- New standalone types go in CNA::Input with a whole-class NOXNA and an injectable seam + fake (see
  the existing SystemPower/SystemMouse/SystemSensor/SystemDevice seams, or the SdlJoystickBackend
  seam for a device-handle-based precedent). New test suites must be named so a CNA_INPUT_TEST_FILTER
  glob matches (e.g. CnaInput*, or add a new token like *Joystick* was added for N-007).
```
