# Input Backend Architecture

Describes how CNA wires SDL3 input events to the `Microsoft::Xna::Framework::Input` (and
`Input::Touch`) public API. Reference implementation: `feature/input` branch, `plan_input.md`
(Phases I1–I6, tasks 700–777). FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.

---

## 1. Overview

Real input behavior flows through one internal bridge and one internal state accumulator:

```
SDL_PollEvent  (Game::PollEvents(), called once per frame from Game::Tick())
  → CNA::Internal::Input::SdlInputBridge::ProcessEvent(event)   // switch on event.type
      → CNA::Internal::Input::InputManager::Set*State(...)      // keyboard / mouse / gamepad / touch snapshot
      → Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_On*(...)   // text input / editing
      → Microsoft::Xna::Framework::Input::Touch::TouchPanel::INTERNAL_onTouchEvent(...)
          → CNA::Internal::Input::GestureDetector::On{Pressed,Moved,Released}(...)
  → Keyboard/Mouse/GamePad/TouchPanel::GetState()   // read by game code each frame
  → Microsoft::Xna::Framework::FrameworkDispatcher::Update()
      → TouchPanel::Update() → GestureDetector::OnUpdate()   // Hold/Flick timing gestures
```

Two classes do the work:

- **`CNA::Internal::Input::SdlInputBridge`** (`include/CNA/Internal/Input/SdlInputBridge.hpp`,
  `src/CNA/Internal/Input/SdlInputBridge.cpp`) — the single SDL event funnel. `ProcessEvent`
  is the *only* place that reads an `SDL_Event`; do not add a second event path. It also
  exposes public static query methods that XNA-layer classes call into directly (not through
  the event stream): `SetVibration`, `SetTriggerVibration`, `SetLightBar`, `GetGUID`, `GetGyro`,
  `GetAccelerometer`, `GetCapabilities`, `GetKeyFromScancode`. This dual role — event funnel
  plus query surface — is intentional; adding a query method here is fine, adding a second
  event-processing entry point is not.
- **`CNA::Internal::Input::InputManager`** (`include/CNA/Internal/Input/InputManager.hpp`,
  `src/CNA/Internal/Input/InputManager.cpp`) — accumulates per-device state pushed by
  `SdlInputBridge` (`Set*State`/`Add*Delta` methods) and hands back immutable XNA state-object
  snapshots (`Get*State` methods: `GetMouseState`, `GetKeyboardState`, `GetTouchState`,
  `GetGamePadState`, `GetRawGamePadState`). `GamePad::GetState(playerIndex, deadZoneMode)` calls
  `GetRawGamePadState` and applies dead-zone processing itself at the XNA layer, since the same
  raw state can be read back through three different `GamePadDeadZone` modes.

Touch additionally has a third internal class:

- **`CNA::Internal::Input::GestureDetector`** (`include/CNA/Internal/Input/GestureDetector.hpp`,
  `src/CNA/Internal/Input/GestureDetector.cpp`) — a ported gesture-recognition state machine
  (Tap, DoubleTap, Hold, HorizontalDrag, VerticalDrag, FreeDrag, Flick, DragComplete, Pinch,
  PinchComplete). Driven by `TouchPanel::INTERNAL_onTouchEvent` (per-event) and
  `TouchPanel::Update()` (per-frame, for timing-based gestures like Hold and the DoubleTap
  window). Recognized gestures are enqueued via `TouchPanel::EnqueueGesture` and drained by
  game code through `TouchPanel::ReadGesture()`.

---

## 2. SDL3 event → XNA state mapping

Every case in `SdlInputBridge::ProcessEvent`'s `switch (event.type)`. This is the complete list;
if an `SDL_Event` type isn't in this table, CNA does not currently handle it (falls through to
the `default: break;` case).

