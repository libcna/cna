# Input Implementation Task Plan

> Goal: `Microsoft::Xna::Framework::Input` and `Microsoft::Xna::Framework::Input::Touch`
> fully ported from FNA — not just API surface, but **FNA-faithful runtime behavior**
> wired to SDL3, CHECKLIST-compliant, and covered by unit + integration tests.
>
> The authoritative reference is the FNA source tree at
> `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/` and
> `/rv/data/library/github.com/FNA-XNA/FNA/src/FNAPlatform/SDL3_FNAPlatform.cs`.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Not started |
| 🔄 | In progress |
| ✅ | Done |
| ⛔ | Blocked / deferred |
| ℹ️  | Known limitation (not a bug) |
| ⚠️  | Partial / no-op / stub |

> **Task numbering:** Input tasks occupy the **700+ band**. `GRAPHICS_TASKS.md` owns 100–699.
> Phase titles here (`Phase I1…I7`) are local to this document.

---

## Current state summary

The input value-type layer is in good shape. The enums (`Buttons`, `ButtonState`, `Keys`,
`KeyState`, `GamePadType`, `GamePadDeadZone`, `GestureType`, `TouchLocationState`) match FNA
values exactly. The dead-zone / clamp / button-packing math in `GamePadThumbSticks`,
`GamePadTriggers`, `GamePadDeadZone`, and `GamePadState` is **line-for-line identical** to FNA.
The full `GestureDetector` state machine is ported. Keyboard, Mouse, and GamePad **are** driven
by real SDL3 events through `CNA::Internal::Input::SdlInputBridge` → `InputManager`.

What this plan addresses: the **behavior and wiring gaps** the API-surface audit (AUDIT.md) did
not capture — dead/stub paths, unwired SDL3 features, fidelity deviations from FNA, CHECKLIST
violations in the internal layer, and very thin test coverage.

### Per-device wiring status (entry point: `SdlInputBridge::ProcessEvent`, driven by `Game::PollEvents`)

| Device | API surface | SDL3 wiring | Behavior fidelity | Tests |
|--------|-------------|-------------|-------------------|-------|
| Keyboard | ✅ complete | ✅ key down/up → `KeyboardState` | ⚠️ incomplete keycode map; no scancode mode; `GetPressedKeys` unordered; identity `GetKeyFromScancodeEXT` | ⚠️ minimal |
| Mouse | ✅ complete | ✅ motion/button/wheel → `MouseState` | ⚠️ `SetPosition` doesn't warp; relative-mode dead; `ClickedEXT` never fires; dead `INTERNAL_*` fields | ❌ none |
| GamePad | ✅ complete | ✅ hotplug + button/axis + rumble + caps | ⚠️ EXT buttons unmapped; `PacketNumber`=0; LED/gyro/accel stubbed | ⚠️ integration only |
| Touch | ✅ complete | ⚠️ fingers → `GetState` only; **gesture pipeline dead** | ⚠️ `SetFinger`/`Update` unreachable; `DisplayWidth/Height` never set | ⚠️ 3 tests |
| TextInput | ✅ surface | ❌ **fully unwired**; all bodies no-op | ⚠️ events never raised | ❌ none |
| MouseCursor | ⚠️ MonoGame ext | ✅ system cursors only | ⚠️ no `FromTexture2D`/`Dispose`; SPDX/NOXNA wrong | ❌ none |

---

## Phase I1 — TextInputEXT: SDL3 backend wiring

