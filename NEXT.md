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
- **Current development phase:** **Phase I1 (TextInputEXT) and Phase I2 (Touch & gesture pipeline)
  are both complete.** Next up is **Phase I3 (GamePad behavior and FNA fidelity)**.
- **Key architectural decisions:**
  - The authoritative behavioral reference is the FNA source tree at
    `/rv/data/library/github.com/FNA-XNA/FNA/src`.
  - Non-XNA members inside the `Microsoft::Xna` namespace are tagged with the `NOXNA` marker macro.
    FNA's `EXT`-suffixed additions are non-XNA; a wholly-non-XNA class is tagged `NOXNA class`.
  - Real input behavior flows through an internal bridge: SDL3 events →
    `CNA::Internal::Input::SdlInputBridge` → `InputManager` → XNA state objects. Touch additionally
    flows `SdlInputBridge` → `TouchPanel::INTERNAL_onTouchEvent` → `GestureDetector`.
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`. EasyGL is the primary/tested backend.

---

## 2. Current status

### Build
- **EasyGL build (`cmake-build-debug`):** clean.
- **Vulkan (`cmake-build-vulkan`) and Bgfx (`cmake-build-bgfx`) build dirs are still wired to the
  sibling `…/openeggbert/cna` repo** — see Section 4. They were not used or fixed for input work.

### Tests
- **1812 / 1812 unit tests pass** (EasyGL build, `cmake-build-debug/CnaTests`). Touch/gesture-related
  additions this phase:
  - `GestureDetectorTest` — 12 tests (Tap, DoubleTap timing, Hold ≥1s, Horizontal/Vertical/Free
    drag thresholds, Flick velocity, Pinch + PinchComplete), driven through
    `TouchPanel::INTERNAL_onTouchEvent`/`Update`.
  - `TouchInputTest` (extended) — FIFO gesture queue, empty-queue throw, `GetCapabilities`,
    `EnabledGestures`/`DisplayWidth`/`DisplayHeight`/`DisplayOrientation` get/set.
  - `TouchCollectionTest`, `TouchLocationTest`, `GestureSampleTest`, `TouchPanelCapabilitiesTest` —
    direct constructor/method/operator coverage.

### Apps / libraries available
- `CNA` static library (XNA 4.0 API surface).
- `CnaTests` (Google Test unit suite).
- `cna_demo_input` — interactive input demo (keyboard, mouse, gamepad, touch, text input panel).
  Builds; runs crash-free.

### Recently implemented (Phase I2 — tasks 710–722)
- SDL finger events (`FINGER_DOWN/MOTION/UP`) now call `TouchPanel::INTERNAL_onTouchEvent` alongside
  `InputManager::SetTouchState`, so `GestureDetector` receives real input.
- `FINGER_DOWN` sets `TouchPanel::TouchDeviceExists`, opening the `Update()` gate in
  `FrameworkDispatcher` so `GestureDetector::OnUpdate()` (Hold/Flick) actually runs.
- `GraphicsDevice` publishes `TouchPanel::DisplayWidth`/`DisplayHeight` from the back buffer
  (constructor + `Reset()`), needed for `INTERNAL_onTouchEvent`'s normalized→pixel scaling.
- `TouchPanel::SetFinger` fixed to carry previous state/position (5-arg `TouchLocation` ctor), so
  `TryGetPreviousLocation` works for touches routed through it.
- FNA-fidelity fixes: `TouchLocation::ToString`/`GetHashCode`, `TouchPanel::ReadGesture` throws
  `System::InvalidOperationException`, `GestureDetector` SPDX/comment hygiene, `TouchCollection`
  gained a settable indexer and NOXNA-tagged `empty()`/`begin()`/`end()`.
- All gestures (Tap/DoubleTap/Hold/HorizontalDrag/VerticalDrag/FreeDrag/Flick/Pinch/PinchComplete)
  now fire from real SDL touch input and are covered by tests.

### What does NOT work yet
- **Mouse:** `SetPosition` does not warp the cursor; relative-mouse-mode is a dead flag; `ClickedEXT`
  never fires (Phase I4).
- **Keyboard:** `GetPressedKeys()` order is non-deterministic; SDL keycode→Keys map is incomplete;
  `GetKeyFromScancodeEXT` is an identity stub (Phase I5).
- **GamePad:** EXT buttons (Misc1/Paddles/TouchPad) never reach state; `PacketNumber` is always 0;
  LightBar/Gyro/Accelerometer EXT reads are stubs (Phase I3 — next focus).
- **demo_input text panel is not visually verified** (see Section 5; carried over from Phase I1,
  unrelated to touch).
- **Minor, out of scope for Phase I2:** `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES`
  unconditionally in both branches; FNA zeroes `MaximumTouchCount` when disconnected. Noted, not
  fixed — see Section 5.

---

## 3. Recent changes

All on `feature/input` (most recent first):

| Commit | Change |
|--------|--------|
| `6591ee1` | docs: mark Phase I2 (tasks 710–722) complete in `plan_input.md` |
| `a61486b` | test(Tasks 721,722): `TouchPanel`/`TouchCollection`/`TouchLocation`/`GestureSample` coverage |
| `3bd6f72` | test(Task 720): `GestureDetector` test suite |
| `16889fd` | feat(Task 718): `TouchCollection` settable indexer + NOXNA cleanup |
| `779d397` | chore(Task 717): `GestureDetector` CHECKLIST hygiene |
| `3951da0` | fix(Task 715): `TouchLocation::ToString`/`GetHashCode` match FNA |
| `e89f0a2` | fix(Tasks 713,714,716,719): `TouchPanel` FNA-fidelity fixes |
| `4ce1a03` | feat(Task 711): set `TouchPanel` `DisplayWidth`/`Height` from the back buffer |
| `d77efc5` | feat(Tasks 710,712): wire SDL finger events into `GestureDetector` |
| `cbfbbae` | docs: rewrite NEXT.md as input-focused handoff (Phase I1 complete) |

- **Files added:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`.
- **Files modified:** `SdlInputBridge.cpp`, `GraphicsDevice.cpp`, `TouchPanel.{hpp,cpp}`,
  `TouchLocation.cpp`, `GestureDetector.{hpp,cpp}`, `TouchCollection.{hpp,cpp}`,
  `TouchInputTests.cpp`, `plan_input.md`.