| SDL event | InputManager / TouchPanel call | XNA-visible effect |
|---|---|---|
| `SDL_EVENT_MOUSE_MOTION` | `SetMousePosition` (window→logical coords via `to_logical_position`), `AddMouseRelativeDelta(xrel, yrel)` | `Mouse::GetState().X/Y`; relative-mode delta accumulator (drained on next `GetState()` read while `IsRelativeMouseModeEXT` is on) |
| `SDL_EVENT_MOUSE_BUTTON_DOWN` / `_UP` | `SetMouseButtonState` (Left/Right/Middle/XButton1/XButton2), `SetMousePosition`; on DOWN also `Mouse::INTERNAL_onClicked(button-1)` | `Mouse::GetState().LeftButton` etc.; `Mouse::ClickedEXT` fires |
| `SDL_EVENT_MOUSE_WHEEL` | `AddScrollWheelDelta(wheel.y * 120)` | `Mouse::GetState().ScrollWheelValue` (cumulative, XNA units of 120 per notch) |
| `SDL_EVENT_KEY_DOWN` / `_UP` | `SetKeyState(key, pressed)` (skipped on key-repeat — state is already set); also drives control-character `TextInput` synthesis (`handle_text_input_key_down/up`) | `Keyboard::GetState().IsKeyDown/Up`; `Keys` resolved via `try_convert_sdl_scancode` (scancode mode) or `try_convert_sdl_key` (default mode) — see §4 |
| `SDL_EVENT_TEXT_INPUT` | none (bypasses `InputManager`) | `TextInputEXT::INTERNAL_OnTextInput(char)` once per UTF-8 byte of `event.text.text`; suppressed while a synthesized Ctrl+V paste is in flight |
| `SDL_EVENT_TEXT_EDITING` | none | `TextInputEXT::INTERNAL_OnTextEditing(text, start, length)` — empty/null composition maps to `("", 0, 0)` |
| `SDL_EVENT_FINGER_DOWN` | `SetTouchState(id, Pressed, pixelPos)`; also `TouchPanel::setTouchDeviceExistsProperty(true)` and `TouchPanel::INTERNAL_onTouchEvent(id, Pressed, x, y, 0, 0)` | `TouchPanel::GetState()`; `GestureDetector::OnPressed` |
| `SDL_EVENT_FINGER_MOTION` | `SetTouchState(id, Moved, pixelPos)`; `TouchPanel::INTERNAL_onTouchEvent(id, Moved, x, y, dx, dy)` | `TouchPanel::GetState()`; `GestureDetector::OnMoved` |
| `SDL_EVENT_FINGER_UP` | `SetTouchState(id, Released, pixelPos)`; `TouchPanel::INTERNAL_onTouchEvent(id, Released, x, y, 0, 0)` | `TouchPanel::GetState()`; `GestureDetector::OnReleased` |
| `SDL_EVENT_GAMEPAD_ADDED` | `SetGamePadConnection(playerIndex, true)` (assigns the next free `PlayerIndex` slot, opens the `SDL_Gamepad`) | `GamePad::GetState(playerIndex).IsConnected` |
| `SDL_EVENT_GAMEPAD_REMOVED` | `SetGamePadConnection(playerIndex, false)` (closes the `SDL_Gamepad`, frees the slot) | `GamePad::GetState(playerIndex).IsConnected` becomes false |
| `SDL_EVENT_GAMEPAD_BUTTON_DOWN` / `_UP` | `SetGamePadButtonState(playerIndex, button, state)` | `GamePadState.Buttons`/`.DPad` |
| `SDL_EVENT_GAMEPAD_AXIS_MOTION` | `SetGamePadAxisValue(playerIndex, axis, normalizedValue)` (stick Y axes negated to match XNA's up-positive convention) | `GamePadState.ThumbSticks`/`.Triggers` (raw; dead zone applied by `GamePad::GetState`) |

Touch coordinates: SDL delivers finger position/delta normalized to `[0, 1]` relative to the
window. There are two independent scaling paths from that same normalized event, feeding two
different consumers: `to_touch_pixel_position` (file-local to `SdlInputBridge.cpp`) scales by
the SDL window's point size (`SDL_GetWindowSize`) then maps through the graphics backend's
logical-coordinate transform, feeding `InputManager::SetTouchState` (and so
`TouchPanel::GetState()`'s fallback snapshot, §3); `TouchPanel::INTERNAL_onTouchEvent` instead
scales by `TouchPanel::DisplayWidth`/`DisplayHeight` (set from the real back-buffer size by
`GraphicsDevice`, task 711), feeding `GestureDetector`. Both match FNA's own
normalized→pixel intent, just via two different size sources for two different consumers.

Gamepad rumble, light bar, trigger vibration, gyroscope, and accelerometer are **not** part of
the event stream above — they're on-demand queries/commands issued by `GamePad`'s EXT methods
directly through `SdlInputBridge`'s public static methods (`SetVibration`, `SetLightBar`,
`SetTriggerVibration`, `GetGyro`, `GetAccelerometer`), not accumulated state.

---

## 3. Event-driven vs. FNA's poll-driven model

This is the single biggest architectural deviation from FNA, and it applies uniformly across
every input device:

- **FNA** re-queries the platform layer fresh on every `Get*State()` call. `Keyboard.GetState()`,
  `Mouse.GetState()`, `GamePad.GetState()`, and `TouchPanel.GetState()` all call into
  `SDL3_FNAPlatform` methods that ask SDL for current state *at call time*.
- **CNA** is event-driven. `SdlInputBridge::ProcessEvent` accumulates whatever SDL delivers into
  `InputManager`'s (or `TouchPanel`'s) internal fields as events arrive; `Get*State()` methods
  just read back that accumulated state — they never touch SDL themselves.