> **Root cause:** every `TextInputEXT` method is a no-op stub; `SdlInputBridge` ignores
> `SDL_EVENT_TEXT_INPUT` / `SDL_EVENT_TEXT_EDITING`. Text entry and IME are entirely
> non-functional. FNA reference: `TextInputEXT.cs`, `SDL3_FNAPlatform.cs:903–953, 1162–1204`.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 700 | Wire `StartTextInput()`/`StopTextInput()` to `SDL_StartTextInput(window)` / `SDL_StopTextInput(window)` using `WindowHandle` (cast `uintptr_t`→`SDL_Window*`). Replace empty bodies at `TextInputEXT.cpp:25–31` | ⬜ | |
| 701 | Wire `SetInputRectangle(Rectangle)` to `SDL_SetTextInputArea`. Replace empty body `TextInputEXT.cpp:33–35` | ⬜ | |
| 702 | Wire `IsTextInputActive()`→`SDL_TextInputActive`, `IsScreenKeyboardShown(IntPtr)`→`SDL_ScreenKeyboardShown`. Replace `return false` stubs `TextInputEXT.cpp:10–23` | ⬜ | |
| 703 | Populate `TextInputEXT::WindowHandle` at window creation and clear it at destruction (mirror FNA `SDL3_FNAPlatform.cs:463–465`); currently never assigned | ⬜ | |
| 704 | Handle `SDL_EVENT_TEXT_INPUT` in `SdlInputBridge::ProcessEvent`: UTF-8 decode `event.text.text` → per-char `TextInputEXT::INTERNAL_OnTextInput` (FNA `1162–1184`) | ⬜ | |
| 705 | Handle `SDL_EVENT_TEXT_EDITING`: decode → `INTERNAL_OnTextEditing(text, start, length)`, including the empty/null composition path (FNA `1186–1204`) | ⬜ | |
| 706 | Control-character synthesis on `KEY_DOWN` (incl. repeat): emit `TextInput` for Home/End/Back/Tab/Enter/Delete and Ctrl+V, with a `textInputSuppress` flag to avoid double paste. Port `TextInputBindings`/`TextInputCharacters` (FNA `FNAPlatform.cs:261–280`, `SDL3_FNAPlatform.cs:903–953`). Note: bridge currently drops key repeats entirely (`SdlInputBridge.cpp:570–573`) | ⬜ | |
| 707 | TextInputEXT CHECKLIST fixes: add `#include "CNA/CNAHelper.hpp"`; `NOXNA`-tag `INTERNAL_OnTextInput`/`INTERNAL_OnTextEditing` (FNA-internal, non-XNA); convert public field `WindowHandle` → `getWindowHandleProperty()`/`setWindowHandleProperty()` | ⬜ | |
| 708 | New `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`: event dispatch via `INTERNAL_OnTextInput`/`OnTextEditing`, subscriber lambda fires, `IsTextInputActive` default, `WindowHandle` round-trip | ⬜ | |

---

## Phase I2 — Touch and gesture pipeline wiring

> **Root cause:** SDL finger events feed only `InputManager::SetTouchState` (`SdlInputBridge.cpp:632–663`),
> so `TouchPanel::INTERNAL_onTouchEvent` / `SetFinger` / `GestureDetector::On*` have **zero callers**.
> `TouchPanel::Update()` is gated behind `touchDeviceExists_`, which only `SetFinger` sets — so
> `OnUpdate()` (Hold/Flick velocity) never runs. The full `GestureDetector` exists but is unreachable.
> No gesture (Tap/DoubleTap/Hold/Drag/Flick/Pinch) can fire from real input.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 710 | In `SdlInputBridge.cpp:632–663`, additionally call `TouchPanel::INTERNAL_onTouchEvent(touchId, state, x, y, dx, dy)` (normalized coords + deltas) alongside `InputManager::SetTouchState`, so `GestureDetector` receives input | ⬜ | |
| 711 | Set `TouchPanel::DisplayWidth`/`DisplayHeight` from the active window/back-buffer size (never set today; required for `INTERNAL_onTouchEvent` pixel scaling) | ⬜ | |
| 712 | Fix the `Update()` gate: `FrameworkDispatcher.cpp:62` only calls `TouchPanel::Update()` when `touchDeviceExists_` is true, but that flag is set solely by the never-called `SetFinger`. Set it on the real input path so `OnUpdate()` (Hold/Flick) runs | ⬜ | |
| 713 | Fix `TouchPanel::SetFinger` (Moved/Released branches, `.cpp:191,227–231`) to use the 5-arg `TouchLocation` ctor carrying `prevState`/`prevPosition` (match FNA `TouchPanel.cs:209–215`), so `TryGetPreviousLocation` can succeed | ⬜ | |
| 714 | Reconcile `TouchPanel::GetState()` (`.cpp:104–122`) with FNA: either populate `touches_` via the platform path (FNA iterates `touches[]` from `SetFinger`) or explicitly document the `InputManager` fallback deviation | ⬜ | |
| 715 | `TouchLocation::ToString()` → emit `"{Position:" + Position + "}"` (FNA format, currently `"[TouchLocation Id=N]"`); reconcile `GetHashCode()` to FNA's `Id + Position` formula (currently `id ^ state*31`) | ⬜ | |
| 716 | `TouchPanel::ReadGesture()` → throw `System::InvalidOperationException` (already in sharp-runtime) instead of `std::logic_error` (`.cpp:128`) | ⬜ | |
| 717 | `CNA::Internal::Input::GestureDetector` hygiene: change SPDX `MIT`→`MS-PL`; remove forbidden "Ported from FNA / taken from MonoGame" comments per CLAUDE.md | ⬜ | |
| 718 | `TouchCollection`: add XNA `GetEnumerator()` + nested `Enumerator` struct (or document the `begin/end` deviation); `NOXNA`-tag `empty()`; add settable indexer to mirror FNA `this[int] set` | ⬜ | |
| 719 | `TouchPanel::TouchDeviceExists` accessors are FNA-internal but public without `NOXNA` — tag `NOXNA` / reduce visibility | ⬜ | |
| 720 | New `GestureDetector` test suite: Tap, DoubleTap timing, Hold ≥1s, Horizontal/Vertical/Free drag thresholds, Flick velocity (MIN_FLICK_VELOCITY=100), Pinch + PinchComplete — driving `INTERNAL_onTouchEvent` + `Update` | ⬜ | |
| 721 | TouchPanel tests: `ReadGesture` (+ empty-throw), `IsGestureAvailable`, `EnqueueGesture`, `GetCapabilities`, `EnabledGestures` get/set, `DisplayWidth/Height/Orientation` | ⬜ | |
| 722 | Direct tests for `TouchCollection` (Contains/FindById out-ref/CopyTo/IndexOf/Add/Clear/Remove/RemoveAt/Insert/IsConnected/operator[]), `TouchLocation` (`==`/`!=`/`Equals` equal+unequal, `GetHashCode` consistency, `ToString`, out-ref `TryGetPreviousLocation`), `GestureSample`, `TouchPanelCapabilities` | ⬜ | |

