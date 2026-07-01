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
| Touch | ✅ complete | ✅ fingers → `GetState` + gesture pipeline (`INTERNAL_onTouchEvent`/`Update`/`TouchDeviceExists`) live | ✅ `SetFinger` prev-state fixed; `DisplayWidth/Height` set from backbuffer; `GetState()`'s `InputManager` fallback documented as an intentional deviation from FNA's poll model | ✅ 41 tests | **(Phase I2 complete)** |
| TextInput | ✅ surface | ✅ SDL3 wired (Start/Stop/SetRect/active) + TEXT_INPUT/EDITING dispatch + control-char synthesis | ✅ events raised; Ctrl+V suppress | ✅ 9 tests | **(Phase I1 complete)** |
| MouseCursor | ⚠️ MonoGame ext | ✅ system cursors only | ⚠️ no `FromTexture2D`/`Dispose`; SPDX/NOXNA wrong | ❌ none |

---

## Phase I1 — TextInputEXT: SDL3 backend wiring

> **Root cause:** every `TextInputEXT` method is a no-op stub; `SdlInputBridge` ignores
> `SDL_EVENT_TEXT_INPUT` / `SDL_EVENT_TEXT_EDITING`. Text entry and IME are entirely
> non-functional. FNA reference: `TextInputEXT.cs`, `SDL3_FNAPlatform.cs:903–953, 1162–1204`.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 700 | Wire `StartTextInput()`/`StopTextInput()` to `SDL_StartTextInput(window)` / `SDL_StopTextInput(window)` using `WindowHandle` (cast `uintptr_t`→`SDL_Window*`). Replace empty bodies at `TextInputEXT.cpp:25–31` | ✅ | `ToSdlWindow()` helper casts `WindowHandle`; null-guarded (handle unset until Task 703). TU compiles clean. End-to-end needs 703 (populate handle) + 704 (event). |
| 701 | Wire `SetInputRectangle(Rectangle)` to `SDL_SetTextInputArea`. Replace empty body `TextInputEXT.cpp:33–35` | ✅ | Builds `SDL_Rect` from `rectangle.X/Y/Width/Height`, cursor offset 0 (matches FNA `SetTextInputRectangle`). Null-guarded. |
| 702 | Wire `IsTextInputActive()`→`SDL_TextInputActive`, `IsScreenKeyboardShown(IntPtr)`→`SDL_ScreenKeyboardShown`. Replace `return false` stubs `TextInputEXT.cpp:10–23` | ✅ | Both query SDL3 via `WindowHandle`; null-guarded → `false` until handle set (Task 703). |
| 703 | Populate the window handle at window creation and clear it at destruction (mirror FNA `SDL3_FNAPlatform.cs:463–465`); currently never assigned. Use `TextInputEXT::setWindowHandleProperty(handle)` (property as of Task 707) | ✅ | `GraphicsDevice.cpp`: set after `SDL_CreateWindow` (1119); clear in `destroyNativeResources()` if it points at this window. Mouse/TouchPanel handles still unpublished (separate follow-up). |
| 704 | Handle `SDL_EVENT_TEXT_INPUT` in `SdlInputBridge::ProcessEvent`: UTF-8 decode `event.text.text` → per-char `TextInputEXT::INTERNAL_OnTextInput` (FNA `1162–1184`) | ✅ | Forwards each UTF-8 byte of `event.text.text` (CNA's `char`-based callback → byte-oriented UTF-8; FNA decodes to UTF-16). `textInputSuppress` gating deferred to Task 706. Bridge-level test (`SdlInputBridgeTextInputTest`) drives synthetic events. |
| 705 | Handle `SDL_EVENT_TEXT_EDITING`: decode → `INTERNAL_OnTextEditing(text, start, length)`, including the empty/null composition path (FNA `1186–1204`) | ✅ | Passes UTF-8 `event.edit.text` + `start`/`length`; empty/null composition → empty string with 0/0 (FNA's `null` maps to empty `std::string&`). |
| 706 | Control-character synthesis on `KEY_DOWN` (incl. repeat): emit `TextInput` for Home/End/Back/Tab/Enter/Delete and Ctrl+V, with a `textInputSuppress` flag to avoid double paste. Port `TextInputBindings`/`TextInputCharacters` (FNA `FNAPlatform.cs:261–280`, `SDL3_FNAPlatform.cs:903–953`). Note: bridge currently drops key repeats entirely (`SdlInputBridge.cpp:570–573`) | ✅ | `kTextInputCharacters[7]` + `text_input_binding_index` helpers; KEY block no longer drops repeats (state set only on first press, text re-emitted on repeat); `g_textInputSuppress` gates the TEXT_INPUT case for Ctrl+V. Verified by `SdlInputBridgeTextInputTest` (8 cases: control chars, repeat re-emit, Ctrl+V paste + suppress lifecycle, plain-V passthrough, editing). |
| 707 | TextInputEXT CHECKLIST fixes: add `#include "CNA/CNAHelper.hpp"`; `NOXNA`-tag `INTERNAL_OnTextInput`/`INTERNAL_OnTextEditing` (FNA-internal, non-XNA); convert public field `WindowHandle` → `getWindowHandleProperty()`/`setWindowHandleProperty()` | ✅ | Whole class is non-XNA → tagged `NOXNA class` + every public member `NOXNA` + `@note NOXNA` (per `ShaderEffect` precedent). `WindowHandle` → property w/ private `windowHandle_`. 1757/1757 tests pass. |
| 708 | New `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`: event dispatch via `INTERNAL_OnTextInput`/`OnTextEditing`, subscriber lambda fires, `IsTextInputActive` default, `WindowHandle` round-trip | ✅ | 9 tests (`TextInputEXTTest.*`): char/editing dispatch, empty composition, no-subscriber safety, handle round-trip, `Is*` false without window, Start/Stop/SetRect no-op guards. Suite 1757→1766. |

---

## Phase I2 — Touch and gesture pipeline wiring ✅ COMPLETE (tasks 710–722)

> **Original root cause (now fixed):** SDL finger events fed only `InputManager::SetTouchState`,
> so `TouchPanel::INTERNAL_onTouchEvent` / `SetFinger` / `GestureDetector::On*` had **zero callers**,
> and `TouchPanel::Update()`'s `touchDeviceExists_` gate never opened. `SdlInputBridge.cpp`'s
> `SDL_EVENT_FINGER_*` cases now also call `TouchPanel::INTERNAL_onTouchEvent` and set
> `TouchDeviceExists`; `GraphicsDevice` publishes `DisplayWidth`/`DisplayHeight` from the
> back buffer. Gestures (Tap/DoubleTap/Hold/Drag/Flick/Pinch/PinchComplete) now fire from real
> input, verified by the `GestureDetectorTest` suite (task 720).

| # | Task | Status | Notes |
|---|------|--------|-------|
| 710 | In `SdlInputBridge.cpp:632–663`, additionally call `TouchPanel::INTERNAL_onTouchEvent(touchId, state, x, y, dx, dy)` (normalized coords + deltas) alongside `InputManager::SetTouchState`, so `GestureDetector` receives input | ✅ | `SDL_EVENT_FINGER_DOWN/MOTION/UP` now also call `TouchPanel::INTERNAL_onTouchEvent` with raw normalized `event.tfinger.x/y` (+`dx/dy` on Motion, 0/0 on Down/Up), matching FNA `SDL3_FNAPlatform.cs:969–1004`. `GestureDetector::On{Pressed,Moved,Released}` now receive real input; pixel scaling is still 0 until Task 711 sets `DisplayWidth/Height`. 1774/1774 tests pass. |
| 711 | Set `TouchPanel::DisplayWidth`/`DisplayHeight` from the active window/back-buffer size (never set today; required for `INTERNAL_onTouchEvent` pixel scaling) | ✅ | `GraphicsDevice.cpp`: `setDisplayWidthProperty`/`setDisplayHeightProperty` called from `virtualWidth_`/`virtualHeight_` in the ctor and in `Reset(PresentationParameters&, GraphicsAdapter*)` (mirrors FNA `GraphicsDevice.cs:436–437,761–762`, both back-buffer-size assignment sites). 1774/1774 tests pass. |
| 712 | Fix the `Update()` gate: `FrameworkDispatcher.cpp:62` only calls `TouchPanel::Update()` when `touchDeviceExists_` is true, but that flag is set solely by the never-called `SetFinger`. Set it on the real input path so `OnUpdate()` (Hold/Flick) runs | ✅ | `SdlInputBridge.cpp` `SDL_EVENT_FINGER_DOWN` now calls `TouchPanel::setTouchDeviceExistsProperty(true)` (mirrors FNA `SDL3_FNAPlatform.cs:972`, "Windows only notices a touch screen once it's touched"). `FrameworkDispatcher::Update()`'s gate at `.cpp:62` now opens from real finger input, so `TouchPanel::Update()` → `GestureDetector::OnUpdate()` runs. 1774/1774 tests pass. |
| 713 | Fix `TouchPanel::SetFinger` (Moved/Released branches, `.cpp:191,227–231`) to use the 5-arg `TouchLocation` ctor carrying `prevState`/`prevPosition` (match FNA `TouchPanel.cs:209–215`), so `TryGetPreviousLocation` can succeed | ✅ | Released branch: `TouchLocation(prevId, Released, prevPos, previous.State, previous.Position)`. Moved branch (finger already down): `TouchLocation(fingerId, Moved, fingerPos, previous.State, previous.Position)`. Matches FNA `TouchPanel.cs:170–177,209–215`; the newly-pressed branch stays 3-arg (FNA has no prevState there either). 1774/1774 tests pass. |
| 714 | Reconcile `TouchPanel::GetState()` (`.cpp:104–122`) with FNA: either populate `touches_` via the platform path (FNA iterates `touches[]` from `SetFinger`) or explicitly document the `InputManager` fallback deviation | ✅ | Documented the deviation in-source rather than implementing FNA's per-frame poll model (`FNAPlatform.UpdateTouchPanelState()` → `SDL_GetTouchFingers()`): CNA's `SdlInputBridge` is event-driven, not poll-driven, so `SetFinger`/`touches_` aren't fed by the real input path; `GetState()`'s `InputManager` fallback keeps real touches visible. Full poll-based parity is future work, not required for gesture wiring. 1774/1774 tests pass. |
| 715 | `TouchLocation::ToString()` → emit `"{Position:" + Position + "}"` (FNA format, currently `"[TouchLocation Id=N]"`); reconcile `GetHashCode()` to FNA's `Id + Position` formula (currently `id ^ state*31`) | ✅ | `ToString()` → `"{Position:" + position_.ToString() + "}"` (matches FNA `TouchLocation.cs:99–102`, using `Vector2::ToString()`'s existing `"{X:.. Y:..}"` format). `GetHashCode()` → `id_ + position_.GetHashCode()` (matches FNA `Id.GetHashCode() + Position.GetHashCode()`; C# `int.GetHashCode()` is the int itself). No existing tests referenced the old format. 1774/1774 tests pass. |
| 716 | `TouchPanel::ReadGesture()` → throw `System::InvalidOperationException` (already in sharp-runtime) instead of `std::logic_error` (`.cpp:128`) | ✅ | Matches FNA `TouchPanel.cs` (`throw new InvalidOperationException()`, no message). 1774/1774 tests pass. |
| 717 | `CNA::Internal::Input::GestureDetector` hygiene: change SPDX `MIT`→`MS-PL`; remove forbidden "Ported from FNA / taken from MonoGame" comments per CLAUDE.md | ✅ | `.hpp`/`.cpp` SPDX → `MS-PL` (dropped the extra `Copyright (c)` line to match sibling `TouchPanel`/`TouchLocation` header style); removed "Ported from FNA GestureDetector.cs." from the class doc comment. 1774/1774 tests pass. |
| 718 | `TouchCollection`: add XNA `GetEnumerator()` + nested `Enumerator` struct (or document the `begin/end` deviation); `NOXNA`-tag `empty()`; add settable indexer to mirror FNA `this[int] set` | ✅ | Chose the `begin/end` deviation path (matches the established codebase-wide precedent — `GameComponentCollection`, `EffectAnnotationCollection`, etc. all replace `IEnumerable<T>::GetEnumerator()` with `NOXNA`-tagged `begin()`/`end()` rather than a bespoke `Enumerator` struct, since no such pattern exists anywhere else in CNA). `empty()` → `NOXNA`-tagged. Added non-const `operator[](std::size_t)` returning `TouchLocation&` alongside the existing const overload, mirroring FNA's settable `this[int]`. 1774/1774 tests pass. |
| 719 | `TouchPanel::TouchDeviceExists` accessors are FNA-internal but public without `NOXNA` — tag `NOXNA` / reduce visibility | ✅ | Tagged `NOXNA` (kept public — genuine C++ visibility reduction would need `friend` grants for `SdlInputBridge`, `FrameworkDispatcher`, and `TouchCollection`, three unrelated call sites across the internal bridge; `NOXNA` is simpler and matches how other FNA-internal accessors, e.g. `INTERNAL_onTouchEvent`, are already marked). Doc comment notes FNA declares it `internal`. Added `#include "CNA/CNAHelper.hpp"` per CLAUDE.md. 1774/1774 tests pass. |
| 720 | New `GestureDetector` test suite: Tap, DoubleTap timing, Hold ≥1s, Horizontal/Vertical/Free drag thresholds, Flick velocity (MIN_FLICK_VELOCITY=100), Pinch + PinchComplete — driving `INTERNAL_onTouchEvent` + `Update` | ✅ | New `tests/CNA/Internal/Input/GestureDetectorTests.cpp`, 12 tests, all driven through `TouchPanel::INTERNAL_onTouchEvent`/`Update` (not `GestureDetector` directly). Since the detector's state machine lives in file-static variables with no reset hook, each test uses a fixture that drains the gesture queue and drives a neutral press/release cycle in `SetUp`/`TearDown`. Timing gestures (Hold, DoubleTap window) use real `sleep_for` with margin, since the production code has no injectable clock. 1786/1786 tests pass. |
| 721 | TouchPanel tests: `ReadGesture` (+ empty-throw), `IsGestureAvailable`, `EnqueueGesture`, `GetCapabilities`, `EnabledGestures` get/set, `DisplayWidth/Height/Orientation` | ✅ | Extended `TouchInputTests.cpp` with 6 tests: FIFO enqueue/read, empty-queue throw (`InvalidOperationException`), `GetCapabilities` both branches (`touchDeviceExists_` true, and the `InputManager` fallback), `EnabledGestures`/`DisplayWidth`/`DisplayHeight`/`DisplayOrientation` get/set round-trips. Noted (not fixed, out of scope): `GetCapabilities()` passes `MAX_TOUCHES` unconditionally in both branches, unlike FNA which zeroes `MaximumTouchCount` when disconnected — flagged for a follow-up, not part of tasks 710–722. 1792/1792 tests pass. |
| 722 | Direct tests for `TouchCollection` (Contains/FindById out-ref/CopyTo/IndexOf/Add/Clear/Remove/RemoveAt/Insert/IsConnected/operator[]), `TouchLocation` (`==`/`!=`/`Equals` equal+unequal, `GetHashCode` consistency, `ToString`, out-ref `TryGetPreviousLocation`), `GestureSample`, `TouchPanelCapabilities` | ✅ | Extended `TouchInputTests.cpp` with 20 tests across 4 suites: `TouchCollectionTest` (9), `TouchLocationTest` (6), `GestureSampleTest` (3), `TouchPanelCapabilitiesTest` (2). Covers both `operator[]` overloads (task 718's new mutable indexer), all mutating methods, both `TouchLocation` constructors + `TryGetPreviousLocation` true/false paths, and both `GestureSample` constructors (public 6-arg defaulting finger ids to `NO_FINGER`, internal 8-arg with explicit ids). 1812/1812 tests pass. |

---

## Phase I3 — GamePad behavior and FNA fidelity

> CNA drives real SDL3 gamepads (hotplug, rumble, capability probing all genuine). Gaps are
> unwired EXT features and a few fidelity deviations. FNA reference: `GamePad.cs`,
> `SDL3_FNAPlatform.cs` (GetGamePadState / PacketNumber / sensors).

| # | Task | Status | Notes |
|---|------|--------|-------|
| 725 | Implement `GamePad::SetLightBarEXT`: add `SdlInputBridge::SetLightBar` → `SDL_SetGamepadLED(device, R, G, B)`; wire from the no-op at `GamePad.cpp:59–62` | ✅ | `SdlInputBridge::SetLightBar(PlayerIndex, Color)` added (mirrors `SetVibration`'s null-guarded `get_sdl_gamepad_for_player` pattern); calls `SDL_SetGamepadLED(gamepad, R, G, B)`. Matches FNA `SDL3_FNAPlatform.cs:1982-1994`. No dedicated test yet — deferred to task 740 (batched GamePad EXT method tests), consistent with `SetVibration`/`GetGyroEXT`/etc. having no coverage until that task. 1812/1812 tests pass (no new tests). |
| 726 | Implement `GamePad::GetGyroEXT` via `SDL_GetGamepadSensorData(SDL_SENSOR_GYRO)`; replace `return false` stub `GamePad.cpp:69–74` (caps already report `HasGyroEXT`) | ✅ | Added `SdlInputBridge::GetGyro`/`GetAccelerometer`, sharing a private `read_gamepad_sensor(gamepad, type, out)` helper: enables the sensor on first use (`SDL_GamepadSensorEnabled`/`SDL_SetGamepadSensorEnabled`) then reads via `SDL_GetGamepadSensorData(gamepad, type, data, 3)`, zeroing + returning false on no-gamepad or read failure. Matches FNA `SDL3_FNAPlatform.cs:1998–2035` (`GetGamePadGyro`). No dedicated test yet — deferred to task 740, same as Task 725. 1812/1812 tests pass. |
| 727 | Implement `GamePad::GetAccelerometerEXT` via `SDL_GetGamepadSensorData(SDL_SENSOR_ACCEL)`; replace stub `GamePad.cpp:76–81` | ✅ | Same `read_gamepad_sensor` helper (task 726) with `SDL_SENSOR_ACCEL`; matches FNA `SDL3_FNAPlatform.cs:2037–2071` (`GetGamePadAccelerometer`). Implemented together with 726 since both are the same read pattern differing only by sensor type. |
| 728 | Map EXT buttons: extend `InputManager::GamePadButton` enum + `try_convert_sdl_gamepad_button` (`SdlInputBridge.cpp:85–122`) for `MISC1`→`Misc1EXT`, `RIGHT_PADDLE1`→`Paddle1EXT`, `LEFT_PADDLE1`→`Paddle2EXT`, `RIGHT_PADDLE2`→`Paddle3EXT`, `LEFT_PADDLE2`→`Paddle4EXT`, `TOUCHPAD`→`TouchPadEXT`, so they reach `GamePadState.Buttons` | ✅ | Added 6 entries to `InputManager::GamePadButton`, matching `case` branches in `SetGamePadButtonState` (sets/clears the corresponding `Buttons` flag), and matching `case` branches in `try_convert_sdl_gamepad_button`. No `GamePadButtons.hpp` change needed — FNA has no named EXT getters either; EXT buttons are read via the raw `buttons_`/`buttons` flags field in both FNA and CNA. 1812/1812 tests pass (no new tests; button-mapping coverage lands with task 737/740). |
| 729 | Implement `PacketNumber` increment-on-change: track previous per-`PlayerIndex` state, bump when `GamePadState != previous` (FNA semantics); currently hardcoded `0` | ✅ | Intentional deviation from FNA's poll-and-compare algorithm (`SDL3_FNAPlatform.cs:1805-1943`): CNA tracks the counter at the raw `InputManager` layer (bumped inside `SetGamePadConnection`/`SetGamePadButtonState`/`SetGamePadAxisValue` when a value actually changes) rather than comparing freshly-built `GamePadState`s, since the built state's button flags vary by `GamePadDeadZone` mode — comparing built states would falsely bump the counter depending on which dead-zone mode a caller uses. `GamePadState` gained `NOXNA void setPacketNumberProperty(int)` (FNA's `PacketNumber` setter is `internal`); public constructors unchanged (FNA's ctors take no `packetNumber` param either). New test `PacketNumberBumpsOnConnectButtonAndAxisChangesOnly`. 1813/1813 tests pass. |
| 730 | Rework `GamePadCapabilities`: replace 34 raw public mutable `bool` fields with `getXProperty()` getters + internal (private) setters; `NOXNA`-tag the 10 EXT capability properties; add `#include "CNA/CNAHelper.hpp"` | ✅ | All 36 members (25 core bools + `GamePadType` + 10 EXT bools) are now private fields with `getXProperty()` getters and `NOXNA`-tagged `setXProperty()` setters (FNA declares every one `{ get; internal set; }`), matching the existing `setWindowHandleProperty`/`setTouchDeviceExistsProperty`/`setPacketNumberProperty` NOXNA-public-setter precedent from tasks 715–729 rather than introducing a new friend-based pattern. The 10 EXT properties are `NOXNA` on **both** getter and setter (FNA extensions, not XNA 4.0 API). Moved implementations out of the header into a new `GamePadCapabilities.cpp` (previously all-inline). Sole producer `SdlInputBridge::GetCapabilities()` updated to the new setters. 1813/1813 tests pass (no new tests; dedicated coverage lands with task 740). |
| 731 | Rename `GamePadButtons::FromButtons`→`FromButtonArray` and `GamePadDPad::FromButtons`→`FromButtonArray` to match FNA internal names; reconcile `GamePadDPad`'s single-`Buttons` signature with FNA's `params Buttons[]` | ✅ | Both renamed to `FromButtonArray`. `GamePadDPad::FromButtonArray` now takes `std::initializer_list<Buttons>` (was a single combined `Buttons` value) — OR-combines the list, then checks the 4 DPad bits, matching FNA's per-element loop (equivalent since OR is associative). `GamePadState`'s 5-arg public constructor now passes the same `buttons` initializer_list directly to both `GamePadButtons::FromButtonArray` and `GamePadDPad::FromButtonArray`, exactly matching FNA's ctor (`GamePadState.cs:146-158`) instead of CNA's previous workaround of pre-combining through a `GamePadButtons` round-trip. `GamePad.cpp`'s single-`Buttons` call site updated to `FromButtonArray({raw.buttons})`. 1813/1813 tests pass (no regressions; dedicated tests for these methods land with task 736). |
| 732 | Make `GamePadThumbSticks::GetHashCode` (`Left + 37*Right`) and `GamePadTriggers::GetHashCode` (`Left + Right`) FNA-faithful — requires `Vector2::GetHashCode` / float-hash helper (add to sharp-runtime / Vector2 if missing). Currently truncated `*1000` formulas | ✅ | `GamePadThumbSticks::GetHashCode()` → `left_.GetHashCode() + 37 * right_.GetHashCode()` (already-existing `Vector2::GetHashCode()`, bit-reinterpretation hash). `GamePadTriggers::GetHashCode()` → `System::Single::GetHashCode(left_) + System::Single::GetHashCode(right_)` — found the needed float-hash helper already present in sharp-runtime (`System::Single::GetHashCode(float)`, same bit-reinterpretation approach), so no sharp-runtime addition was needed. Both now match FNA's formulas exactly (`GamePadThumbSticks.cs:186`, `GamePadTriggers.cs:137`). 1813/1813 tests pass (no existing tests referenced the old truncated formula; dedicated tests land with 738/739). |
| 733 | Decide `GamePadState::ToString` fidelity: FNA returns the fully-qualified type name; CNA returns `"[GamePadState IsConnected=…]"`. Match FNA or document the intentional deviation | ✅ | Chose to match FNA exactly: FNA's `GamePadState.ToString()` is `return base.ToString();` — it never overrides `ToString`, so `ValueType`'s default (fully-qualified type name, ignoring field values) applies. `ToString()` now unconditionally returns `"Microsoft.Xna.Framework.Input.GamePadState"`, documented in-source. 1813/1813 tests pass (no existing test referenced the old format; dedicated coverage lands with task 737). |
| 734 | Port `GAMEPAD_COUNT` / `FNA_GAMEPAD_NUM_GAMEPADS` env override (currently hardcoded to 4 slots in bridge/InputManager) | ✅ | Added `effective_gamepad_count()` in `SdlInputBridge.cpp` (lazily-computed, matches FNA's `DetermineNumGamepads()` — parses `FNA_GAMEPAD_NUM_GAMEPADS`, requires `>= 0`, falls back to the default on unset/invalid). Unlike FNA, the result is clamped to `MaxSupportedGamePads` (4): CNA's `PlayerIndex` is frozen XNA API with exactly 4 values (One–Four), so an override *above* 4 has no addressable slot to use (FNA's own comment says doing so requires also adding `PlayerIndex` names, which CLAUDE.md forbids). `try_find_free_gamepad_slot()` now only searches slots below the effective count, so setting the env var to a smaller number (or 0) disables/limits gamepad hotplug — the practically useful direction. 1813/1813 tests pass (no new tests; this is an env-var-gated path, not easily unit-testable without process-level env control). |
| 735 | Document the polling-vs-event-driven architecture difference (FNA polls SDL in `GetGamePadState`; CNA reads accumulated event state) and ensure SDL events are pumped every frame so state is current | ✅ | Verified first (no code change needed): `Game::Tick()` (`Game.cpp:378`) unconditionally calls `PollEvents()` → `SDL_PollEvent`/`SdlInputBridge::ProcessEvent` once per frame before `Update()`/`Draw()`, on every code path that reaches a frame (`Run()`→`RunLoop()`→`Tick()`, `RunOneFrame()`→`Tick()`, and the `__EMSCRIPTEN__` main-loop callback all funnel through this). Documented the architecture difference in two places: a comment on `GamePad::GetState()` (`GamePad.cpp`) explaining FNA's fresh-poll-per-call vs. CNA's accumulate-from-events model and pointing at `Game::Tick()` as what keeps it current, and an expanded class-level doc comment on `InputManager` (`InputManager.hpp`) with the same note. 1813/1813 tests pass (doc-only change, no behavior touched). |
| 736 | Unit tests: `GamePadButtons` (getters, `FromButtonArray`, `==`/`!=`/`Equals`/`GetHashCode`) and `GamePadDPad` (ctor, `FromButtonArray`, operators, getters) | ✅ | New `tests/.../Input/GamePadButtonsTests.cpp`, 14 tests (7 `GamePadButtonsTest` + 7 `GamePadDPadTest`): both ctors, all 11 named getters (released + pressed), `FromButtonArray` (multi-flag, cross-element combine, empty list), `Equals`/`==`/`!=` for equal+unequal, `GetHashCode` (exact value match against the underlying flags / FNA's bit-weighted DPad formula). 1827/1827 tests pass. |
| 737 | Unit tests: `GamePadState` — both public constructors directly, `IsButtonDown`/`IsButtonUp`, `==`/`!=`/`Equals`/`GetHashCode`/`ToString`, button-packing (trigger→`LeftTrigger`, stick→thumbstick-direction buttons) | ✅ | New `tests/.../Input/GamePadStateTests.cpp`, 10 tests: default + 4-arg + 5-arg constructors, trigger→`LeftTrigger`/`RightTrigger` packing past `GamePad::TriggerThreshold`, stick→thumbstick-direction packing past `LeftDeadZone`/`RightDeadZone`, `IsButtonDown`/`IsButtonUp` (including multi-flag "all requested bits" semantics), `Equals`/`==`/`!=` (including `PacketNumber` affecting equality), exact-formula `GetHashCode`, and `ToString`'s fixed fully-qualified-name return (task 733). 1837/1837 tests pass. |
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
| 780 | `examples/demo_input`: add a text-entry panel that subscribes to `TextInputEXT::TextInput`/`TextEditing` and toggles `StartTextInput`/`StopTextInput` — exercises the Phase I1 path end-to-end (currently text input has zero demo coverage) | ✅ | `DrawTextPanel`: buffer cells (Backspace/Enter editing), last-byte 8 bit-LEDs, IME draft, active indicator, blinking caret; F1 toggles Start/Stop. Builds; runs crash-free 4s under native backend. Visual confirm needs a display (no Wayland screenshot tool here). |
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
