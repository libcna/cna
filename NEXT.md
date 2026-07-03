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
- **Current development phase:** Phases I1–I6 are complete (TextInputEXT; Touch & gesture
  pipeline; GamePad behavior/FNA fidelity; Mouse behavior and MouseCursor; Keyboard fidelity and
  SDL key mapping; CHECKLIST/SPDX/NOXNA compliance and docs). **Phase I7 (demo and integration
  coverage) is in progress** — task 780 of 780–783 is done (from an earlier session; not part of
  this session's work).
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
- EasyGL build (`cmake-build-debug`): clean, as of the last build in this session (tasks 777/778
  were docs-only, no rebuild needed; last actual rebuild was for task 776).
- Vulkan (`cmake-build-vulkan`) / Bgfx (`cmake-build-bgfx`) build dirs are misconfigured — their
  CMake caches point at the sibling `…/openeggbert/cna` repo, not this checkout (see Section 5).
  They have not been used or fixed for input work.

### Tests
- 1910 / 1910 unit tests passing (EasyGL build, `cmake-build-debug/CnaTests`), as of the last run
  in this session, verified stable under `--gtest_shuffle --gtest_repeat=10` (Keyboard filter) and
  a full-suite `--gtest_shuffle` pass.

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad, touch, text input panel).
  Builds; last known to run crash-free.

### Recently implemented
- Phase I4 (Mouse + MouseCursor, tasks 745–755): real `SetPosition`/`IsRelativeMouseModeEXT`/
  `ClickedEXT` behavior; `MouseCursor` CHECKLIST compliance, `FromTexture2D`, `IDisposable`,
  lazy stock-cursor construction; 26 new tests in `MouseInputTests.cpp`.
- Phase I5 — now complete (Keyboard, tasks 760–768): `GetPressedKeys()` ordering fix;
  FNA-faithful `GetHashCode()`; `operator[](Keys)` indexer; completed SDL keycode→`Keys` map
  (F13–F24, Apps, Sleep, Volume keys, `KP_CLEAR`/`KP_PERIOD`, AZERTY/Norwegian/BEPO locale
  fallbacks); real `GetKeyFromScancodeEXT` implementation; scancode mode
  (`FNA_KEYBOARD_USE_SCANCODES` + `INTERNAL_scanMap`) wired into both the live key-event handler
  and `GetKeyFromScancodeEXT`; `KeyboardState::ToString()` now matches FNA's `ValueType` default
  (fully-qualified type name) instead of a CNA-invented placeholder, and its `NOXNA` tag was
  removed as a pre-existing mistake; `Keys`'s implicit `int` underlying type is now explicit
  (`enum class Keys : int`), matching FNA's declaration intent with no behavior change;
  `KeyboardInputTests.cpp` expanded from 3 to 21 tests (18 new), covering everything the phase
  touched.