---

## Phase I3 — GamePad behavior and FNA fidelity

> CNA drives real SDL3 gamepads (hotplug, rumble, capability probing all genuine). Gaps are
> unwired EXT features and a few fidelity deviations. FNA reference: `GamePad.cs`,
> `SDL3_FNAPlatform.cs` (GetGamePadState / PacketNumber / sensors).

| # | Task | Status | Notes |
|---|------|--------|-------|
| 725 | Implement `GamePad::SetLightBarEXT`: add `SdlInputBridge::SetLightBar` → `SDL_SetGamepadLED(device, R, G, B)`; wire from the no-op at `GamePad.cpp:59–62` | ⬜ | |
| 726 | Implement `GamePad::GetGyroEXT` via `SDL_GetGamepadSensorData(SDL_SENSOR_GYRO)`; replace `return false` stub `GamePad.cpp:69–74` (caps already report `HasGyroEXT`) | ⬜ | |
| 727 | Implement `GamePad::GetAccelerometerEXT` via `SDL_GetGamepadSensorData(SDL_SENSOR_ACCEL)`; replace stub `GamePad.cpp:76–81` | ⬜ | |
| 728 | Map EXT buttons: extend `InputManager::GamePadButton` enum + `try_convert_sdl_gamepad_button` (`SdlInputBridge.cpp:85–122`) for `MISC1`→`Misc1EXT`, `RIGHT_PADDLE1`→`Paddle1EXT`, `LEFT_PADDLE1`→`Paddle2EXT`, `RIGHT_PADDLE2`→`Paddle3EXT`, `LEFT_PADDLE2`→`Paddle4EXT`, `TOUCHPAD`→`TouchPadEXT`, so they reach `GamePadState.Buttons` | ⬜ | |
| 729 | Implement `PacketNumber` increment-on-change: track previous per-`PlayerIndex` state, bump when `GamePadState != previous` (FNA semantics); currently hardcoded `0` | ⬜ | |
| 730 | Rework `GamePadCapabilities`: replace 34 raw public mutable `bool` fields with `getXProperty()` getters + internal (private) setters; `NOXNA`-tag the 10 EXT capability properties; add `#include "CNA/CNAHelper.hpp"` | ⬜ | |
| 731 | Rename `GamePadButtons::FromButtons`→`FromButtonArray` and `GamePadDPad::FromButtons`→`FromButtonArray` to match FNA internal names; reconcile `GamePadDPad`'s single-`Buttons` signature with FNA's `params Buttons[]` | ⬜ | |
| 732 | Make `GamePadThumbSticks::GetHashCode` (`Left + 37*Right`) and `GamePadTriggers::GetHashCode` (`Left + Right`) FNA-faithful — requires `Vector2::GetHashCode` / float-hash helper (add to sharp-runtime / Vector2 if missing). Currently truncated `*1000` formulas | ⬜ | |
| 733 | Decide `GamePadState::ToString` fidelity: FNA returns the fully-qualified type name; CNA returns `"[GamePadState IsConnected=…]"`. Match FNA or document the intentional deviation | ⬜ | |
| 734 | Port `GAMEPAD_COUNT` / `FNA_GAMEPAD_NUM_GAMEPADS` env override (currently hardcoded to 4 slots in bridge/InputManager) | ⬜ | optional |
| 735 | Document the polling-vs-event-driven architecture difference (FNA polls SDL in `GetGamePadState`; CNA reads accumulated event state) and ensure SDL events are pumped every frame so state is current | ⬜ | |
| 736 | Unit tests: `GamePadButtons` (getters, `FromButtonArray`, `==`/`!=`/`Equals`/`GetHashCode`) and `GamePadDPad` (ctor, `FromButtonArray`, operators, getters) | ⬜ | |
| 737 | Unit tests: `GamePadState` — both public constructors directly, `IsButtonDown`/`IsButtonUp`, `==`/`!=`/`Equals`/`GetHashCode`/`ToString`, button-packing (trigger→`LeftTrigger`, stick→thumbstick-direction buttons) | ⬜ | |
| 738 | Unit tests: `GamePadThumbSticks` — `Circular` and `IndependentAxes` dead-zone math (only `None` is currently exercised), square/circular clamp, `ExcludeCircularDeadZone`, operators | ⬜ | |
| 739 | Unit tests: `GamePadTriggers` — dead-zone ctor, `==` epsilon behavior (`WithinEpsilon`), `GetHashCode` | ⬜ | |
| 740 | Unit tests: `GamePad` + `GamePadCapabilities` — `GetCapabilities`, `SetVibration`, EXT methods, `ExcludeAxisDeadZone` | ⬜ | |