What keeps CNA's accumulated state current: `Game::Tick()` (`Game.cpp`) unconditionally calls
`PollEvents()` → `SDL_PollEvent` → `SdlInputBridge::ProcessEvent` exactly once per frame, before
`Update()`/`Draw()` run, on every code path that reaches a frame (`Run()`→`RunLoop()`→`Tick()`,
`RunOneFrame()`→`Tick()`, and the Emscripten main-loop callback all funnel through this). As long
as that per-frame pump keeps happening, the practical behavior is indistinguishable from FNA's
poll-per-call model for game code that reads input once per `Update()`.

Where the difference is actually visible:

- Calling `Get*State()` **more than once per frame** returns the *same* snapshot both times in
  CNA (nothing changed since the last `Tick()`), whereas FNA could theoretically observe SDL
  state changing mid-frame between two calls (in practice this rarely matters, since SDL itself
  only updates on `SDL_PollEvent`).
- `TouchPanel::GetState()` specifically has its own extra wrinkle: FNA populates its `touches_`
  array from a per-frame poll (`SDL_GetTouchFingers`) via `SetFinger`, but CNA's `SetFinger` has
  no real caller — `GetState()` instead falls back to `InputManager::GetTouchState()`'s
  event-accumulated snapshot. Documented in-source in `TouchPanel.cpp` (task 714).
- `GamePadState.PacketNumber` can't be "bump on state comparison" like FNA's poll loop, since
  CNA never builds two consecutive `GamePadState`s to diff — instead it's bumped at the raw
  `InputManager` layer whenever a connection/button/axis value actually changes (task 729,
  documented in-source in `InputManager.cpp`).
- Mouse relative-mode delta (`IsRelativeMouseModeEXT`) is accumulated from
  `SDL_EVENT_MOUSE_MOTION`'s `xrel`/`yrel` and drained to zero on each `GetMouseState()` read,
  rather than FNA's `SDL_GetRelativeMouseState()` poll — the event-driven equivalent of the same
  API (documented in-source in `InputManager.cpp`).

---

## 4. Per-device fidelity notes

Full per-task detail lives in `plan_input.md` (Phases I1–I6) and `AUDIT.md`'s `Input`/
`Input::Touch` tables (`Runtime` column). Summary of what's real vs. what deviates:

### Keyboard

- `Keyboard::GetState()` is a straight read of `InputManager`'s accumulated key-state set.
- `Keyboard::GetKeyFromScancodeEXT` has two modes, matching FNA's `UseScancodes` switch: default
  mode round-trips a US-layout `Keys` value through `SDL_GetKeyFromScancode` and back to
  translate for the *current* keyboard layout; scancode mode (`FNA_KEYBOARD_USE_SCANCODES=1`,
  read once into a process-wide cached flag on first use) does a direct physical-position
  lookup via a ported `INTERNAL_scanMap` table and passes `GetKeyFromScancodeEXT`'s input straight
  through unchanged (tasks 764–765).
