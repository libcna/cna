# NEXT.md — CNA Project Handoff (feature/input branch)

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on **SDL3** with a pluggable 3D graphics backend layer
(EasyGL/OpenGL ES, Vulkan, Bgfx, SDL_Renderer). It is a framework/runtime — not a game — so
XNA/FNA game code can be ported to C++ with minimal API-surface changes.

- **Main goal (this branch):** port `Microsoft::Xna::Framework::Input` and `…::Input::Touch`
  from the FNA reference to CNA — not just API surface, but FNA-faithful runtime behavior wired
  to SDL3, CHECKLIST-compliant, and covered by tests. The plan is `plan_input.md` (Phases I1–I7,
  tasks 700–783).
- **Current development phase:** Phases I1–I4 are complete (TextInputEXT; Touch & gesture
  pipeline; GamePad behavior/FNA fidelity; Mouse behavior and MouseCursor). **Phase I5 (Keyboard
  fidelity and SDL key mapping) is nearly complete** — tasks 760–767 of 760–768 are done; only
  task 768 (dedicated test coverage) remains.
- **Key architectural decisions:**
  - The authoritative behavioral reference is the FNA source tree at
    `/rv/data/library/github.com/FNA-XNA/FNA/src`.
  - Non-XNA members inside the `Microsoft::Xna` namespace are tagged with the `NOXNA` marker
    macro. FNA's `EXT`-suffixed additions are non-XNA; a wholly-non-XNA class is tagged
    `NOXNA class`.
  - Real input behavior flows through an internal bridge: SDL3 events →
    `CNA::Internal::Input::SdlInputBridge` → `InputManager` → XNA state objects. Touch
    additionally flows `SdlInputBridge` → `TouchPanel::INTERNAL_onTouchEvent` → `GestureDetector`.
  - CNA is event-driven, not poll-driven like FNA: `InputManager` accumulates raw per-device
    state as SDL events arrive; `Game::Tick()` pumps SDL events exactly once per frame
    (`PollEvents()`), which is what keeps that accumulated state current.
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`. EasyGL is the primary/tested
    backend.

---

## 2. Current status

### Build
- EasyGL build (`cmake-build-debug`): clean, as of the last build in this session (includes
  Task 767).
- Vulkan (`cmake-build-vulkan`) / Bgfx (`cmake-build-bgfx`) build dirs are misconfigured — their
  CMake caches point at the sibling `…/openeggbert/cna` repo, not this checkout (see Section 5).
  They have not been used or fixed for input work.

### Tests
- 1892 / 1892 unit tests passing (EasyGL build, `cmake-build-debug/CnaTests`), as of the last run
  in this session, verified stable under `--gtest_shuffle --gtest_repeat=10`.

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad, touch, text input panel).
  Builds; last known to run crash-free.

### Recently implemented
- Phase I4 (Mouse + MouseCursor, tasks 745–755): real `SetPosition`/`IsRelativeMouseModeEXT`/
  `ClickedEXT` behavior; `MouseCursor` CHECKLIST compliance, `FromTexture2D`, `IDisposable`,
  lazy stock-cursor construction; 26 new tests in `MouseInputTests.cpp`.
- Phase I5 so far (Keyboard, tasks 760–767): `GetPressedKeys()` ordering fix;
  FNA-faithful `GetHashCode()`; `operator[](Keys)` indexer; completed SDL keycode→`Keys` map
  (F13–F24, Apps, Sleep, Volume keys, `KP_CLEAR`/`KP_PERIOD`, AZERTY/Norwegian/BEPO locale
  fallbacks); real `GetKeyFromScancodeEXT` implementation; scancode mode
  (`FNA_KEYBOARD_USE_SCANCODES` + `INTERNAL_scanMap`) wired into both the live key-event handler
  and `GetKeyFromScancodeEXT`; `KeyboardState::ToString()` now matches FNA's `ValueType` default
  (fully-qualified type name) instead of a CNA-invented placeholder, and its `NOXNA` tag was
  removed as a pre-existing mistake; `Keys`'s implicit `int` underlying type is now explicit
  (`enum class Keys : int`), matching FNA's declaration intent with no behavior change.
- One unrelated build blocker was hit and fixed: the sibling `sharp-runtime` checkout committed a
  `System::IAsyncResult` interface change that broke this repo's `StorageDevice.cpp`.

### What does NOT work yet
- Keyboard: nothing outstanding is known to be broken. The one remaining Phase I5 task (768) is
  test-coverage only, not a behavior fix (see Section 8).
- `Mouse::SetPosition` has no inverse (logical→window) coordinate transform, so its OS-cursor
  warp target is off by the scale factor on a letterboxed/scaled window (documented in-source in
  `Mouse.cpp`; fixing it for real is a graphics-layer change, out of scope for this branch).
- `demo_input`'s text input panel has not been visually verified on a real display in this
  environment (no Wayland screenshot tool available here).
- `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES` unconditionally even when disconnected
  (FNA returns `0` when disconnected) — minor, not yet scheduled as its own task.

---

## 3. Recent changes

All on `feature/input`, most recent first (`git log`):

| Commit | Change |
|--------|--------|
| `1600bd6` | fix(Task 767): make `Keys` enum's `int` underlying type explicit |
| `37b3bd6` (docs) | mark Task 766 complete in `plan_input.md`; update `NEXT.md` |
| `d7156a3` | fix(Task 766): `KeyboardState::ToString()` matches FNA's `ValueType` default |
| `112edd4` (docs) | mark Task 765 complete in `plan_input.md`; update `NEXT.md` |
| `7dd4bf3` | feat(Task 765): add keyboard scancode mode (`INTERNAL_scanMap` + `FNA_KEYBOARD_USE_SCANCODES`) |
| `028ba32` (docs) | rewrite `NEXT.md` as a concise handoff document |
| `b63c56a` (docs) | mark Task 764 complete in `plan_input.md`; update `NEXT.md` |
| `697ae83` | feat(Task 764): implement `Keyboard::GetKeyFromScancodeEXT` |
| `a312df4` (docs) | mark Task 763 complete |
| `15f7667` | feat(Task 763): complete the SDL keycode→`Keys` map |
| `5bbadf0` (docs) | mark Task 762 complete |
| `b2b8939` | feat(Task 762): add `KeyState operator[](Keys) const` |
| `bfa4a0b` (docs) | mark Task 761 complete |
| `5c9532b` | fix(Task 761): `KeyboardState::GetHashCode()` FNA fidelity |
| `52f7bc3` (docs) | mark Task 760 complete |
| `345b30e` | fix(Task 760): `KeyboardState::GetPressedKeys()` ascending order |
| `92911c9`…`088f4a4` | Phase I4 (Mouse/MouseCursor, tasks 745–755) + the `sharp-runtime`
  `StorageDevice.cpp` build-blocker fix — see `plan_input.md` for full detail. |

- **Files added:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`.
- **Files modified this session:** `Keyboard.{hpp,cpp}`, `KeyboardState.cpp`,
  `SdlInputBridge.{hpp,cpp}`, `Mouse.{hpp,cpp}`, `MouseCursor.{hpp,cpp}`, `InputManager.{hpp,cpp}`,
  `StorageDevice.cpp`, `AUDIT.md`, `plan_input.md`, `NEXT.md`.