---

## Phase I4 — Mouse behavior and MouseCursor

> `MouseState` is faithful and populated from SDL3. The gaps are dead behavior paths in `Mouse`
> (no cursor warp, dead relative mode, `ClickedEXT` never fires) and a non-compliant MonoGame-derived
> `MouseCursor` (no FNA counterpart). FNA reference: `Mouse.cs`, `MouseState.cs` (no `MouseCursor.cs`).

| # | Task | Status | Notes |
|---|------|--------|-------|
| 745 | Wire `Mouse::SetPosition` to actually warp the OS cursor (`SDL_WarpMouseInWindow`) with the FNA relative-mode early-return guard and window/back-buffer coordinate scaling (`Mouse.cs:107–116`); stop relying solely on `InputManager` state mutation that the next motion event overwrites | ⬜ | |
| 746 | Implement `Mouse::IsRelativeMouseModeEXT` as a real get/set property backed by `SDL_SetWindowRelativeMouseMode`/`SDL_GetWindowRelativeMouseMode`; feed relative deltas into `GetMouseState`. Currently a dead `bool` | ⬜ | |
| 747 | Either implement FNA-style back-buffer scaling in `Mouse::GetState` using the `INTERNAL_BackBufferWidth/Height/WindowWidth/Height/MouseWheel` fields, or **remove** those dead fields and document the existing `TransformWindowToLogical` deviation (`SdlInputBridge.cpp:226`) | ⬜ | |
| 748 | Wire `ClickedEXT`: have `SdlInputBridge` call `Mouse::INTERNAL_onClicked(button)` on mouse-button-down (currently never invoked) | ⬜ | |
| 749 | Add horizontal scroll wheel: handle `event.wheel.x` (bridge uses `wheel.y` only, `SdlInputBridge.cpp:562–565`). Only if the XNA layer exposes a horizontal-scroll path | ⬜ | optional (not in this FNA `MouseState`) |
| 750 | `MouseCursor` CHECKLIST: fix SPDX `MIT`→`MS-PL`, add `#include "CNA/CNAHelper.hpp"`, `NOXNA`-tag the class/members (entirely non-XNA inside the Xna namespace) | ⬜ | |
| 751 | `MouseCursor::FromTexture2D(Texture2D, int, int)` via `SDL_CreateColorCursor` (requires Texture2D pixel access); currently missing → custom cursors unsupported | ⬜ | |
| 752 | `MouseCursor::Dispose()` / `System::IDisposable` + `isDisposed_` guard (MonoGame `MouseCursor : IDisposable`); currently only a destructor | ⬜ | |
| 753 | Rename stock cursor `WaitCursor`→`WaitArrow` (MonoGame name); fix static-init ordering — `SDL_CreateSystemCursor` runs at static init **before** `SDL_Init` (likely null/fragile); construct lazily | ⬜ | |
| 754 | `MouseCursor` `Handle` property (or `NOXNA`-tag `GetSDLCursor`); record overall status decision (keep as documented MonoGame `NOXNA` extension or drop) in AUDIT.md | ⬜ | |
| 755 | New `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`: `MouseState` (8 getters, ctor, `==`/`!=`, `Equals` equal+unequal, `GetHashCode` consistency, `ToString` format incl. multi-button + `None`), `Mouse` (`GetState`, `SetPosition`, `INTERNAL_onClicked`→`ClickedEXT`), `MouseCursor` (stock cursors non-null) | ⬜ | auto-discovered via `tests/*.cpp` glob |

