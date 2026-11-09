# Input Backend Architecture

> **Related input docs (INP-0003):** [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual hardware checks](demo-input-checklist.md#hardware-verification-record) · [demo checklist](demo-input-checklist.md)

Describes how CNA wires SDL3 input events to the `Microsoft::Xna::Framework::Input` (and
`Input::Touch`) public API. The FNA reference can live anywhere; the report generator accepts
`--fna <checkout>` or `CNA_FNA_TREE=<checkout>`. Task numbers in the input pages (bare numbers such
as "task 734", `Phase I*`/`INPUT-*`, `P#-###`) are decoded in [task-id-index.md](task-id-index.md).

---

## 1. Overview

Real input behavior flows through one platform event batch. Event-driven devices feed the shared
accumulator; whole-device services publish one frame snapshot:

```
IPlatform::PollEvents(batch)  (Game::PollEvents(), once per frame from Game::Tick())
  → CNA::Internal::Input::PlatformInputBridge::ProcessEvent(event) // visit PlatformEvent
      → CNA::Internal::Input::InputManager::Set*State(...)      // legacy keyboard/mouse compatibility
      → Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_On*(...)   // text input / editing
      → Microsoft::Xna::Framework::Input::Touch::TouchPanel::INTERNAL_setTouchState(...)
      → Microsoft::Xna::Framework::Input::Touch::TouchPanel::INTERNAL_onTouchEvent(...)
          → CNA::Internal::Input::GestureDetector::On{Pressed,Moved,Released}(...)
  → IPlatformKeyboard/Mouse/Gamepad/Joystick::Update()
  → Keyboard/Mouse/GamePad/Joysticks/TouchPanel state // read by game code each frame
  → Microsoft::Xna::Framework::FrameworkDispatcher::Update()
      → TouchPanel::Update() → GestureDetector::OnUpdate()   // Hold/Flick timing gestures
```

The current event/state boundary is split deliberately by device shape:

- **`CNA::Internal::Input::PlatformInputBridge`**
  (`modules/input/include/CNA/Internal/Input/PlatformInputBridge.hpp`, implemented in
  `modules/input/src/Internal/SdlInputBridge.cpp`) — the single production event funnel.
  It consumes CNA's SDL-free `PlatformEvent` vocabulary; native translation is performed once by
  the selected `IPlatform` implementation before the event reaches `Game`.
- **`CNA::Internal::Input::SdlInputBridge`** retains keyboard/mouse/touch compatibility helpers. Its
  raw `ProcessEvent` overload is only an adapter for
  legacy SDL-shaped tests and delegates to the platform bridge in an SDL3 build; production
  `Game` does not call it, and it owns no mapped-gamepad or raw-joystick queries or native handles
  (PLAT-82/83).
- **`CNA::Internal::Input::InputManager`** (`modules/input/include/CNA/Internal/Input/InputManager.hpp`,
  `modules/input/src/Internal/InputManager.cpp`) — retains only the keyboard and mouse accumulators
  used by control-character/click synthesis and legacy raw-adapter tests. It holds no public-device
  state (PLAT-86): touch locations belong to `TouchPanel`; public `Keyboard` and `Mouse` read their
  platform services.
- **`Microsoft::Xna::Framework::Input::Touch::TouchPanel`** owns the event-driven touch map and
  gesture queue. `INTERNAL_setTouchState` records logical position, pressure and previous-frame
  history; `Update()` promotes held touches and retires releases exactly once per frame.
- **`CNA::Platform::IPlatformGamepad`** owns the four stable player slots, immutable frame
  snapshots, cached capabilities/identity and all mapped-controller actuators. Public
  `GamePad::GetState(playerIndex, deadZoneMode)` projects one raw platform snapshot through the
  requested dead-zone mode; `InputManager` contains no mapped-gamepad state.
- **`CNA::Platform::IPlatformJoystick`** owns the genuinely unmapped view: arbitrary axes,
  buttons, POV hats and trackballs addressed by the same `DeviceId` carried by hotplug events.
  `Game` publishes it once after each event drain; public `Joysticks` only projects that snapshot.
- **`CNA::Platform::IPlatformHaptics`** owns stable-id standalone enumeration plus simple-rumble
  initialization/play/stop. Public `Haptics::GetHapticsEXT` and the standalone `HapticDevice`
  rumble methods use it. The SDL-only internal haptic seam remains solely for the richer arbitrary
  effect model and joystick/mouse correlation; no SDL handle appears in a public header.

Gesture handling adds another internal class:

- **`CNA::Internal::Input::GestureDetector`** (`modules/input/include/CNA/Internal/Input/GestureDetector.hpp`,
  `modules/input/src/Internal/GestureDetector.cpp`) — a ported gesture-recognition state machine
  (Tap, DoubleTap, Hold, HorizontalDrag, VerticalDrag, FreeDrag, Flick, DragComplete, Pinch,
  PinchComplete). Driven by `TouchPanel::INTERNAL_onTouchEvent` (per-event) and
  `TouchPanel::Update()` (per-frame, for timing-based gestures like Hold and the DoubleTap
  window). Recognized gestures are enqueued via `TouchPanel::EnqueueGesture` and drained by
  game code through `TouchPanel::ReadGesture()`.

---

## 2. SDL3 event → PlatformEvent → XNA state mapping

`Sdl3EventMapper` first translates the native event into the contract vocabulary; then
`PlatformInputBridge` applies the mapped alternative. This table describes that end-to-end path.
An SDL event the mapper does not recognise is intentionally omitted from the platform batch.

| SDL event | State sink | XNA-visible effect |
|---|---|---|
| `SDL_EVENT_MOUSE_MOTION` | The compatibility bridge still accumulates it; after the event drain `IPlatformMouse::Update()` polls absolute position/buttons and relative displacement | `Mouse::GetState().X/Y`; relative displacement is consumed on the first read, while absolute position is a frame snapshot |
| `SDL_EVENT_MOUSE_BUTTON_DOWN` / `_UP` | On DOWN, `Mouse::INTERNAL_onClicked(button-1)` fires; the compatibility accumulator mirrors all five buttons | Public held buttons come from the platform snapshot's CNA-owned five-bit mask; `Mouse::ClickedEXT` remains event-driven |
| `SDL_EVENT_MOUSE_WHEEL` | The platform mouse observes the mapped event because wheel state is not pollable, truncates to whole notches and accumulates both axes in XNA units; the compatibility bridge mirrors it | `Mouse::GetState().ScrollWheelValue` and horizontal EXT value (cumulative, 120 units per notch) |
| `SDL_EVENT_MOUSE_ADDED` / `_REMOVED` | none (bypasses `InputManager`) | `CNA::Input::InputDevices::MouseConnectedEXT`/`MouseDisconnectedEXT.Invoke(event.mdevice.which)` — CNAEXT hotplug notification only |
| `SDL_EVENT_KEYBOARD_ADDED` / `_REMOVED` | none (bypasses `InputManager`) | `CNA::Input::InputDevices::KeyboardConnectedEXT`/`KeyboardDisconnectedEXT.Invoke(event.kdevice.which)` — CNAEXT hotplug notification only |
| `SDL_EVENT_KEY_DOWN` / `_UP` | Updates the bridge's internal control-key state and drives control-character `TextInput` synthesis (`handle_text_input_key_down/up`) | Public held-key state is published once after the event drain by `IPlatformKeyboard::Update()`; the SDL3 service converts native keycodes into `KeyCode`, while Terminal publishes its decoded `KeyCode` set |
| `SDL_EVENT_TEXT_INPUT` | none (bypasses `InputManager`) | `event.text.text` (UTF-8) is decoded to UTF-16 code units (`decode_utf8_to_utf16`) and each is dispatched via `TextInputEXT::INTERNAL_OnTextInput(charcs)` — one UTF-16 code unit per call, astral code points as surrogate pairs, matching FNA's `Encoding.UTF8.GetChars`; suppressed while a synthesized Ctrl+V paste is in flight |
| `SDL_EVENT_TEXT_EDITING` | none | `TextInputEXT::INTERNAL_OnTextEditing(text, start, length)` — empty/null composition maps to `("", 0, 0)` |
| `SDL_EVENT_TEXT_EDITING_CANDIDATES` | none | `TextInputEXT::INTERNAL_OnTextEditingCandidates(candidates, selected, horizontal)` — CNAEXT IME candidate-list extension; null candidates dispatch as an empty list |
| `SDL_EVENT_FINGER_DOWN` | `TouchPanel::INTERNAL_setTouchState(id, Pressed, pixelPos, pressure)`; also sets the sticky device flag and calls `INTERNAL_onTouchEvent(id, Pressed, x, y, 0, 0)` | `TouchPanel::GetState()`; `GestureDetector::OnPressed` |
| `SDL_EVENT_FINGER_MOTION` | `TouchPanel::INTERNAL_setTouchState(id, Moved, pixelPos, pressure)`; `INTERNAL_onTouchEvent(id, Moved, x, y, dx, dy)` | `TouchPanel::GetState()`; `GestureDetector::OnMoved` |
| `SDL_EVENT_FINGER_UP` / `SDL_EVENT_FINGER_CANCELED` | `TouchPanel::INTERNAL_setTouchState(id, Released, pixelPos, pressure)`; `INTERNAL_onTouchEvent(id, Released, x, y, 0, 0)` | `TouchPanel::GetState()`; `GestureDetector::OnReleased` — up and cancel share the same platform state transition |
| `SDL_EVENT_GAMEPAD_ADDED` / `_REMOVED` | Mapped for general device notification, but mapped-gamepad ownership is reconciled by `IPlatformGamepad::Update()` after the event drain | `Sdl3Gamepad` preserves existing slots, closes vanished handles and assigns new devices to the first free `PlayerIndex` slot |
| `SDL_EVENT_GAMEPAD_BUTTON_DOWN` / `_UP` | The event is mapped but does not mutate a second store; `Sdl3Gamepad::Update()` polls the complete held-button mask once per frame | `GamePadState.Buttons`/`.DPad` read one immutable platform snapshot |
| `SDL_EVENT_GAMEPAD_AXIS_MOTION` | The event is mapped but state is polled with the same CNA-owned axis helper; stick Y is inverted and triggers normalised at the SDL edge | `GamePadState.ThumbSticks`/`.Triggers`; the requested dead zone is applied by public `GamePad::GetState` |
| `SDL_EVENT_JOYSTICK_ADDED` / `_REMOVED` | `Sdl3Joystick` opens/closes the raw device before the platform batch reaches the input bridge; a small SDL-free announced-id set suppresses duplicate add and unknown remove notifications | `CNA::Input::Joysticks::ConnectedEXT`/`DisconnectedEXT.Invoke(deviceId)` plus one immutable raw snapshot per frame; distinct from `GamePad`'s mapped view of the same physical device |
| `SDL_EVENT_SENSOR_UPDATE` | Mapped losslessly to process-scoped `SensorEvent`; the input bridge has no accumulator for it because public `Sensors` remains an on-demand query | General platform consumers can correlate its stable device id with `IPlatformSensors::GetSensors()` and observe all six values plus the sensor sample timestamp |

**Intentionally unhandled event type (P8-012):**
- `SDL_EVENT_GAMEPAD_REMAPPED` — FNA itself has no handler for this event either (no match in
  `SDL3_FNAPlatform.cs`). XNA's `Buttons`/`GamePadState` model is read fresh on every
  frame update rather than kept in an event accumulator, so a live SDL mapping change is picked up
  by the next `IPlatformGamepad::Update()` without a special-case handler.

`CNA::Input::Sensors` and `Microsoft::Devices::Sensors` still read through their established
on-demand APIs; mapping `SensorEvent` does not create a second public accumulator. It completes the
general platform event contract for consumers that do want the native stream.

Touch coordinates: SDL delivers finger position/delta normalized to `[0, 1]` relative to the
window. There are two independent scaling paths from that same normalized event, feeding two
different consumers: `to_touch_pixel_position` (file-local to `SdlInputBridge.cpp`) scales by
the SDL window's point size (`SDL_GetWindowSize`) then maps through the graphics renderer's
logical-coordinate transform, feeding `TouchPanel::INTERNAL_setTouchState` and its event snapshot;
`TouchPanel::INTERNAL_onTouchEvent` instead
scales by `TouchPanel::DisplayWidth`/`DisplayHeight` (set from the real back-buffer size by
`GraphicsDevice`, task 711), feeding `GestureDetector`. Both match FNA's own
normalized→pixel intent, just via two different size sources for two different consumers.

Gamepad rumble, light bar, trigger vibration, gyroscope, and accelerometer are **not** part of
the event stream above — they're on-demand queries/commands issued by `GamePad`'s EXT methods
through `IPlatformGamepad` (`SetRumble`, `SetLightBar`, `SetTriggerRumble`, `TryGetSensor`), not
accumulated state.

---

## 3. Frame snapshots and event accumulators vs. FNA's per-call polling

This is the single biggest architectural deviation from FNA:

- **FNA** re-queries the platform layer fresh on every `Get*State()` call. `Keyboard.GetState()`,
  `Mouse.GetState()`, `GamePad.GetState()`, and `TouchPanel.GetState()` all call into
  `SDL3_FNAPlatform` methods that ask SDL for current state *at call time*.
- **CNA** publishes input once per frame. Keyboard, mouse, gamepad and joystick are whole-device
  platform snapshots; touch is accumulated directly by `TouchPanel` from `PlatformEvent`.
  Public `Get*State()` methods read those stored values and never poll a native API.

What keeps CNA's accumulated state current: `Game::Tick()` (`Game.cpp`) unconditionally calls
`PollEvents()` → `IPlatform::PollEvents(batch)` → `PlatformInputBridge::ProcessEvent`, followed by
`IPlatformKeyboard::Update()` and `IPlatformMouse::Update()`, exactly once per frame before
`Update()`/`Draw()` run, on every code path that reaches a frame (`Run()`→`RunLoop()`→`Tick()`,
`RunOneFrame()`→`Tick()`, and Emscripten `RunLoop()`/`RunFrameEXT()` all funnel through the shared
tick core). As long as that per-frame pump keeps happening, the practical behavior is
indistinguishable from FNA's
poll-per-call model for game code that reads input once per `Update()`.

Where the difference is actually visible:

- Calling `Get*State()` **more than once per frame** returns the *same* snapshot both times in
  CNA (nothing changed since the last `Tick()`), whereas FNA could theoretically observe SDL
  state changing mid-frame between two calls (in practice this rarely matters, since SDL itself
  only updates on `SDL_PollEvent`). Relative mouse x/y deliberately remain the exception: the
  first read consumes accumulated displacement and a second read reports zero.
- `TouchPanel::GetState()` specifically has its own extra wrinkle: FNA populates its `touches_`
  array from a per-frame poll (`SDL_GetTouchFingers`) via `SetFinger`, but CNA's `SetFinger` has
  no real caller — `GetState()` instead reads the panel's own event-accumulated map. Repeated reads
  are pure; `TouchPanel::Update()` alone advances previous locations and release retirement.
- `GamePadState.PacketNumber` is published by `IPlatformGamepad` and changes only when the raw
  platform snapshot changes, rather than being recomputed by each public getter.
- Mouse relative-mode delta (`IsRelativeMouseModeEXT`) is collected by `IPlatformMouse::Update()`
  after the native event pump and drained through `ConsumeRelativeDelta()` on each public read,
  matching FNA's `SDL_GetRelativeMouseState` behaviour. Absolute position stays in the snapshot
  and becomes visible again when relative mode is disabled.

---

## 4. Per-device fidelity notes

The per-member FNA comparison is `docs/input-fna-fidelity.md`. Summary of what's real vs. what
deviates:

### Keyboard

- `Keyboard::GetState()` converts the most recent `IPlatformKeyboard` snapshot into an XNA
  `KeyboardState`; `GetModStateEXT()` converts the modifier mask from that same snapshot. Neither
  call updates the service, so repeated reads inside one frame cannot observe different clocks.
- `Keyboard::GetKeyFromScancodeEXT` has two modes, matching FNA's `UseScancodes` switch: default
  mode round-trips a US-layout `Keys` value through `SDL_GetKeyFromScancode` and back to
  translate for the *current* keyboard layout; scancode mode (`FNA_KEYBOARD_USE_SCANCODES=1`,
  read once into a process-wide cached flag on first use) does a direct physical-position
  lookup via a ported `INTERNAL_scanMap` table and passes `GetKeyFromScancodeEXT`'s input straight
  through unchanged (tasks 764–765).
- `KeyboardState::GetPressedKeys()` returns keys sorted ascending by numeric `Keys` value (task
  760); `GetHashCode()` reconstructs FNA's 8×32-bit word layout and XORs them (task 761);
  `ToString()` matches FNA's `ValueType` default, `"Microsoft.Xna.Framework.Input.KeyboardState"`
  (task 766).
- Storage remains an `std::unordered_set<Keys>` rather than FNA's native bitfield representation
  — an accepted internal-representation deviation; the ordering/hash *contract* matches FNA, the
  storage layout doesn't need to.
- The three SDL↔XNA mapping tables (`try_convert_sdl_key`, `try_convert_sdl_scancode`,
  `try_convert_keys_to_sdl_scancode`) were audited against FNA's `keyMap`/`scanMap`/`xnaMap` (task
  819) and are faithful 1:1 ports — zero missing entries, zero mismatches. The only CNA-specific
  addition is `SDLK_AC_BACK → Keys::Escape` (Android back button; no FNA equivalent). **40 XNA
  `Keys` values have no SDL scancode and intentionally cannot round-trip** through
  `GetKeyFromScancodeEXT` — the IME keys (Kana, Kanji, ImeConvert, …), browser/media keys,
  ChatPad keys, and a few OEM/system keys (Attn, Crsel, Pa1, …); the full list is documented in a
  source comment on `try_convert_keys_to_sdl_scancode`. This matches FNA's own `xnaMap` omissions.