- `KeyboardState::GetPressedKeys()` returns keys sorted ascending by numeric `Keys` value (task
  760); `GetHashCode()` reconstructs FNA's 8×32-bit word layout and XORs them (task 761);
  `ToString()` matches FNA's `ValueType` default, `"Microsoft.Xna.Framework.Input.KeyboardState"`
  (task 766, not the placeholder it used to be).
- Storage remains an `std::unordered_set<Keys>` rather than FNA's native bitfield representation
  — an accepted internal-representation deviation; the ordering/hash *contract* matches FNA, the
  storage layout doesn't need to.

### Mouse

- `Mouse::SetPosition`, `IsRelativeMouseModeEXT`, and `ClickedEXT` are all wired to real SDL3
  calls (`SDL_WarpMouseInWindow`, `SDL_Get/SetWindowRelativeMouseMode`) rather than stubs
  (Phase I4, tasks 745–749).
- Known deviation: `SetPosition`'s warp target has no inverse logical→window coordinate
  transform, so on a letterboxed/scaled window the OS cursor lands off by the scale factor.
  Fixing it for real needs a graphics-layer (`IGraphicsBackend`) addition — out of scope for the
  input branch, documented in-source in `Mouse.cpp`.
- `MouseCursor` (custom + 11 stock system cursors) is a MonoGame-derived `NOXNA` extension — FNA
  has no `MouseCursor` type at all. Kept as a deliberate, documented status decision (task 754),
  since `Mouse::SetCursor(MouseCursor&)` depends on it and it's the standard way
  MonoGame/FNA-family games set cursors.

### GamePad

- `GetState`/`GetCapabilities`/`SetVibration` and every EXT method (light bar, trigger
  vibration, gyroscope, accelerometer reads) are wired to real `SDL_Gamepad` hardware, not
  stubs (Phase I3, tasks 725–740).
- `PacketNumber` semantics are an accepted deviation from FNA's poll-and-compare algorithm — see
  §3 above.
- `GamePadState::GetHashCode()` (`buttons_.GetHashCode() ^ (packetNumber_ * 31)`) is **not** a
  literal FNA port: FNA's own `GetHashCode()` is `return base.GetHashCode()`, .NET's
  non-deterministic reflection-based default, which has no reproducible C++ equivalent. CNA's
  formula is a reasonable accepted substitute, documented as such in `AUDIT.md`.
- `FNA_GAMEPAD_NUM_GAMEPADS` env override is ported (task 734), but clamped to 4 slots since
  `PlayerIndex` is frozen XNA API (only One–Four) — going *above* 4 has no addressable slot.

### Touch

- `SDL_EVENT_FINGER_*` feed `TouchPanel::INTERNAL_onTouchEvent`, which drives
  `GestureDetector`'s state machine directly — this is what makes Tap, DoubleTap, Hold,
  Horizontal/Vertical/Free drag, Flick, Pinch, and PinchComplete recognition work end-to-end
  (Phase I2, tasks 710–722; previously the pipeline was entirely dead — no caller ever reached
  `GestureDetector`).
- `TouchDeviceExists` only becomes true after the *first* touch event, matching FNA's own
  comment ("Windows only notices a touch screen once it's touched").
- Known deviation: `TouchPanel::GetState()` falls back to `InputManager`'s event-driven snapshot
  rather than FNA's per-frame `SetFinger` poll population of `touches_` — see §3.
- Known minor bug (not yet fixed): `TouchPanel::GetCapabilities()` passes `MAX_TOUCHES`
  unconditionally in both the connected and disconnected branches; FNA returns `0` when
  disconnected (task 721 noted it, not fixed).

### TextInputEXT

- Wired to `SDL_StartTextInput`/`StopTextInput`, `SDL_SetTextInputArea`,
  `SDL_EVENT_TEXT_INPUT`/`_EDITING` (Phase I1, tasks 700–708), including control-character
  synthesis for keys SDL doesn't deliver as `TEXT_INPUT` (Home/End/Back/Tab/Enter/Delete,
  Ctrl+V) and Ctrl+V paste-echo suppression.
- Known deviation: the `TextInput`/`INTERNAL_OnTextInput` callback is `char`-based, so UTF-8
  text is forwarded one byte at a time; FNA's C# callback is UTF-16-code-unit-based. A consumer
  appending bytes to a `std::string` reconstructs the original UTF-8 text correctly either way.