---

## Phase I5 — Keyboard fidelity and SDL key mapping

> `KeyboardState` is populated from SDL3 key events and `Keys`/`KeyState` values match FNA exactly.
> Gaps are an unordered `GetPressedKeys`, a non-FNA `GetHashCode`, a missing indexer, an incomplete
> keycode map, and a stubbed `GetKeyFromScancodeEXT`. FNA reference: `KeyboardState.cs`,
> `SDL3_FNAPlatform.cs` (keyMap 2360–2489, scanMap 2490–2618, `GetKeyFromScancode` 2768–2797).

| # | Task | Status | Notes |
|---|------|--------|-------|
| 760 | Fix `KeyboardState::GetPressedKeys()` ordering — must return keys sorted ascending by numeric value (XNA/FNA contract; FNA iterates bits 0→255). Currently `unordered_set` iteration is nondeterministic (`KeyboardState.cpp:32–35`) | ⬜ | |
| 761 | Make `KeyboardState::GetHashCode()` FNA-faithful (`keys0 ^ … ^ keys7` over a 256-bit field) or document as accepted deviation; currently `XOR(key*31)` | ⬜ | |
| 762 | Add `KeyState operator[](Keys) const` mirroring FNA's `this[Keys]` indexer (keep/alias the existing `getItem`) | ⬜ | |
| 763 | Complete the SDL keycode→`Keys` map in `try_convert_sdl_key` (`SdlInputBridge.cpp:258–369`): add `F13–F24`, `APPLICATION`/`MENU`→`Apps`, `SLEEP`, `VOLUMEUP`/`VOLUMEDOWN`, `KP_CLEAR`→`OemClear`, `KP_PERIOD`→`OemPeriod`, and AZERTY/Norwegian/BEPO locale fallbacks (FNA `keyMap`) | ⬜ | |
| 764 | Implement `Keyboard::GetKeyFromScancodeEXT` properly: port FNA `GetKeyFromScancode` (xnaMap→`SDL_GetKeyFromScancode`→keyMap). Currently identity no-op (`Keyboard.cpp:22–25`) | ⬜ | |
| 765 | Add scancode mode: port `INTERNAL_scanMap` + `FNA_KEYBOARD_USE_SCANCODES` env switch into the key-conversion path (entirely absent today) | ⬜ | |
| 766 | Replace `KeyboardState::ToString()` placeholder (`"[KeyboardState]"`) with a meaningful representation, or drop it (NOXNA extension not in FNA) | ⬜ | |
| 767 | Consider `enum class Keys : int` for explicit FNA-matching underlying type | ⬜ | cosmetic |
| 768 | Expand `KeyboardInputTests.cpp`: all three constructors, `operator[]`/`getItem`, `==`/`!=`/`Equals` (equal+unequal), `GetHashCode` consistency, `ToString`, **multi-key** `GetPressedKeys` ordering, empty `GetPressedKeys`, `GetState(PlayerIndex)`, `GetKeyFromScancodeEXT`, and `Keys`/`KeyState` value spot-checks | ⬜ | |

---

## Phase I6 — CHECKLIST / SPDX / NOXNA compliance and docs

