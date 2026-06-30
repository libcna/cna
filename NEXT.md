# NEXT.md — CNA Project Handoff (feature/input branch)

> This handoff covers the **Input subsystem porting** work that is the active focus of the
> `feature/input` branch. The broader Graphics work is tracked separately in `GRAPHICS_TASKS.md`
> (and was the subject of the previous handoff); it is **not** the current focus here.

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
- **Current development phase:** **Phase I1 (TextInputEXT SDL3 wiring) is complete.** Next up is
  **Phase I2 (Touch & gesture pipeline)**, the highest-impact remaining gap.
- **Key architectural decisions:**
  - The authoritative behavioral reference is the FNA source tree at
    `/rv/data/library/github.com/FNA-XNA/FNA/src`.
  - Non-XNA members inside the `Microsoft::Xna` namespace are tagged with the `NOXNA` marker macro.
    FNA's `EXT`-suffixed additions are non-XNA; a wholly-non-XNA class is tagged `NOXNA class`.
  - Real input behavior flows through an internal bridge: SDL3 events →
    `CNA::Internal::Input::SdlInputBridge` → `InputManager` → XNA state objects.
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`. EasyGL is the primary/tested backend.

---

## 2. Current status

### Build
- **EasyGL build (`cmake-build-debug`):** clean. **This build dir is configured for `cna_input`**
  (it was previously mis-wired to the sibling `…/openeggbert/cna` checkout and was reconfigured).
- **Vulkan (`cmake-build-vulkan`) and Bgfx (`cmake-build-bgfx`) build dirs are still wired to the
  sibling `…/openeggbert/cna` repo** — see Section 4. They were not used or fixed for input work.

### Tests
- **1774 / 1774 unit tests pass** (last EasyGL run, `cmake-build-debug/CnaTests`). This includes:
  - `TextInputEXTTest` — 9 tests (static API, dispatch, window-handle property, null-guards).
  - `SdlInputBridgeTextInputTest` — 8 tests (synthetic SDL events through `ProcessEvent`:
    TEXT_INPUT/EDITING dispatch, control-char synthesis, Ctrl+V paste + suppress lifecycle).

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad, touch, **and a new text
  input panel**). Builds; runs crash-free.

### Recently implemented (Phase I1 — tasks 700–708, + extras)
- `TextInputEXT` fully wired to SDL3: `StartTextInput`/`StopTextInput`/`SetInputRectangle`/
  `IsTextInputActive`/`IsScreenKeyboardShown`.
- Window handle published to `TextInputEXT` by `GraphicsDevice` at window create/destroy.
- `SdlInputBridge` dispatches `SDL_EVENT_TEXT_INPUT` and `SDL_EVENT_TEXT_EDITING`, and synthesizes
  control-character `TextInput` for Home/End/Back/Tab/Enter/Delete and Ctrl+V (with suppress).
- `TextInputEXT` tagged as a `NOXNA` FNA extension; `WindowHandle` converted to a property.
- New `demo_input` text panel (font-free: buffer cells, last-byte bit-LEDs, IME draft, caret;
  F1 toggles Start/Stop).

### What does NOT work yet
- **Touch gestures are dead end-to-end** (Tap/DoubleTap/Hold/Drag/Flick/Pinch). The full
  `GestureDetector` is ported but unreachable — SDL finger events never reach it (Phase I2).
- **Mouse:** `SetPosition` does not warp the cursor; relative-mouse-mode is a dead flag; `ClickedEXT`
  never fires (Phase I4).
- **Keyboard:** `GetPressedKeys()` order is non-deterministic; SDL keycode→Keys map is incomplete;
  `GetKeyFromScancodeEXT` is an identity stub (Phase I5).
- **GamePad:** EXT buttons (Misc1/Paddles/TouchPad) never reach state; `PacketNumber` is always 0;
  LightBar/Gyro/Accelerometer EXT reads are stubs (Phase I3).
- **demo_input text panel is not visually verified** (see Section 5).

---

## 3. Recent changes

All on `feature/input` (most recent first):

| Commit | Change |
|--------|--------|
| `53bc88e` | Task 780: text input panel in `demo_input` (`InputDemo.hpp/.cpp`) |
| `41852ef` | Bridge-level test `SdlInputBridgeTextInputTests.cpp` (Tasks 704–706) |
| `d31e7d1` | Task 708: `TextInputEXTTests.cpp` — completes Phase I1 |
| `deb0835` | Task 706: control-char synthesis + Ctrl+V suppress (`SdlInputBridge.cpp`) |
| `d8f750a` | Task 705: dispatch `SDL_EVENT_TEXT_EDITING` |
| `3827c35` | Task 704: dispatch `SDL_EVENT_TEXT_INPUT` |
| `03a6e56` | Task 703: publish window handle to `TextInputEXT` (`GraphicsDevice.cpp`) |
| `02dea2d` | Task 707: `TextInputEXT` → `NOXNA` class + `WindowHandle` property |
| `483243a` | Tasks 701–702: `SetInputRectangle` + `Is*` queries → SDL3 |
| `07e99c4` | Task 700: `Start`/`StopTextInput` → SDL3 |
| `c3a1c9c` | Added `plan_input.md` (Phases I1–I7, tasks 700–783) |

- **Files added:** `plan_input.md`, `tests/.../Input/TextInputEXTTests.cpp`,
  `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`.
- **Files modified:** `TextInputEXT.hpp/.cpp`, `SdlInputBridge.cpp`, `GraphicsDevice.cpp`,
  `demo_input/src/InputDemo.hpp/.cpp`.
- **Behavior changed:** `SdlInputBridge` no longer drops key repeats outright (it re-emits text
  control chars on repeat while leaving pressed-key state unchanged).

---

## 4. Current blocker / main problem

**There is no hard blocker on the EasyGL track** — it builds clean and all 1774 unit tests pass.
Two practical issues matter for the next session:

1. **Vulkan/Bgfx build dirs are mis-wired (practical gotcha).**
   - **Symptom:** `cmake-build-vulkan/CMakeCache.txt` and `cmake-build-bgfx/CMakeCache.txt` have
     `CMAKE_HOME_DIRECTORY=/rv/data/development/github.com/openeggbert/cna` (the **sibling** repo),
     not this `cna_input` checkout. Building there compiles the wrong source tree and will not
     include input changes.
   - **Affected:** any Vulkan/Bgfx verification of input work.
   - **Already tried:** `cmake-build-debug` (EasyGL) hit the same problem and was fixed by
     `rm -rf cmake-build-debug && cmake -S … -B … -G Ninja -DCNA_GRAPHICS_BACKEND=EASYGL …`.
     The Vulkan/Bgfx dirs have **not** been reconfigured.

2. **Highest-impact functional gap: Touch gestures are non-functional end-to-end** (the main
   reason to start Phase I2). SDL finger events feed only `InputManager::SetTouchState`; they never
   call `TouchPanel::INTERNAL_onTouchEvent`, so `GestureDetector` never runs. See `plan_input.md`
   Phase I2 (tasks 710–722). Affected: `SdlInputBridge.cpp` (~finger-event cases), `TouchPanel.cpp`,
   `FrameworkDispatcher.cpp` (the `touchDeviceExists_` gate on `TouchPanel::Update()`).

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **Confirmed** | Touch gestures never fire from real input — SDL finger events bypass `TouchPanel::INTERNAL_onTouchEvent` / `GestureDetector`; `TouchPanel::Update()` is gated behind `touchDeviceExists_`, set only by the never-called `SetFinger`. |
| **Confirmed** | `cmake-build-vulkan` / `cmake-build-bgfx` caches point at the sibling `…/cna` repo (Section 4). |
| **Incomplete** | Mouse: `SetPosition` no warp; `IsRelativeMouseModeEXT` dead; `ClickedEXT` never fires; dead `INTERNAL_*` fields (Phase I4). |
| **Incomplete** | Keyboard: `GetPressedKeys()` unordered; SDL keycode→Keys map missing F13–F24/Apps/Volume/locale fallbacks; `GetKeyFromScancodeEXT` is identity stub; no scancode mode (Phase I5). |
| **Incomplete** | GamePad: EXT buttons unmapped; `PacketNumber`=0; LightBar/Gyro/Accelerometer stubs; `GamePadCapabilities` uses raw public fields instead of properties (Phase I3). |
| **Needs verification** | `demo_input` text panel not visually confirmed: it builds and runs crash-free ~4s under the native backend, but no Wayland screenshot tool is available here and forcing the X11 driver makes SDL exit. A human at a display should run it and type. |
| **Intentional deviation** | `TextInputEXT::TextInput` is `char`-based, so `SDL_EVENT_TEXT_INPUT` is forwarded **per UTF-8 byte** (appending rebuilds the UTF-8 string). FNA decodes to UTF-16 because C# strings are UTF-16. |
| **Pre-existing (not from input work)** | Working tree shows `D .claude/settings.json` (deleted) and untracked `vendor/wgpu-native/`. These predate this branch's input work and were **not** committed. |

---

## 6. Architecture notes

### Main modules (input)
| Layer | Location | Notes |
|-------|----------|-------|
| XNA public API | `include/Microsoft/Xna/Framework/Input/**` | Must match XNA 4.0 / FNA; FNA `EXT` additions tagged `NOXNA`. |
| Internal bridge | `src/CNA/Internal/Input/SdlInputBridge.cpp` | Single entry point `ProcessEvent(const SDL_Event&)`; knows SDL types. |
| Internal state | `src/CNA/Internal/Input/InputManager.cpp` | Accumulates per-device state; exposes `GetKeyboardState/GetMouseState/GetRawGamePadState/GetTouchState`. |
| Gesture engine | `src/CNA/Internal/Input/GestureDetector.cpp` | Full ported state machine — currently unreachable (Phase I2). |
| FNA reference | `/rv/data/library/github.com/FNA-XNA/FNA/src/Input` | Authoritative behavior. |

### Data flow (input)
```
SDL_PollEvent  (Game::PollEvents)
  → SdlInputBridge::ProcessEvent(event)        // switch on event.type
      → InputManager::Set*State(...)           // keyboard / mouse / gamepad / touch snapshot
      → TextInputEXT::INTERNAL_OnText*(...)     // text input / editing (Phase I1)
      → (Phase I2 target) TouchPanel::INTERNAL_onTouchEvent(...) → GestureDetector
  → Keyboard/Mouse/GamePad/TouchPanel::GetState()  read by game code each frame
```

### Invariants / boundaries that must stay stable
- `NOXNA` marks every non-XNA member in the `Microsoft::Xna` namespace; include `CNA/CNAHelper.hpp`
  where it is used. A wholly-non-XNA class uses `NOXNA class` (precedent: `ShaderEffect`).
- `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp` and `.cpp`.
- C# properties → `getXProperty()` / `setXProperty()` (no public fields for XNA API).
- XNA/FNA API names are frozen — no renames.
- The window handle is published by `GraphicsDevice` at create/destroy; `TextInputEXT` reads it via
  `getWindowHandleProperty()`. (Note: `Mouse.WindowHandle` and `TouchPanel.WindowHandle` are **not**
  yet published — a follow-up, since FNA sets all three together in `DisposeWindow`.)
- `SdlInputBridge::ProcessEvent` is the single SDL-event funnel; do not add a second event path.

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

# Run the input/text test suites specifically
./cmake-build-debug/CnaTests --gtest_filter='TextInputEXTTest.*:SdlInputBridgeTextInputTest.*:*Input*:*Touch*'

# Run the input demo (needs a display; F1 toggles text input, Esc exits)
./cmake-build-debug/cna_demo_input

# Verify a build dir's source wiring (should print …/cna_input)
grep CMAKE_HOME_DIRECTORY cmake-build-debug/CMakeCache.txt
```

> No project-wide lint/format target was observed. Doxygen config (`Doxyfile`) exists but is not a
> build/verify step.

---

## 8. Next smallest tasks

In priority order. Each is one focused session.

1. **Task 710 — Route SDL finger events into the gesture pipeline** (start of Phase I2).
   - Goal: in `SdlInputBridge::ProcessEvent` finger cases, also call
     `TouchPanel::INTERNAL_onTouchEvent(touchId, state, x, y, dx, dy)` (normalized coords + deltas)
     alongside the existing `InputManager::SetTouchState`, so `GestureDetector` receives input.
   - Files: `src/CNA/Internal/Input/SdlInputBridge.cpp`, `…/Input/Touch/TouchPanel.{hpp,cpp}`.
   - Verify: build `CnaTests`; add a bridge-level test feeding synthetic `SDL_EVENT_FINGER_*` and
     asserting a `GestureSample` is dequeued via `TouchPanel::ReadGesture` (mirror
     `SdlInputBridgeTextInputTests.cpp`). `./cmake-build-debug/CnaTests --gtest_filter='*Gesture*'`.

2. **Task 712 — Fix the `TouchPanel::Update()` gate so gestures can tick.**
   - Goal: `FrameworkDispatcher.cpp` only calls `TouchPanel::Update()` when `touchDeviceExists_` is
     true, but that flag is set solely by the never-called `SetFinger`. Set it on the real input path.
   - Files: `…/Input/Touch/TouchPanel.cpp`, `…/Framework/FrameworkDispatcher.cpp`.
   - Verify: extend the Task-710 gesture test to require `Hold`/`Flick` (which need `OnUpdate`).

3. **Task 717 — GestureDetector CHECKLIST hygiene (tiny).**
   - Goal: change `GestureDetector` SPDX `MIT`→`MS-PL`; remove forbidden "ported from FNA / MonoGame"
     comments (CLAUDE.md forbids them).
   - Files: `include/CNA/Internal/Input/GestureDetector.hpp`, `src/CNA/Internal/Input/GestureDetector.cpp`.
   - Verify: `cmake --build cmake-build-debug --target CNA`.

4. **Task 715 — `TouchLocation::ToString`/`GetHashCode` FNA fidelity + tests.**
   - Goal: `ToString()` → `"{Position:…}"`; reconcile `GetHashCode()` to FNA's `Id + Position`.
   - Files: `…/Input/Touch/TouchLocation.{hpp,cpp}`, `tests/.../Input/TouchInputTests.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='*Touch*'`.

5. **Task 716 — `TouchPanel::ReadGesture()` throws `System::InvalidOperationException`** (instead of
   `std::logic_error`); add a test for the empty-queue throw.
   - Files: `…/Input/Touch/TouchPanel.cpp`, `tests/.../Input/TouchInputTests.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='*Touch*'`.

---

## 9. Do not do yet

- **Do not reconfigure or commit build directories.** `cmake-build-*` are gitignored; if Vulkan/Bgfx
  builds are needed, reconfigure them locally for `cna_input`, but do not commit the dirs.
- **Do not commit the pre-existing working-tree changes** (`D .claude/settings.json`, untracked
  `vendor/wgpu-native/`) — they are unrelated to the input work.
- **Do not start Mouse/Keyboard/GamePad phases (I3–I5) before Touch (I2)** — touch is the largest
  functional gap and the agreed next focus.
- **No API renames or namespace moves** — XNA/FNA names are frozen.
- **No broad refactor of `SdlInputBridge` or `InputManager`** — keep the single-funnel design;
  add cases, do not restructure.
- **No graphics changes** on this branch — graphics is tracked in `GRAPHICS_TASKS.md` on its own track.
- **Do not change the `TextInputEXT` char-based callback signature** to widen it (UTF-16/char32) —
  that is a deliberate, documented deviation and would ripple through the public EXT API.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope beyond the task.

Context: on branch feature/input. Phase I1 (TextInputEXT SDL3 wiring, tasks 700-708) is complete;
1774/1774 unit tests pass on the EasyGL build (cmake-build-debug, which is correctly wired to
cna_input). Touch gestures are dead end-to-end (Phase I2) — that is the next focus.

Next: Task 710 — in SdlInputBridge::ProcessEvent finger cases, also call
TouchPanel::INTERNAL_onTouchEvent(...) so GestureDetector receives input. Then add a bridge-level
test feeding synthetic SDL_EVENT_FINGER_* events and asserting a GestureSample via
TouchPanel::ReadGesture (mirror SdlInputBridgeTextInputTests.cpp).

Build/test: cmake --build cmake-build-debug --target CnaTests -j$(nproc)
            ./cmake-build-debug/CnaTests --gtest_filter='*Touch*:*Gesture*'
Make one small verified change, then update NEXT.md (and plan_input.md task status) before finishing.
```