- **Behavior changed:** `KeyboardState::GetPressedKeys()`/`GetHashCode()` now FNA-faithful;
  `Keyboard::GetKeyFromScancodeEXT` is a real layout-aware translation instead of an identity
  stub, and now respects scancode mode; SDL keycode coverage is materially more complete;
  `FNA_KEYBOARD_USE_SCANCODES=1` now switches the live key-event handler to physical-key-position
  (`INTERNAL_scanMap`-equivalent) lookups instead of layout-dependent keycodes;
  `KeyboardState::ToString()` now returns `"Microsoft.Xna.Framework.Input.KeyboardState"`
  (FNA's `ValueType` default) instead of the old `"[KeyboardState]"` placeholder, and is no
  longer `NOXNA`-tagged; `Keys`'s underlying type is now explicitly `int` (no observable change);
  Mouse/MouseCursor behavior described above under "Recently implemented".
- **Tests added:** 26 (`MouseInputTests.cpp`, Phase I4). Tasks 760–767 (Phase I5, this session)
  were verified with ad-hoc standalone harnesses/manual checks that were **not committed**;
  dedicated `KeyboardInputTests.cpp` coverage for all of Phase I5 is deferred to task 768 (an
  explicit, already-planned task), matching the batching pattern used for tasks 740 and 755.

---

## 4. Current blocker / main problem

**There is no blocker.** The last known state: EasyGL build clean, 1892/1892 tests passing, no
failing command or failing test identified. Work can resume directly at Phase I5 task 768 (the
last remaining task in the phase).

The one open practical issue is unrelated to correctness and does not block Phase I5 work:

- **Symptom:** `cmake-build-vulkan/CMakeCache.txt` and `cmake-build-bgfx/CMakeCache.txt` have
  `CMAKE_HOME_DIRECTORY=/rv/data/development/github.com/openeggbert/cna` (the sibling repo), not
  this `cna_input` checkout.
- **Failing command:** none directly — building in those dirs would silently compile the wrong
  source tree rather than fail outright.
- **Affected:** any Vulkan/Bgfx verification of input work (EasyGL is the only backend verified
  so far).
- **Suspected cause:** those build dirs were originally configured against the sibling `cna`
  repo and never reconfigured for `cna_input`.
- **What has already been tried:** `cmake-build-debug` (EasyGL) was reconfigured correctly with
  `rm -rf cmake-build-debug && cmake -S … -B … -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL …`; the
  same fix has **not** been applied to the Vulkan/Bgfx dirs.

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| Confirmed | `cmake-build-vulkan` / `cmake-build-bgfx` caches point at the sibling `…/cna` repo (Section 4). |
| Incomplete | Dedicated `KeyboardInputTests.cpp` coverage for tasks 760–767 deferred to task 768 (the only remaining Phase I5 task). |
| Needs verification | `demo_input` text panel not visually confirmed on a real display in this environment (builds and runs crash-free, but no Wayland screenshot tool available here and forcing X11 makes SDL exit). |
| Intentional deviation | `Mouse::SetPosition` has no inverse (logical→window) coordinate transform, so its `SDL_WarpMouseInWindow` target is off by the scale factor on a letterboxed/scaled window. Documented in-source in `Mouse.cpp`. Fixing it for real needs a graphics-layer `IGraphicsBackend` addition, out of scope for this branch. |
| Intentional deviation | `InputManager::GetMouseState()` reports relative-mode `X`/`Y` from a float delta accumulator fed by `SDL_EVENT_MOUSE_MOTION`'s `xrel`/`yrel` (drained to `0` on each read), rather than FNA's `SDL_GetRelativeMouseState` poll — the event-driven equivalent. Documented in-source in `InputManager.cpp`. |
| Intentional deviation | `GamePadState.PacketNumber` is tracked at the raw `InputManager` layer (bumped on real connection/button/axis changes) rather than by comparing freshly-built `GamePadState`s like FNA's poll loop. Documented in-source in `InputManager.cpp`. |
| Intentional deviation | `TouchPanel::GetState()` falls back to `InputManager::GetTouchState()` because CNA's bridge is event-driven, not poll-driven like FNA. Documented in-source in `TouchPanel.cpp`. |
| Intentional deviation | `TextInputEXT::TextInput` is `char`-based, so `SDL_EVENT_TEXT_INPUT` is forwarded per UTF-8 byte, not per UTF-16 code unit like FNA. |
| Minor, not yet fixed | `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES` unconditionally in both branches; FNA returns `0` when disconnected. |
| Pre-existing, unrelated to input work | Working tree shows `D .claude/settings.json` (deleted) and untracked `vendor/wgpu-native/`. Do not commit either. |
| Fixed | `MouseCursor`'s move constructor/assignment used to bitwise-copy the raw `SDL_Cursor*` without nulling the moved-from source (latent double-free risk, not reachable by any current call site). Fixed in task 752; regression-tested in `MouseInputTests.cpp`. |
| Fixed | Keyboard scancode mode (`FNA_KEYBOARD_USE_SCANCODES`/`INTERNAL_scanMap`) was entirely absent — the live key-event handler always used layout-dependent keycodes, and `GetKeyFromScancodeEXT` never took the scancode-mode passthrough branch. Fixed in task 765. |
| Fixed | `KeyboardState::ToString()` returned a CNA-invented `"[KeyboardState]"` placeholder, incorrectly tagged `NOXNA`, instead of matching FNA's `ValueType` default (fully-qualified type name) like `GamePadState::ToString()` does. Fixed in task 766. |
| Fixed (cosmetic, no behavior change) | `Keys`'s `int` underlying type was implicit; task 767 made it explicit (`enum class Keys : int`) to match FNA's declaration intent. |
| Fixed (unrelated to input work, but blocked all builds) | The sibling `sharp-runtime` checkout committed a `System::IAsyncResult` interface addition that broke this repo's `StorageDevice.cpp`. Fixed by implementing the two missing overrides in `ContainerResult`/`SelectorResult`. If a future `sharp-runtime` change breaks the build the same way, check `cd ../sharp-runtime && git status --short && git log -1` before assuming it's a transient concurrent-edit race — if the change is already committed, it needs the same kind of fix here, not a wait-and-retry. |

---

## 6. Architecture notes

### Main modules (input)
| Layer | Location | Notes |
|-------|----------|-------|
| XNA public API | `include/Microsoft/Xna/Framework/Input/**` | Must match XNA 4.0 / FNA; FNA `EXT` additions tagged `NOXNA`. |
| Internal bridge | `src/CNA/Internal/Input/SdlInputBridge.cpp` | Single event entry point `ProcessEvent(const SDL_Event&)`, plus public static query methods (`GetKeyFromScancode`, `GetCapabilities`, `GetGUID`, …) that XNA-layer classes call into directly. |
| Internal state | `src/CNA/Internal/Input/InputManager.cpp` | Accumulates per-device state; exposes `GetKeyboardState/GetMouseState/GetRawGamePadState/GetTouchState`. Event-driven, not poll-driven. |
| Gesture engine | `src/CNA/Internal/Input/GestureDetector.cpp` | Ported state machine, driven by `TouchPanel::INTERNAL_onTouchEvent`/`Update`. |
| FNA reference | `/rv/data/library/github.com/FNA-XNA/FNA/src/Input` | Authoritative behavior. |

### Data flow (input)
```
SDL_PollEvent  (Game::PollEvents, called once per frame from Game::Tick())
  → SdlInputBridge::ProcessEvent(event)        // switch on event.type
      → InputManager::Set*State(...)           // keyboard / mouse / gamepad / touch snapshot
      → TextInputEXT::INTERNAL_OnText*(...)     // text input / editing
      → TouchPanel::INTERNAL_onTouchEvent(...)  // touch → GestureDetector
  → Keyboard/Mouse/GamePad/TouchPanel::GetState()  read by game code each frame
  → FrameworkDispatcher::Update() → TouchPanel::Update() → GestureDetector::OnUpdate()
```

### Invariants / boundaries that must stay stable
- `NOXNA` marks every non-XNA member in the `Microsoft::Xna` namespace; a wholly-non-XNA class
  uses `NOXNA class` (precedent: `ShaderEffect`, `MouseCursor`).
- `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp` and `.cpp`.
- C# properties → `getXProperty()` / `setXProperty()`, not public fields. FNA's
  `{ get; internal set; }` maps to a `NOXNA`-tagged public setter, not a `friend`-based private
  one — there is no `friend` precedent anywhere in CNA.
- A read-only C# auto-property with lazy static-constructor semantics maps to a `getXProperty()`
  backed by a function-local `static` variable (Meyer's singleton), since a plain C++ static data
  member can't be lazily initialized (precedent: `MouseCursor`'s 11 stock-cursor getters).
- XNA/FNA API names and public constructor signatures are frozen — no renames, no new enum
  values on frozen types (e.g. `PlayerIndex` stays at 4 values), no convenience constructor
  parameters even when they'd be internal-set.
- `SdlInputBridge::ProcessEvent` is the single SDL-event funnel; do not add a second event path.
  Query-only helper methods on `SdlInputBridge` (not event handling) are fine and already
  established practice.
- `GestureDetector` and `MouseCursor`'s stock-cursor singletons are process-lifetime file-static
  state with no reset hook; tests that touch them must clean up after themselves (see
  `GestureDetectorTests.cpp` and `MouseInputTests.cpp`'s `ResetMouseState()` for the pattern).
- Classes that require a `GraphicsDevice` to construct (creates a real SDL window + graphics
  backend) are not unit-tested in `CnaTests` — see `OcclusionQueryDynamicBufferTests.cpp` and
  `MouseInputTests.cpp` for the precedent and rationale.

---

## 7. Useful commands

```bash
# Configure EasyGL build for THIS repo (already done; redo only if the cache is wrong)
cmake -S /rv/data/development/github.com/openeggbert/cna_input \
      -B /rv/data/development/github.com/openeggbert/cna_input/cmake-build-debug \
      -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Build library / tests / demo
cmake --build cmake-build-debug --target CNA -j"$(nproc)"
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
cmake --build cmake-build-debug --target cna_demo_input -j"$(nproc)"

# Run all unit tests
./cmake-build-debug/CnaTests

# Run the Keyboard test suite specifically
./cmake-build-debug/CnaTests --gtest_filter='*Keyboard*'

# Run the input demo (needs a display; F1 toggles text input, Esc exits)
./cmake-build-debug/cna_demo_input

# Verify a build dir's source wiring (should print …/cna_input)
grep CMAKE_HOME_DIRECTORY cmake-build-debug/CMakeCache.txt
```

There is no known bug to reproduce right now (Section 4). No project-wide lint/format target was
observed; the `Doxyfile` exists but is not a build/verify step.

If a build fails on a `sharp-runtime` file unrelated to the change being made, it may be a
concurrent edit in the sibling `sharp-runtime` checkout settling — retry once. If it persists,
run `cd ../sharp-runtime && git status --short && git log -1` to check whether the change is
already committed (permanent) rather than assuming it will resolve itself.

---

## 8. Next smallest tasks

Phase I5 (Keyboard fidelity and SDL key mapping) — task 768 is the last remaining task in the
phase. Full detail for completed tasks 760–767 is in `plan_input.md`.

1. **Task 768 — Expand `KeyboardInputTests.cpp`.**
   - Goal: dedicated coverage for everything Phase I5 touched: all three `KeyboardState`
     constructors, `operator[]`/`getItem`, `==`/`!=`/`Equals` (equal + unequal), `GetHashCode`
     consistency, `ToString`, multi-key `GetPressedKeys` ordering, empty `GetPressedKeys`,
     `GetState(PlayerIndex)`, `GetKeyFromScancodeEXT` (both default and scancode mode), and
     `Keys`/`KeyState` value spot-checks. This is where the ad-hoc verifications done for
     tasks 760–767 should become real, committed tests.
   - Files: `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`.
   - Verify: `cmake --build cmake-build-debug --target CnaTests -j"$(nproc)" && ./cmake-build-debug/CnaTests --gtest_filter='*Keyboard*'`

After task 768, Phase I5 is complete; the next phase in `plan_input.md` is Phase I6 (CHECKLIST /
SPDX / NOXNA compliance and docs).

---

## 9. Do not do yet

- No broad refactor of `SdlInputBridge` or `InputManager` — keep the single-funnel event design;
  add cases/query methods, do not restructure.
- No API renames or namespace moves — XNA/FNA names and constructor signatures are frozen (see
  Section 6).
- No graphics changes on this branch — graphics is tracked in `GRAPHICS_TASKS.md` on its own
  track.
- No unrelated cleanup while Phase I5 is in progress — e.g. do not touch the `TouchPanel`
  `MAX_TOUCHES` deviation or the Vulkan/Bgfx build-dir wiring as a side effect of a Keyboard task;
  each needs its own small task/PR.
- Do not commit the pre-existing working-tree changes (`D .claude/settings.json`, untracked
  `vendor/wgpu-native/`) — unrelated to input work.
- Do not reconfigure or commit `cmake-build-*` directories — they are gitignored.
- Do not attempt to unit-test `GraphicsDevice`-dependent construction in `CnaTests` (Section 6) —
  use a separate integration test executable instead.
- Do not change the `TextInputEXT` char-based callback signature — a deliberate, documented
  deviation.

---

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task (Section 8, Task 768).
Do not refactor unrelated code. Make one small, verified improvement.

Then run the relevant build/test command for that task (see Section 8's "Verify" line, and/or
Section 7), confirm it passes, and update NEXT.md (Sections 2, 3, 8, and this resume prompt)
before finishing.
```