| # | Task | Status | Notes |
|---|------|--------|-------|
| 775 | Add `// SPDX-License-Identifier: MS-PL` to the four internal backend files: `CNA/Internal/Input/{SdlInputBridge,InputManager}.{hpp,cpp}` (all currently start with `#pragma once`/`#include`) | ⬜ | |
| 776 | Sweep every `include/Microsoft/Xna/Framework/Input/**/*.hpp` for non-XNA members missing `NOXNA` (known: `GamePadCapabilities` EXT fields, `TextInputEXT` INTERNAL dispatchers, `MouseCursor`, `TouchCollection::empty()`, `TouchPanel::TouchDeviceExists`) and add the marker + `CNAHelper.hpp` include where missing | ⬜ | |
| 777 | Update `AUDIT.md` Input/Touch sections to distinguish **API surface** (complete) from **runtime behavior** (partial), and reflect the gaps closed by this plan; update `docs/xna-4-api-coverage.md` if present | ⬜ | |
| 778 | New `docs/input-backend.md`: `SdlInputBridge`/`InputManager` architecture, SDL3-event→XNA-state mapping table, event-driven-vs-FNA-polling deviation, and the per-device fidelity notes | ⬜ | |

---

## Phase I7 — Demo and integration coverage

| # | Task | Status | Notes |
|---|------|--------|-------|
| 780 | `examples/demo_input`: add a text-entry panel that subscribes to `TextInputEXT::TextInput`/`TextEditing` and toggles `StartTextInput`/`StopTextInput` — exercises the Phase I1 path end-to-end (currently text input has zero demo coverage) | ⬜ | |
| 781 | `examples/demo_input`: add vibration/rumble feedback and multi-pad rendering (`PlayerIndex::Two/Three/Four`); currently only `PlayerIndex::One` and no rumble | ⬜ | |
| 782 | Integration test: drive synthetic `SDL_EVENT_FINGER_*` through the bridge and assert a `GestureSample` (Tap, Flick) is dequeued via `TouchPanel::ReadGesture` — proves the Phase I2 wiring | ⬜ | |
| 783 | Integration/manual check: relative mouse mode + `SetPosition` cursor warp behave correctly (Phase I4) | ⬜ | |

---

## XNA 4.0 Input API coverage

| Area | API surface | Runtime behavior (now) | After this plan |
|------|-------------|------------------------|-----------------|
| Enums (Buttons, Keys, GamePadType, GestureType, …) | 100% | 100% | 100% |
| GamePad value types + dead-zone math | 100% | ~95% (EXT buttons/PacketNumber missing) | ~100% |
| GamePad EXT (LED, gyro, accelerometer) | 100% | ~0% (stubbed) | ~90% |
| Keyboard | 100% | ~80% (map gaps, ordering, scancode) | ~98% |
| Mouse | 100% | ~70% (no warp, dead relative mode/Clicked) | ~95% |
| MouseCursor (MonoGame ext) | ~70% | ~60% (system cursors only) | ~90% |
| Touch `GetState` | 100% | ~80% (via fallback) | ~95% |
| Touch gestures (Tap…Pinch) | 100% | **~0% (pipeline dead)** | ~90% |
| TextInputEXT / IME | 100% | **~0% (fully unwired)** | ~90% |
| Unit-test coverage | — | ~20% | ~85% |
| **Overall realistic input coverage** | **~100% surface** | **~55% behavior** | **~93% behavior** |

---

## Known limitations / deferred

| Item | Reason |
|------|--------|
| `MouseCursor` | No FNA counterpart (MonoGame-derived); status decision pending (Task 754) |
| Gamepad sensors/touchpad event stream | Capabilities advertise gyro/accel/touchpad; only on-demand reads planned (Tasks 726–727), not an event stream |
| `FNA_GAMEPAD_NUM_GAMEPADS` env override | Backend hardcodes 4 slots; low priority (Task 734) |
| Horizontal scroll wheel | This FNA `MouseState` has no horizontal-scroll member; gated on XNA-layer support (Task 749) |

---

## Reference

- FNA input source: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/`
- FNA platform input: `/rv/data/library/github.com/FNA-XNA/FNA/src/FNAPlatform/SDL3_FNAPlatform.cs`
- Per-file porting requirements: `CHECKLIST.md`
- Internal backend bridge: `src/CNA/Internal/Input/{SdlInputBridge,InputManager,GestureDetector}.cpp`
- Demo: `examples/demo_input/`