### Mouse

- `Mouse::GetState`, `SetPosition` and `IsRelativeMouseModeEXT` read/control `IPlatformMouse`.
  Missing services return a rest state or no-op. SDL3 and Terminal share the same snapshot shape;
  Terminal truthfully refuses warp/relative mode while still publishing cell-quantised state.
- `SetPosition` converts the caller's logical coordinates to window space before the warp
  (INPUT-MOUSE-002 (decision a-0001)), so the OS cursor lands at the correct client coordinate on
  a scaled window. `GetState` applies the inverse. The `WindowId` renderer registry is the single
  authoritative path for both operations (PLAT-66): the input layer never looks up a native
  window or special-cases one renderer. Renderer implementations own their presentation geometry
  and implement the same platform-client↔logical contract in their own terms. A deterministic
  200×100-client / 100×100-logical test pins the 50-pixel letterbox offset and round trip under all
  platform selections.
- Normalized touch events carry the logical client width/height captured by the platform event
  edge. The shared input bridge first scales into client coordinates and then uses the same
  `WindowId` renderer transform as mouse input, with no per-event native window lookup; the
  gesture path keeps its FNA-compatible `DisplayWidth`/`DisplayHeight` basis.
- `MouseCursor` (custom + 12 stock system cursors) is a MonoGame-derived `CNAEXT` extension — FNA
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
  formula is a reasonable accepted substitute (`docs/input-fna-fidelity.md`).