- Phase I6, now complete (tasks 775–778): added the missing
  `// SPDX-License-Identifier: MS-PL` header to `SdlInputBridge.{hpp,cpp}` and
  `InputManager.{hpp,cpp}`, the only four files under `CNA/Internal/Input/` that lacked it;
  swept all Input headers and tagged 7 more non-XNA members with `NOXNA`
  (`TouchLocation()`/`TouchCollection()` default ctors, `TouchPanelCapabilities`'s two ctors, and
  `TouchPanel`'s `EnqueueGesture`/`INTERNAL_onTouchEvent`/`SetFinger`/`Update`) — all confirmed
  against FNA source; also confirmed no other `ToString`/`Equals`/`GetHashCode`/equality-operator
  override is incorrectly `NOXNA`-tagged (the mistake task 766 found and fixed on
  `KeyboardState`); updated `AUDIT.md`'s `Input`/`Input::Touch` tables with a `Runtime` column
  distinguishing API-surface completeness from actual FNA-faithful behavior; updated the
  Input/Touch portions of `docs/xna-4-api-coverage.md` (Touch was still described as an unwired
  0%-functional stub there — corrected to reflect Phase I2's real gesture pipeline); added new
  `docs/input-backend.md` (architecture overview, complete SDL3-event→XNA-state mapping table,
  event-driven-vs-FNA-polling deviation, per-device fidelity notes).
- One unrelated build blocker was hit and fixed: the sibling `sharp-runtime` checkout committed a
  `System::IAsyncResult` interface change that broke this repo's `StorageDevice.cpp`.

### What does NOT work yet
- Keyboard: nothing outstanding is known to be broken; Phase I5 is complete.
- `Mouse::SetPosition` has no inverse (logical→window) coordinate transform, so its OS-cursor
  warp target is off by the scale factor on a letterboxed/scaled window (documented in-source in
  `Mouse.cpp`; fixing it for real is a graphics-layer change, out of scope for this branch).
- `demo_input`'s text input panel has not been visually verified on a real display in this
  environment (no Wayland screenshot tool available here).
- `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES` unconditionally even when disconnected
  (FNA returns `0` when disconnected) — minor, not yet scheduled as its own task.
- Phase I7 tasks 781–783 (demo/integration coverage) have not started — see `plan_input.md`.

---

## 3. Recent changes

All on `feature/input`, most recent first (`git log`):

| Commit | Change |
|--------|--------|
| `121773b` | docs(Task 778): add `docs/input-backend.md`; closes Phase I6 |
| `9e85dcf` (docs) | mark Task 777 complete in `plan_input.md`; update `NEXT.md` for Task 778 |
| `20e0d6e` | docs(Task 777): update AUDIT.md and xna-4-api-coverage.md for Input/Touch |
| `edefc1a` (docs) | mark Task 776 complete in `plan_input.md`; update `NEXT.md` for Task 777 |
| `9c0deb3` | docs(Task 776): tag remaining non-XNA Touch members with NOXNA |
| `d4afc62` (docs) | mark Task 775 complete in `plan_input.md`; update `NEXT.md` for Task 776 |
| `09ff0ba` | docs(Task 775): add SPDX-License-Identifier to internal input backend files |
| `56b32ee` (docs) | mark Task 768 complete in `plan_input.md`; `NEXT.md` now points at Phase I6 |
| `3045ef7` | test(Task 768): expand `KeyboardInputTests.cpp` coverage; closes Phase I5 |
| `9b2e464` (docs) | mark Task 767 complete in `plan_input.md`; update `NEXT.md` |
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

- **Files added:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`,
  `docs/input-backend.md`.
- **Files modified this session:** `Keyboard.{hpp,cpp}`, `KeyboardState.{hpp,cpp}`, `Keys.hpp`,
  `SdlInputBridge.{hpp,cpp}`, `Mouse.{hpp,cpp}`, `MouseCursor.{hpp,cpp}`, `InputManager.{hpp,cpp}`,
  `StorageDevice.cpp`, `KeyboardInputTests.cpp`, `AUDIT.md`, `plan_input.md`, `NEXT.md`.
  (`SdlInputBridge.{hpp,cpp}`/`InputManager.{hpp,cpp}` this round only gained an SPDX header
  line — no logic changed.) Also `TouchLocation.hpp`, `TouchCollection.hpp`,
  `TouchPanelCapabilities.hpp`, `TouchPanel.hpp` (task 776 — Doxygen/`NOXNA` markers only, no
  logic changed), `AUDIT.md`, `docs/xna-4-api-coverage.md` (task 777 — docs only).
- **Behavior changed:** `KeyboardState::GetPressedKeys()`/`GetHashCode()` now FNA-faithful;
  `Keyboard::GetKeyFromScancodeEXT` is a real layout-aware translation instead of an identity
  stub, and now respects scancode mode; SDL keycode coverage is materially more complete;
  `FNA_KEYBOARD_USE_SCANCODES=1` now switches the live key-event handler to physical-key-position
  (`INTERNAL_scanMap`-equivalent) lookups instead of layout-dependent keycodes;
  `KeyboardState::ToString()` now returns `"Microsoft.Xna.Framework.Input.KeyboardState"`
  (FNA's `ValueType` default) instead of the old `"[KeyboardState]"` placeholder, and is no
  longer `NOXNA`-tagged; `Keys`'s underlying type is now explicitly `int` (no observable change);
  Mouse/MouseCursor behavior described above under "Recently implemented".
- **Tests added:** 26 (`MouseInputTests.cpp`, Phase I4) + 18 (`KeyboardInputTests.cpp`, Phase I5,
  task 768 — expanded from 3 to 21). Phase I5's earlier tasks (760–767) were verified during
  development with ad-hoc standalone harnesses/manual checks that were **not committed**; task
  768 is where that coverage became real, committed tests, matching the batching pattern used
  for tasks 740 and 755. Phase I6 (tasks 775–778) added no tests — 775/776 are
  comment/marker-only fixes with no behavior change (1910/1910 still passing confirms this), and
  777/778 are pure documentation updates with no source change at all.

---

## 4. Current blocker / main problem

**There is no blocker.** The last known state: EasyGL build clean, 1910/1910 tests passing, no
failing command or failing test identified. Phase I6 is complete; work can resume directly at
Phase I7 task 781 (task 780 was already done in an earlier session).

The one open practical issue is unrelated to correctness and does not block Phase I7 work:

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
| Not started | Phase I7 tasks 781–783 (multi-pad demo rendering + rumble; gesture integration test; relative-mouse-mode integration check) haven't been picked up yet — see `plan_input.md`. |
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
| Fixed | `KeyboardInputTests.cpp` had only 3 tests, none covering tasks 761–767's changes. Task 768 expanded it to 21 (18 new), closing out Phase I5's test-coverage debt. |
| Fixed | `SdlInputBridge.{hpp,cpp}`/`InputManager.{hpp,cpp}` were missing the `// SPDX-License-Identifier: MS-PL` header used everywhere else in the codebase. Fixed in task 775. |
| Fixed | 7 non-XNA members across `TouchLocation`/`TouchCollection`/`TouchPanelCapabilities`/`TouchPanel` were missing the `NOXNA` marker (default ctors with no FNA counterpart, and `TouchPanel` methods that are `internal` in FNA but public in CNA). Fixed in task 776. |
| Fixed (docs) | `AUDIT.md`'s Input/Touch tables didn't distinguish API-surface completeness from runtime behavior; `docs/xna-4-api-coverage.md` still described `Input::Touch` as an unwired, 0%-functional stub even though Phase I2 made it real. Fixed in task 777. |
| Fixed (docs) | No architecture doc existed for the input backend (`SdlInputBridge`/`InputManager`/`GestureDetector`, the SDL3-event mapping, or the event-driven-vs-polling deviation). Added `docs/input-backend.md` in task 778. |
| Fixed (unrelated to input work, but blocked all builds) | The sibling `sharp-runtime` checkout committed a `System::IAsyncResult` interface addition that broke this repo's `StorageDevice.cpp`. Fixed by implementing the two missing overrides in `ContainerResult`/`SelectorResult`. If a future `sharp-runtime` change breaks the build the same way, check `cd ../sharp-runtime && git status --short && git log -1` before assuming it's a transient concurrent-edit race — if the change is already committed, it needs the same kind of fix here, not a wait-and-retry. |

---

## 6. Architecture notes

**Full architecture writeup: `docs/input-backend.md`** (added task 778) — data-flow diagram,
complete SDL3-event→XNA-state mapping table, event-driven-vs-FNA-polling deviation, and
per-device fidelity notes. The summary below is a quick-reference subset of that doc.

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

Phases I1–I6 are all complete. Phase I7 (demo and integration coverage), tasks 781–783 remain
(task 780 was already done in an earlier session). Full detail for everything completed so far
is in `plan_input.md`.

1. **Task 781 — `examples/demo_input`: multi-pad rendering + rumble.**
   - Goal: add vibration/rumble feedback and render `PlayerIndex::Two/Three/Four` alongside the
     existing `PlayerIndex::One`-only view.
   - Files: `examples/demo_input/*` (check current structure first — not touched this session).
   - Verify: `cmake --build cmake-build-debug --target cna_demo_input -j"$(nproc)"`, then run and
     visually confirm (needs a display; see Section 5's "Needs verification" row for the
     no-Wayland-screenshot-tool caveat already hit for task 780).

2. **Task 782 — Integration test: gesture pipeline end-to-end.**
   - Goal: drive synthetic `SDL_EVENT_FINGER_*` through `SdlInputBridge::ProcessEvent` and assert
     a `GestureSample` (Tap, Flick) is dequeued via `TouchPanel::ReadGesture` — proves the Phase
     I2 wiring works from the real SDL event entry point, not just via `GestureDetector`'s direct
     API (which `GestureDetectorTests.cpp` already covers).
   - Files: likely a new test file under `tests/CNA/Internal/Input/` or an extension of an
     existing one — check `GestureDetectorTests.cpp`/`TouchInputTests.cpp` first for the best fit.
   - Verify: `cmake --build cmake-build-debug --target CnaTests -j"$(nproc)" && ./cmake-build-debug/CnaTests --gtest_filter='*Gesture*:*Touch*'`

3. **Task 783 — Integration/manual check: relative mouse mode + `SetPosition` warp.**
   - Goal: confirm relative mouse mode and `SetPosition`'s cursor warp behave correctly together
     in practice (Phase I4). This is explicitly integration/manual, not necessarily a new unit
     test — decide based on what's practical to verify headlessly vs. what needs a real window.
   - Files: none predetermined — investigate first.
   - Verify: TBD based on the approach chosen.

---

## 9. Do not do yet

- No broad refactor of `SdlInputBridge` or `InputManager` — keep the single-funnel event design;
  add cases/query methods, do not restructure.
- No API renames or namespace moves — XNA/FNA names and constructor signatures are frozen (see
  Section 6).
- No graphics changes on this branch — graphics is tracked in `GRAPHICS_TASKS.md` on its own
  track.
- No unrelated cleanup while Phase I7 is in progress — e.g. do not touch the `TouchPanel`
  `MAX_TOUCHES` deviation or the Vulkan/Bgfx build-dir wiring as a side effect of a demo/test
  task; each needs its own small task/PR.
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
Read NEXT.md first. Inspect only the files needed for the first task (Section 8, Task 781).
Do not refactor unrelated code. Make one small, verified improvement.

Then run the relevant build/test command for that task (see Section 8's "Verify" line, and/or
Section 7), confirm it passes, and update NEXT.md (Sections 2, 3, 8, and this resume prompt)
before finishing.
```