- **Behavior changed:** touch gestures now fire end-to-end from real SDL input; previously
  `GestureDetector` was fully ported but entirely unreachable.

---

## 4. Current blocker / main problem

**There is no hard blocker on the EasyGL track** — it builds clean and all 1812 unit tests pass.
One practical issue carries over, and Phase I3 is the next real gap:

1. **Vulkan/Bgfx build dirs are mis-wired (practical gotcha, unchanged from last handoff).**
   - **Symptom:** `cmake-build-vulkan/CMakeCache.txt` and `cmake-build-bgfx/CMakeCache.txt` have
     `CMAKE_HOME_DIRECTORY=/rv/data/development/github.com/openeggbert/cna` (the **sibling** repo),
     not this `cna_input` checkout. Building there compiles the wrong source tree and will not
     include input changes.
   - **Affected:** any Vulkan/Bgfx verification of input work.
   - **Fix pattern (already applied to `cmake-build-debug`):**
     `rm -rf cmake-build-<x> && cmake -S … -B … -G Ninja -DCNA_GRAPHICS_BACKEND=<X> …`.
     The Vulkan/Bgfx dirs have **not** been reconfigured.

2. **Next functional focus: GamePad EXT features and fidelity gaps (Phase I3).** CNA drives real
   SDL3 gamepads (hotplug, rumble, capability probing all genuine), but `SetLightBarEXT`,
   `GetGyroEXT`, `GetAccelerometerEXT` are stubs, EXT buttons (Misc1/Paddles/TouchPad) never reach
   `GamePadState.Buttons`, and `PacketNumber` is hardcoded to `0`. See `plan_input.md` Phase I3
   (tasks 725–740). Affected: `src/Microsoft/Xna/Framework/Input/GamePad.cpp`,
   `src/CNA/Internal/Input/SdlInputBridge.cpp` (gamepad button/axis conversion).

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **Confirmed** | `cmake-build-vulkan` / `cmake-build-bgfx` caches point at the sibling `…/cna` repo (Section 4). |
| **Incomplete** | GamePad: EXT buttons unmapped; `PacketNumber`=0; LightBar/Gyro/Accelerometer stubs; `GamePadCapabilities` uses raw public fields instead of properties (Phase I3 — next focus). |
| **Incomplete** | Mouse: `SetPosition` no warp; `IsRelativeMouseModeEXT` dead; `ClickedEXT` never fires; dead `INTERNAL_*` fields (Phase I4). |
| **Incomplete** | Keyboard: `GetPressedKeys()` unordered; SDL keycode→Keys map missing F13–F24/Apps/Volume/locale fallbacks; `GetKeyFromScancodeEXT` is identity stub; no scancode mode (Phase I5). |
| **Needs verification** | `demo_input` text panel not visually confirmed: it builds and runs crash-free ~4s under the native backend, but no Wayland screenshot tool is available here and forcing the X11 driver makes SDL exit. A human at a display should run it and type. |
| **Intentional deviation** | `TouchPanel::GetState()` falls back to `InputManager::GetTouchState()` because CNA's `SdlInputBridge` is event-driven, not poll-driven like FNA's `UpdateTouchPanelState()`/`SetFinger` path; documented in-source (`TouchPanel.cpp`). |
| **Intentional deviation** | `TouchCollection`'s `IEnumerable<TouchLocation>::GetEnumerator()` is replaced by NOXNA-tagged `begin()`/`end()`, matching the precedent used by `GameComponentCollection`, `EffectAnnotationCollection`, etc. — no bespoke `Enumerator` struct exists anywhere in CNA. |
| **Minor, not yet fixed** | `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES` unconditionally in both branches (connected and fallback); FNA returns `0` for `MaximumTouchCount` when disconnected. Out of scope for tasks 710–722; worth a follow-up task. |
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
| Gesture engine | `src/CNA/Internal/Input/GestureDetector.cpp` | Full ported state machine — now live, driven by `TouchPanel::INTERNAL_onTouchEvent`/`Update`. |
| FNA reference | `/rv/data/library/github.com/FNA-XNA/FNA/src/Input` | Authoritative behavior. |