- `FNA_GAMEPAD_NUM_GAMEPADS` env override is ported (task 734), but clamped to 4 slots since
  `PlayerIndex` is frozen XNA API (only One–Four) — going *above* 4 has no addressable slot.

### Touch

- `SDL_EVENT_FINGER_*` feed `TouchPanel::INTERNAL_onTouchEvent`, which drives
  `GestureDetector`'s state machine directly — this is what makes Tap, DoubleTap, Hold,
  Horizontal/Vertical/Free drag, Flick, Pinch, and PinchComplete recognition work end-to-end
  (the INPUT-TOUCH-* / INPUT-GESTURE-* cluster).
- `TouchDeviceExists` only becomes true after the *first* touch event, matching FNA's own
  comment ("Windows only notices a touch screen once it's touched").
- `TouchPanel::GetState()` reports `TouchLocation`s with their previous location preserved for
  Moved/Released touches, so `TryGetPreviousLocation()` works on the real event-driven path —
  `TouchPanel` stores each touch's previous state/position and advances it once per frame
  (INPUT-TOUCH-007). A new Pressed touch has no previous, matching FNA.
- Known deviation (behaviourally equivalent, not a gap): `GetState()` reads `TouchPanel`'s
  event-driven map rather than FNA's per-frame `SetFinger`/`SDL_GetTouchFingers` poll of
  `touches_` — see §3. The event map is internally unbounded, but
  `GetState()` caps the public snapshot at `MAX_TOUCHES` (8) to match FNA (DEC-10).
