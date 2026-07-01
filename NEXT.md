# NEXT.md — CNA Project Handoff (feature/input branch)

> This handoff covers the **Input subsystem porting** work that is the active focus of the
> `feature/input` branch. The broader Graphics work is tracked separately in `GRAPHICS_TASKS.md`
> (and was the subject of an earlier handoff); it is **not** the current focus here.

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on **SDL3** with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — so XNA/FNA game code can be ported to C++
with minimal API-surface changes.

- **Main goal (this branch):** Port `Microsoft::Xna::Framework::Input` and `…::Input::Touch` from
  the FNA reference to CNA — not just API surface, but **FNA-faithful runtime behavior** wired to
  SDL3, CHECKLIST-compliant, and covered by tests. The plan is `plan_input.md` (Phases I1–I7,
  tasks 700–783).
- **Current development phase:** **Phase I1 (TextInputEXT), Phase I2 (Touch & gesture pipeline),
  and Phase I3 (GamePad behavior/FNA fidelity) are all complete.** Next up is **Phase I4 (Mouse
  behavior and MouseCursor)**.
- **Key architectural decisions:**
  - The authoritative behavioral reference is the FNA source tree at
    `/rv/data/library/github.com/FNA-XNA/FNA/src`.
  - Non-XNA members inside the `Microsoft::Xna` namespace are tagged with the `NOXNA` marker macro.
    FNA's `EXT`-suffixed additions are non-XNA; a wholly-non-XNA class is tagged `NOXNA class`.
  - Real input behavior flows through an internal bridge: SDL3 events →
    `CNA::Internal::Input::SdlInputBridge` → `InputManager` → XNA state objects. Touch additionally
    flows `SdlInputBridge` → `TouchPanel::INTERNAL_onTouchEvent` → `GestureDetector`.
  - CNA is **event-driven**, not poll-driven like FNA: `InputManager` accumulates raw per-device
    state as SDL events arrive; `Game::Tick()` pumps SDL events exactly once per frame
    (`PollEvents()`), which is what keeps that accumulated state current. See `GamePad.cpp`'s
    `GetState()` and `InputManager`'s class doc comment for the fuller writeup (task 735).
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`. EasyGL is the primary/tested backend.

---

## 2. Current status

### Build
- **EasyGL build (`cmake-build-debug`):** clean.
- **Vulkan (`cmake-build-vulkan`) and Bgfx (`cmake-build-bgfx`) build dirs are still wired to the
  sibling `…/openeggbert/cna` repo** — see Section 4. They were not used or fixed for input work.

### Tests
- **1866 / 1866 unit tests pass** (EasyGL build, `cmake-build-debug/CnaTests`). GamePad-related
  additions this phase (58 tests total across 5 new files, plus 1 added to the existing
  `GamePadInputTests.cpp` for `PacketNumber`):
  - `GamePadButtonsTests.cpp` — `GamePadButtonsTest` (7) + `GamePadDPadTest` (7): constructors, all
    named getters, `FromButtonArray` (multi-flag/cross-element/empty), equality, `GetHashCode`.
  - `GamePadStateTests.cpp` — 10 tests: both constructors, trigger/stick→button packing,
    `IsButtonDown`/`IsButtonUp`, equality (incl. `PacketNumber`), `GetHashCode`, `ToString`.
  - `GamePadThumbSticksTests.cpp` — 9 tests: construction/equality/hash, plus `IndependentAxes`/
    `Circular` dead-zone math (incl. the private `ExcludeCircularDeadZone`) driven indirectly
    through `InputManager` + `GamePad::GetState`.
  - `GamePadTriggersTests.cpp` — 7 tests: construction, `WithinEpsilon`-based equality (with a
    worked-out ULP argument for why the test values are chosen), `GetHashCode`, dead-zone ctor.
  - `GamePadTests.cpp` — 13 tests: `ExcludeAxisDeadZone` math, `GamePad`'s no-hardware fallback
    paths (`GetCapabilities`/`SetVibration`/EXT methods), and a full `GamePadCapabilities`
    getter/setter round-trip (36 properties).

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad, touch, text input panel).
  Builds; runs crash-free.

### Recently implemented (Phase I3 — tasks 725–740)
- `GamePad::SetLightBarEXT`/`GetGyroEXT`/`GetAccelerometerEXT` wired to real SDL3 calls
  (`SDL_SetGamepadLED`, `SDL_Get/SetGamepadSensorEnabled` + `SDL_GetGamepadSensorData`).
- EXT buttons (Misc1/Paddle1–4/TouchPad) now reach `GamePadState.Buttons` via new
  `InputManager::GamePadButton` entries and matching SDL button-conversion cases.
- `GamePadState.PacketNumber` now increments on real state changes. **Architectural deviation from
  FNA:** tracked at the raw `InputManager` layer (bumped when a connection/button/axis value
  actually changes) rather than by comparing freshly-built `GamePadState`s like FNA's poll loop —
  comparing *built* states would falsely bump the counter depending on which `GamePadDeadZone` mode
  a caller uses, since dead-zone mode changes the built button flags for the same raw state.
- `GamePadCapabilities` reworked: 34 raw public `bool` fields (+ `GamePadType_`) → 36 private fields
  with `getXProperty()`/`NOXNA setXProperty()` pairs; the 10 EXT properties are `NOXNA` on both
  getter and setter. Implementation moved out of the header into a new `GamePadCapabilities.cpp`.
- `GamePadButtons::FromButtons`/`GamePadDPad::FromButtons` renamed to `FromButtonArray` (matching
  FNA's internal names); `GamePadDPad::FromButtonArray` now takes `std::initializer_list<Buttons>`
  instead of a single combined value, matching FNA's `params Buttons[]` signature exactly —
  `GamePadState`'s 5-arg public constructor now passes the same list to both `FromButtonArray` calls
  directly, removing a previous CNA-only round-trip workaround.
- FNA-fidelity fixes: `GamePadThumbSticks`/`GamePadTriggers.GetHashCode` (were truncated `*1000`
  formulas, now match FNA's `Vector2`/float-hash-based formulas exactly); `GamePadState.ToString()`
  now returns the fixed fully-qualified type name, matching FNA's `base.ToString()` (FNA never
  overrides `ToString` on this struct).
- Optional `FNA_GAMEPAD_NUM_GAMEPADS` env override added, clamped to 4 (CNA's `PlayerIndex` is
  frozen XNA API with exactly 4 values, so an override above 4 has no addressable slot — the
  practically useful direction, reducing/disabling gamepad tracking, works).

### What does NOT work yet
- **Mouse:** `SetPosition` does not warp the cursor; relative-mouse-mode is a dead flag; `ClickedEXT`
  never fires (Phase I4 — next focus).
- **Keyboard:** `GetPressedKeys()` order is non-deterministic; SDL keycode→Keys map is incomplete;
  `GetKeyFromScancodeEXT` is an identity stub (Phase I5).
- **demo_input text panel is not visually verified** (see Section 5; carried over from Phase I1,
  unrelated to gamepad work).
- **Minor, out of scope for Phase I3:** `TouchPanel::GetCapabilities()` still passes `MAX_TOUCHES`
  unconditionally in both branches (noted in the Phase I2 handoff, not yet fixed — see Section 5).

---

## 3. Recent changes

All on `feature/input` (most recent first). Phase I3 alternated one `feat`/`test`/`fix`/`docs(Task)`
commit per task with a companion `docs: mark Task N complete in plan_input.md` commit — the table
below lists the substantive commits only; each has a paired plan-doc-update commit immediately
after it in `git log`.

| Commit | Change |
|--------|--------|
| `4ec2cc3` | test(Task 740): `GamePad`/`GamePadCapabilities` coverage — completes Phase I3 |
| `e7c7ed5` | test(Task 739): `GamePadTriggers` coverage (dead-zone ctor, epsilon equality) |
| `6ee9393` | test(Task 738): `GamePadThumbSticks` coverage (`Circular`/`IndependentAxes` dead zones) |
| `f0733b7` | test(Task 737): `GamePadState` coverage |
| `094bd71` | test(Task 736): `GamePadButtons`/`GamePadDPad` coverage |
| `b5d0e89` | docs(Task 735): document event-driven vs. FNA's poll-driven gamepad architecture |
| `2ac38db` | feat(Task 734): `FNA_GAMEPAD_NUM_GAMEPADS` env override |
| `98a0106` | fix(Task 733): `GamePadState.ToString` matches FNA's default `ValueType` behavior |
| `a44c973` | feat(Task 732): FNA-faithful `GamePadThumbSticks`/`GamePadTriggers.GetHashCode` |
| `07a019c` | feat(Task 731): rename `FromButtons`→`FromButtonArray`, fix `GamePadDPad` signature |
| `2520109` | feat(Task 730): rework `GamePadCapabilities` to `getXProperty()`/NOXNA setters |
| `12bdbbd` | feat(Task 729): `GamePadState.PacketNumber` increment-on-change |
| `5a7488b` | feat(Task 728): map GamePad EXT buttons (Misc1/Paddles/TouchPad) |
| `e731774` | feat(Tasks 726,727): implement `GetGyroEXT`/`GetAccelerometerEXT` |
| `d15782a` | feat(Task 725): implement `GamePad::SetLightBarEXT` |
| `0ace4de` (docs) | last commit of the phase — `plan_input.md` marked Phase I3 complete |
| `7e4d066` | docs: rewrite NEXT.md as Phase I2 complete handoff |

- **Files added:** `GamePadCapabilities.cpp`, `GamePadButtonsTests.cpp`, `GamePadStateTests.cpp`,
  `GamePadThumbSticksTests.cpp`, `GamePadTriggersTests.cpp`, `GamePadTests.cpp`.
- **Files modified:** `SdlInputBridge.{hpp,cpp}`, `InputManager.{hpp,cpp}`, `GamePad.cpp`,
  `GamePadState.{hpp,cpp}`, `GamePadCapabilities.hpp`, `GamePadButtons.{hpp,cpp}`,
  `GamePadDPad.{hpp,cpp}`, `GamePadThumbSticks.cpp`, `GamePadTriggers.cpp`,
  `GamePadInputTests.cpp`, `plan_input.md`.
- **Behavior changed:** GamePad EXT features (LightBar/Gyro/Accelerometer/extra buttons) now work
  end-to-end from real SDL3 hardware; `PacketNumber` is live instead of hardcoded `0`.

---

## 4. Current blocker / main problem

**There is no hard blocker on the EasyGL track** — it builds clean and all 1866 unit tests pass.
One practical issue carries over, and Phase I4 is the next real gap:

1. **Vulkan/Bgfx build dirs are mis-wired (practical gotcha, unchanged from last handoff).**
   - **Symptom:** `cmake-build-vulkan/CMakeCache.txt` and `cmake-build-bgfx/CMakeCache.txt` have
     `CMAKE_HOME_DIRECTORY=/rv/data/development/github.com/openeggbert/cna` (the **sibling** repo),
     not this `cna_input` checkout. Building there compiles the wrong source tree and will not
     include input changes.
   - **Affected:** any Vulkan/Bgfx verification of input work.
   - **Fix pattern (already applied to `cmake-build-debug`):**
     `rm -rf cmake-build-<x> && cmake -S … -B … -G Ninja -DCNA_GRAPHICS_BACKEND=<X> …`.
     The Vulkan/Bgfx dirs have **not** been reconfigured.

2. **Next functional focus: Mouse dead behavior paths (Phase I4).** `MouseState` itself is faithful
   and populated from SDL3, but `Mouse::SetPosition` doesn't warp the OS cursor (state gets
   overwritten by the next motion event anyway), `IsRelativeMouseModeEXT` is a dead flag not backed
   by `SDL_SetWindowRelativeMouseMode`, and `ClickedEXT` never fires because `SdlInputBridge` never
   calls `Mouse::INTERNAL_onClicked`. `MouseCursor` also has CHECKLIST issues (wrong SPDX, missing
   `NOXNA` tags, no `FromTexture2D`/`Dispose`). See `plan_input.md` Phase I4 (tasks 745–755).
   Affected: `src/Microsoft/Xna/Framework/Input/Mouse.cpp`, `MouseCursor.{hpp,cpp}`,
   `src/CNA/Internal/Input/SdlInputBridge.cpp`.

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **Confirmed** | `cmake-build-vulkan` / `cmake-build-bgfx` caches point at the sibling `…/cna` repo (Section 4). |
| **Incomplete** | Mouse: `SetPosition` no warp; `IsRelativeMouseModeEXT` dead; `ClickedEXT` never fires; dead `INTERNAL_*` fields (Phase I4 — next focus). |
| **Incomplete** | Keyboard: `GetPressedKeys()` unordered; SDL keycode→Keys map missing F13–F24/Apps/Volume/locale fallbacks; `GetKeyFromScancodeEXT` is identity stub; no scancode mode (Phase I5). |
| **Needs verification** | `demo_input` text panel not visually confirmed: it builds and runs crash-free ~4s under the native backend, but no Wayland screenshot tool is available here and forcing the X11 driver makes SDL exit. A human at a display should run it and type. |
| **Intentional deviation** | `GamePadState.PacketNumber` is tracked at the raw `InputManager` layer (bumped on real connection/button/axis changes) rather than by comparing freshly-built `GamePadState`s like FNA's poll loop; documented in-source (`InputManager.cpp`). |
| **Intentional deviation** | `TouchPanel::GetState()` falls back to `InputManager::GetTouchState()` because CNA's `SdlInputBridge` is event-driven, not poll-driven like FNA's `UpdateTouchPanelState()`/`SetFinger` path; documented in-source (`TouchPanel.cpp`). |
| **Intentional deviation** | `TouchCollection`'s `IEnumerable<TouchLocation>::GetEnumerator()` is replaced by NOXNA-tagged `begin()`/`end()`, matching the precedent used by `GameComponentCollection`, `EffectAnnotationCollection`, etc. — no bespoke `Enumerator` struct exists anywhere in CNA. |
| **Intentional deviation** | `FNA_GAMEPAD_NUM_GAMEPADS` env override is clamped to 4 (CNA's `PlayerIndex` enum is frozen XNA API with exactly 4 values); FNA nominally allows values above 4 but its own comment says doing so also requires adding `PlayerIndex` names, which CLAUDE.md forbids. |
| **Minor, not yet fixed** | `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES` unconditionally in both branches (connected and fallback); FNA returns `0` for `MaximumTouchCount` when disconnected. Noted during Phase I2, not yet scheduled as its own task. |
| **Intentional deviation** | `TextInputEXT::TextInput` is `char`-based, so `SDL_EVENT_TEXT_INPUT` is forwarded **per UTF-8 byte** (appending rebuilds the UTF-8 string). FNA decodes to UTF-16 because C# strings are UTF-16. |
| **Pre-existing (not from input work)** | Working tree shows `D .claude/settings.json` (deleted) and untracked `vendor/wgpu-native/`. These predate this branch's input work and were **not** committed. |

---

## 6. Architecture notes

### Main modules (input)
| Layer | Location | Notes |
|-------|----------|-------|
| XNA public API | `include/Microsoft/Xna/Framework/Input/**` | Must match XNA 4.0 / FNA; FNA `EXT` additions tagged `NOXNA`. |
| Internal bridge | `src/CNA/Internal/Input/SdlInputBridge.cpp` | Single entry point `ProcessEvent(const SDL_Event&)`; knows SDL types. |
| Internal state | `src/CNA/Internal/Input/InputManager.cpp` | Accumulates per-device state; exposes `GetKeyboardState/GetMouseState/GetRawGamePadState/GetTouchState`. Event-driven, not poll-driven — see class doc comment. |
| Gesture engine | `src/CNA/Internal/Input/GestureDetector.cpp` | Full ported state machine — live, driven by `TouchPanel::INTERNAL_onTouchEvent`/`Update`. |
| FNA reference | `/rv/data/library/github.com/FNA-XNA/FNA/src/Input` | Authoritative behavior. |

### Data flow (input)
```
SDL_PollEvent  (Game::PollEvents, called once per frame from Game::Tick())
  → SdlInputBridge::ProcessEvent(event)        // switch on event.type
      → InputManager::Set*State(...)           // keyboard / mouse / gamepad / touch snapshot
      → TextInputEXT::INTERNAL_OnText*(...)     // text input / editing (Phase I1)
      → TouchPanel::INTERNAL_onTouchEvent(...)  // touch → GestureDetector (Phase I2)
      → TouchPanel::TouchDeviceExists = true    // on FINGER_DOWN, opens the Update() gate
  → Keyboard/Mouse/GamePad/TouchPanel::GetState()  read by game code each frame
      → GamePad::GetState builds a GamePadThumbSticks/Triggers/Buttons/DPad from the raw
        InputManager snapshot, applying dead-zone processing per the requested GamePadDeadZone
  → FrameworkDispatcher::Update() → TouchPanel::Update() → GestureDetector::OnUpdate()
```

### Invariants / boundaries that must stay stable
- `NOXNA` marks every non-XNA member in the `Microsoft::Xna` namespace; include `CNA/CNAHelper.hpp`
  where it is used. A wholly-non-XNA class uses `NOXNA class` (precedent: `ShaderEffect`).
- `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp` and `.cpp`.
- C# properties → `getXProperty()` / `setXProperty()` (no public fields for XNA API). FNA's
  `{ get; internal set; }` pattern maps to a `NOXNA`-tagged public setter (precedent:
  `TextInputEXT::setWindowHandleProperty`, `TouchPanel::setTouchDeviceExistsProperty`,
  `GamePadState::setPacketNumberProperty`, all of `GamePadCapabilities`'s 36 setters) — **not** a
  `friend`-based private setter; there is no precedent anywhere in CNA for the friend approach.
- XNA/FNA API names are frozen — no renames, and no adding enum values to frozen types (e.g.
  `PlayerIndex` stays at exactly 4 values even where FNA's own `GAMEPAD_COUNT` env var nominally
  allows going higher).
- FNA's public constructor signatures are frozen too — don't add convenience parameters to them
  even for otherwise-internal-set fields (e.g. `GamePadState`'s two public constructors take no
  `packetNumber` argument, matching FNA exactly; `PacketNumber` is set via the property setter
  after construction instead).
- The window handle is published by `GraphicsDevice` at create/destroy; `TextInputEXT` reads it via
  `getWindowHandleProperty()`. (Note: `Mouse.WindowHandle` and `TouchPanel.WindowHandle` are **not**
  yet published — a follow-up, since FNA sets all three together in `DisposeWindow`.)
- `SdlInputBridge::ProcessEvent` is the single SDL-event funnel; do not add a second event path.
- `GestureDetector`'s state machine lives in file-static variables with **no reset hook** — tests
  that drive it must fully release every finger they press and drain the gesture queue between
  cases (see `GestureDetectorTests.cpp`'s fixture for the established pattern).
- Private dead-zone-mode constructors (`GamePadThumbSticks`/`GamePadTriggers`'s 3-arg ctors,
  friended to `GamePad` only) are only reachable in tests via `InputManager::SetGamePadAxisValue` +
  `GamePad::GetState(playerIndex, deadZoneMode)` — see `GamePadThumbSticksTests.cpp`/
  `GamePadTriggersTests.cpp` for the pattern.

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

# Run the GamePad test suites specifically
./cmake-build-debug/CnaTests --gtest_filter='*GamePad*'

# Run the input demo (needs a display; F1 toggles text input, Esc exits)
./cmake-build-debug/cna_demo_input

# Verify a build dir's source wiring (should print …/cna_input)
grep CMAKE_HOME_DIRECTORY cmake-build-debug/CMakeCache.txt
```

> No project-wide lint/format target was observed. Doxygen config (`Doxyfile`) exists but is not a
> build/verify step.
>
> Note: builds in this environment occasionally trigger a large rebuild pass (`-- GLOB mismatch! /
> Re-running CMake...`) unrelated to the change being made — this has coincided with a sibling
> `sharp-runtime` checkout being edited concurrently in another session on this machine. If a build
> fails on an unrelated `sharp-runtime` file (e.g. `Decimal.cpp`, `DateTime.cpp`), it has always
> resolved itself on retry once the other session's edit settles; it is not this branch's problem.

---

## 8. Next smallest tasks

In priority order — the start of Phase I4 (Mouse). Each is one focused session.

1. **Task 745 — Wire `Mouse::SetPosition` to actually warp the OS cursor.**
   - Goal: call `SDL_WarpMouseInWindow` with the FNA relative-mode early-return guard and
     window/back-buffer coordinate scaling (`Mouse.cs:107–116`); stop relying solely on
     `InputManager` state mutation, which the next motion event just overwrites.
   - Files: `src/Microsoft/Xna/Framework/Input/Mouse.cpp`, `SdlInputBridge.{hpp,cpp}`.

2. **Task 746 — Implement `Mouse::IsRelativeMouseModeEXT` as a real property.**
   - Goal: back it with `SDL_SetWindowRelativeMouseMode`/`SDL_GetWindowRelativeMouseMode`; feed
     relative deltas into `GetMouseState`. Currently a dead `bool` with no SDL wiring.

3. **Task 747 — Resolve the back-buffer scaling fields in `Mouse::GetState`.**
   - Goal: either implement FNA-style scaling using `INTERNAL_BackBufferWidth/Height/WindowWidth/
     Height/MouseWheel`, or remove those dead fields and document the existing
     `TransformWindowToLogical` deviation (`SdlInputBridge.cpp:226`) instead.

4. **Task 748 — Wire `ClickedEXT`.**
   - Goal: have `SdlInputBridge` call `Mouse::INTERNAL_onClicked(button)` on mouse-button-down;
     currently never invoked.

Full Phase I4 task list (745–755, including `MouseCursor` CHECKLIST fixes, `FromTexture2D`,
`Dispose`, and the dedicated `MouseInputTests.cpp` test task 755) is in `plan_input.md`.

---

## 9. Do not do yet

- **Do not reconfigure or commit build directories.** `cmake-build-*` are gitignored; if Vulkan/Bgfx
  builds are needed, reconfigure them locally for `cna_input`, but do not commit the dirs.
- **Do not commit the pre-existing working-tree changes** (`D .claude/settings.json`, untracked
  `vendor/wgpu-native/`) — they are unrelated to the input work.
- **Do not start Keyboard phase (I5) before Mouse (I4)** — Mouse is the agreed next focus per
  `plan_input.md`'s phase ordering.
- **No API renames or namespace moves** — XNA/FNA names are frozen. This includes not adding values
  to frozen enums like `PlayerIndex`, and not adding parameters to FNA's public constructors even
  when convenient (see the `GamePadState`/`PacketNumber` precedent in Section 6).
- **No broad refactor of `SdlInputBridge` or `InputManager`** — keep the single-funnel design;
  add cases, do not restructure.
- **No graphics changes** on this branch — graphics is tracked in `GRAPHICS_TASKS.md` on its own track.
- **Do not change the `TextInputEXT` char-based callback signature** to widen it (UTF-16/char32) —
  that is a deliberate, documented deviation and would ripple through the public EXT API.
- **Do not silently "fix" the `GetCapabilities()` `MAX_TOUCHES` deviation** noted in Section 5 as part
  of unrelated work — it needs its own small task/PR since it changes public-facing behavior.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope beyond the task.

Context: on branch feature/input. Phase I1 (TextInputEXT), Phase I2 (Touch & gesture pipeline), and
Phase I3 (GamePad behavior/FNA fidelity, tasks 725-740) are all complete; 1866/1866 unit tests pass
on the EasyGL build (cmake-build-debug, correctly wired to cna_input). Mouse (Phase I4) is the next
focus: SetPosition doesn't warp the cursor, IsRelativeMouseModeEXT is a dead flag, ClickedEXT never
fires, and MouseCursor has CHECKLIST issues (wrong SPDX, missing NOXNA tags, no FromTexture2D/Dispose).

Next: Task 745 — wire Mouse::SetPosition to SDL_WarpMouseInWindow with FNA's relative-mode guard and
coordinate scaling (Mouse.cs:107-116). Then Task 746 (IsRelativeMouseModeEXT via
SDL_Set/GetWindowRelativeMouseMode) and Task 748 (wire ClickedEXT via Mouse::INTERNAL_onClicked)
follow the same bridge-then-XNA-API pattern used throughout Phase I3.

Build/test: cmake --build cmake-build-debug --target CnaTests -j$(nproc)
            ./cmake-build-debug/CnaTests --gtest_filter='*Mouse*'
Make one small verified change, then update NEXT.md (and plan_input.md task status) before finishing.
```