### Data flow (input)
```
SDL_PollEvent  (Game::PollEvents)
  → SdlInputBridge::ProcessEvent(event)        // switch on event.type
      → InputManager::Set*State(...)           // keyboard / mouse / gamepad / touch snapshot
      → TextInputEXT::INTERNAL_OnText*(...)     // text input / editing (Phase I1)
      → TouchPanel::INTERNAL_onTouchEvent(...)  // touch → GestureDetector (Phase I2, now live)
      → TouchPanel::TouchDeviceExists = true    // on FINGER_DOWN, opens the Update() gate
  → Keyboard/Mouse/GamePad/TouchPanel::GetState()  read by game code each frame
  → FrameworkDispatcher::Update() → TouchPanel::Update() → GestureDetector::OnUpdate()
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
- `GestureDetector`'s state machine lives in file-static variables with **no reset hook** — tests
  that drive it must fully release every finger they press and drain the gesture queue between
  cases (see `GestureDetectorTests.cpp`'s fixture for the established pattern).

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

# Run the touch/gesture test suites specifically
./cmake-build-debug/CnaTests --gtest_filter='*Touch*:*Gesture*'

# Run the input demo (needs a display; F1 toggles text input, Esc exits)
./cmake-build-debug/cna_demo_input

# Verify a build dir's source wiring (should print …/cna_input)
grep CMAKE_HOME_DIRECTORY cmake-build-debug/CMakeCache.txt
```