- `TouchPanel::GetCapabilities()` reports `MaximumTouchCount = 0` when disconnected and **4** when
  connected, matching FNA's `touchDeviceExists ? 4 : 0` (DEC-09 — XNA always reports 4;
  the value is a fixed XNA-compat constant, not the `MAX_TOUCHES` tracking cap).

### TextInputEXT

- Wired to `SDL_StartTextInput`/`StopTextInput`, `SDL_SetTextInputArea`,
  `SDL_EVENT_TEXT_INPUT`/`_EDITING` (tasks 700–708), including control-character
  synthesis for keys SDL doesn't deliver as `TEXT_INPUT` (Home/End/Back/Tab/Enter/Delete,
  Ctrl+V) and Ctrl+V paste-echo suppression.
- The `TextInput`/`INTERNAL_OnTextInput` callback is `charcs` (`char16_t`) — one UTF-16 code unit
  per call, with astral code points delivered as a high/low surrogate pair, matching FNA's
  `Action<char>` exactly (task 806). The bridge decodes SDL's UTF-8 to UTF-16 via
  `decode_utf8_to_utf16`. `TextEditing` remains a UTF-8 `std::string` (FNA's `Action<string,int,int>`
  maps to `std::string`) — a separate, accepted deviation.

---

## 5. Running the input tests

The canonical gate has two lanes: `CnaInputModuleTests` contains every test owned by
`modules/input/tests`, while `CnaInputCrossModuleTests` selects an explicit set of platform,
runtime and Guide integration suites from `CnaTests`. To build and run both lanes with OPENGLES3:

```bash
git submodule update --init   # first time only (see README)
cmake -S . -B cmake-build-input-easygl -G Ninja -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-input-easygl --target cna_input_test_gate --parallel 8

# Both lanes run shuffled five times for order-independence.
ctest --test-dir cmake-build-input-easygl -L input --output-on-failure
```

`ctest -L input` is authoritative. The module lane needs no filter, so a new input-owned suite joins
automatically. The small cross-module allowlist lives in `cmake/UnitTests.cmake`; its suite-level
patterns prevent unrelated test methods containing words such as "touch" or "keyboard" from entering
the gate. `ctest -L input --output-on-failure` reports the current per-lane counts; no fixed count
is maintained in the documentation.

Order-dependence in process-wide input state (`InputManager`, `TouchPanel`, `GestureDetector`, and the
`MouseCursor` stock cursors) is shaken out by `--gtest_shuffle --gtest_repeat=5` in both lanes.

Swap `-DCNA_GRAPHICS_RENDERER=OPENGLES3` for `SDLGPU` to verify the same input tests on the
other renderer used by input CI. The full suite is just
`./cmake-build-input-easygl/CnaTests` with no filter.