> No project-wide lint/format target was observed. Doxygen config (`Doxyfile`) exists but is not a
> build/verify step.

---

## 8. Next smallest tasks

In priority order — the start of Phase I3 (GamePad). Each is one focused session.

1. **Task 725 — `GamePad::SetLightBarEXT`.**
   - Goal: add `SdlInputBridge::SetLightBar` → `SDL_SetGamepadLED(device, R, G, B)`; wire from the
     no-op at `GamePad.cpp:59–62`.
   - Files: `include/CNA/Internal/Input/SdlInputBridge.hpp`, `src/CNA/Internal/Input/SdlInputBridge.cpp`,
     `src/Microsoft/Xna/Framework/Input/GamePad.cpp`.
   - Verify: build `CnaTests`; extend `GamePadInputTests.cpp`.

2. **Task 726/727 — `GetGyroEXT`/`GetAccelerometerEXT` via `SDL_GetGamepadSensorData`.**
   - Goal: replace the `return false` stubs at `GamePad.cpp:69–81` (caps already report
     `HasGyroEXT`/`HasAccelerometerEXT`, so the capability side is done — only the read path is missing).
   - Files: `src/Microsoft/Xna/Framework/Input/GamePad.cpp`, `SdlInputBridge.{hpp,cpp}`.

3. **Task 728 — Map EXT buttons (Misc1/Paddles/TouchPad).**
   - Goal: extend `InputManager::GamePadButton` enum + `try_convert_sdl_gamepad_button`
     (`SdlInputBridge.cpp:85–122`) so `MISC1`/`RIGHT_PADDLE1`/`LEFT_PADDLE1`/`RIGHT_PADDLE2`/
     `LEFT_PADDLE2`/`TOUCHPAD` reach `GamePadState.Buttons`.

4. **Task 729 — `PacketNumber` increment-on-change.**
   - Goal: track previous per-`PlayerIndex` state, bump `PacketNumber` when `GamePadState != previous`
     (FNA semantics); currently hardcoded `0`.

Full Phase I3 task list (725–740, including `GamePadCapabilities` property rework and the unit-test
tasks 736–740) is in `plan_input.md`.

---

## 9. Do not do yet

- **Do not reconfigure or commit build directories.** `cmake-build-*` are gitignored; if Vulkan/Bgfx
  builds are needed, reconfigure them locally for `cna_input`, but do not commit the dirs.
- **Do not commit the pre-existing working-tree changes** (`D .claude/settings.json`, untracked
  `vendor/wgpu-native/`) — they are unrelated to the input work.
- **Do not start Mouse/Keyboard phases (I4–I5) before GamePad (I3)** — GamePad is the agreed next
  focus per `plan_input.md`'s phase ordering.
- **No API renames or namespace moves** — XNA/FNA names are frozen.
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

Context: on branch feature/input. Phase I1 (TextInputEXT) and Phase I2 (Touch & gesture pipeline,
tasks 710-722) are both complete; 1812/1812 unit tests pass on the EasyGL build (cmake-build-debug,
correctly wired to cna_input). GamePad (Phase I3) is the next focus: SetLightBarEXT/GetGyroEXT/
GetAccelerometerEXT are stubs, EXT buttons never reach GamePadState, and PacketNumber is hardcoded 0.

Next: Task 725 — implement GamePad::SetLightBarEXT via a new SdlInputBridge::SetLightBar that calls
SDL_SetGamepadLED(device, R, G, B), wired from the no-op in GamePad.cpp. Then Tasks 726/727 (gyro/
accelerometer reads) and 728 (EXT button mapping) follow the same bridge-then-XNA-API pattern.

Build/test: cmake --build cmake-build-debug --target CnaTests -j$(nproc)
            ./cmake-build-debug/CnaTests --gtest_filter='*GamePad*'
Make one small verified change, then update NEXT.md (and plan_input.md task status) before finishing.
```