---

## 6. Thread safety

**Input is a single-threaded (game-loop-thread) API.** All input state described above —
`InputManager`'s compatibility state, `GestureDetector`'s state machine,
`TouchPanel`'s touch arrays, event map and gesture queue, and the static `Mouse::ClickedEXT` /
`TextInputEXT` callbacks — is plain process-wide static state with **no locking** (there is no
`mutex`/`atomic`/`thread` under `modules/input/src/Internal/` or `modules/input/src/Xna/`). That is
deliberate and safe because every access happens on one thread:

- **Writes** flow from `Game::PollEvents()` through `PlatformInputBridge` into event accumulators,
  followed by one keyboard and mouse service update into their whole-device snapshots.
  `PollEvents()` is called from `Game::Tick()`, on the thread that runs the game loop.
- **Reads** flow from game code calling `Keyboard/Mouse/GamePad/TouchPanel::GetState()` in
  `Update()`/`Draw()`, which the same loop invokes on that same thread.

This matches XNA/FNA (where `Game.Update`/`Draw` run on the main thread) and is also required by
SDL itself: `SDL_PollEvent` must be pumped on the thread that initialized video / created the
window. The function-local-static singletons (`InputManager`'s state, the `MouseCursor` stock
cursors, `GestureDetector`'s state) rely on C++11's thread-safe static *initialization*, but their
subsequent mutation is unsynchronized — which is fine under the single-thread contract.

**Consequence for game code:** do not call `Get*State()` (or the `Set*`/`INTERNAL_*` entry points)
from a background thread while the game loop is pumping events — that is outside the XNA input
contract and would be an unsynchronized data race. No locking is added because the single-thread
model makes it unnecessary; adding it would only cost per-frame overhead.

**Event-pump freshness (INPUT-KBD-022).** Every `Get*State()` snapshot — keyboard, mouse, gamepad,
touch — is only as fresh as the **last `Game::PollEvents()`** and following service updates (once
per `Game::Tick()`, before `Update()`/`Draw()`). CNA does **not** re-query SDL inside `Get*State()`
(FNA does poll fresh; see §3), so input arriving between two frames is not observed until the next
tick. Within one `Update()` state is stable except for consumed relative mouse displacement.
This is the authoritative statement of both properties (single-thread + per-tick freshness); the
`InputManager` class doc and [`docs/platform-input-notes.md`](platform-input-notes.md) (§Cross-cutting)
point here.
