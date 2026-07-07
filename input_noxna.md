# CNA Input — NOXNA Extension Analysis (SDL3-only)

> **Status: ANALYSIS ONLY — nothing here is implemented.** This document maps how CNA's input layer could be
> extended *beyond* XNA 4.0 with `NOXNA` members on existing types and brand-new types in a `CNA::Input`
> namespace, using **SDL3 exclusively** (including `SDL_haptic`). Per the project owner's direction we do
> **not** consider any non-SDL3 input library — where SDL3 cannot do something, that is recorded as a
> platform limitation, not a reason to add a dependency. The goal is to *squeeze the maximum out of SDL3*.

## 1. Purpose & scope

XNA 4.0's input surface (`Microsoft::Xna::Framework::Input`) is frozen and must stay FNA-faithful. Modern
SDL3 exposes a large amount of input capability that XNA never had (gamepad touchpads, force-feedback
effects, pen/stylus, device sensors, battery, per-device enumeration, clipboard, IME candidates, …). This
analysis catalogs those capabilities and proposes how to surface them without touching the XNA contract, via
two extension axes:

- **Axis A — `NOXNA` members on existing XNA/EXT types.** Small additive getters/methods where SDL3 offers
  "more" of an existing XNA concept (e.g. gamepad battery, touchpad fingers, player-index LED).
- **Axis B — new types in a new `CNA::Input` namespace.** For input concepts XNA has *no* type for
  (raw joystick, haptics, pen, device sensor, power, clipboard, device enumeration, IME candidates).

The `CNA` namespace is the project-designated home for non-XNA extensions (see `CLAUDE.md`), so genuinely new
input types live under `CNA::Input`, never inside `Microsoft::Xna::Framework::Input`.

## 2. Design principles (binding for any future implementation)

1. **Additive & non-breaking.** No existing XNA/EXT signature or enum value changes. The public-API
   signature-freeze + enum-freeze tests must stay green; new members are appended.
2. **Tagging.** Every new member/type carries the `NOXNA` marker and, where consumer-visible on an otherwise
   XNA type, an `EXT` name suffix — matching the existing convention (`GetGyroEXT`, `SetLightBarEXT`, …).
   Genuinely-new standalone types live in `CNA::Input` and are `NOXNA` at the class.
3. **No SDL leak.** No public header may expose an SDL type (enforced by `PublicApiInputCompileTests`). SDL
   handles cross the boundary only as opaque `std::uintptr_t` or via an internal seam — exactly as
   `Mouse::WindowHandle` (opaque `uintptr_t`) and `MouseCursor` (opaque `struct SDL_Cursor;` fwd-decl) do.
4. **Injectable seam for testability.** Hardware input is not headless-testable directly. Each new
   capability that talks to SDL should go through an internal interface with a fake (the proven
   `ISdlGamepadBackend` + `FakeSdlGamepadBackend` pattern), so translation/bookkeeping is unit-tested and
   only real actuation is manual (`[!]`, hardware checklist).
5. **Graceful degradation + honest platform matrix.** Many capabilities are absent on Web/Android. Every
   getter must return a safe "not supported / empty" value there, and every proposal below carries a
   Win/Linux/macOS/Android/Web support cell. Never pretend a capability exists where SDL3 can't deliver it.
6. **Event-driven, single-funnel.** New event-sourced input (pen, gamepad touchpad, sensor) is decoded in
   `SdlInputBridge::ProcessEvent` and accumulated in `InputManager`, matching the existing architecture — no
   second event path.
7. **SDL3-only.** The only library is SDL3 (core + `SDL_haptic`, both already vendored). No new dependency.

## 3. Baseline — what CNA already exposes (do NOT re-propose)

From the current audited surface: `Mouse` relative-mode EXT + `ClickedEXT` + `MouseCursor` (stock +
`FromTexture2D`); `Keyboard::GetKeyFromScancodeEXT`; `GamePad` `GetGUIDEXT` / `SetLightBarEXT` /
`SetTriggerVibrationEXT` / `GetGyroEXT` / `GetAccelerometerEXT`; `Buttons` `Misc1EXT`/`Paddle1-4EXT`/
`TouchPadEXT`; `GamePadCapabilities` `Has{LightBar,TriggerVibrationMotors,Gyro,Accelerometer}EXT` (+ the
`TouchPadEXT` button flag); `TextInputEXT` (whole class); `GestureSample` `FingerId(2)EXT`. Everything below
is **additive** to that.

## 4. Verified SDL3 input surface (vendored `third_party/SDL/include/SDL3`)

The vendored SDL3 provides these input headers, all confirmed present: `SDL_gamepad.h`, `SDL_joystick.h`,
`SDL_haptic.h`, `SDL_sensor.h`, `SDL_pen.h`, `SDL_touch.h`, `SDL_mouse.h`, `SDL_keyboard.h`, `SDL_power.h`,
`SDL_clipboard.h`, `SDL_hidapi.h`, `SDL_guid.h`, plus the relevant events (`SDL_EVENT_PEN_*`,
`SDL_EVENT_GAMEPAD_TOUCHPAD_*`, `SDL_EVENT_GAMEPAD_SENSOR_UPDATE`, `SDL_EVENT_SENSOR_UPDATE`,
`SDL_EVENT_TEXT_EDITING_CANDIDATES`, `SDL_EVENT_MOUSE_ADDED/REMOVED`, `SDL_EVENT_KEYBOARD_ADDED`). Concrete
functions cited per feature below. (`SDL_openxr.h` exists but VR is out of scope.)

---

## 5. Axis A — `NOXNA` members on existing types

Each row: **what to add** · **backing SDL3 API** · **testability** · Win / Linux / mac / Android / Web.

### 5.1 `GamePad` (static) + `GamePadCapabilities`

| Proposed member | Backs onto SDL3 | Test | Win | Lin | mac | And | Web |
|---|---|---|---|---|---|---|---|
| `GamePad::GetTouchpadFingerEXT(player, touchpad, finger, out state,x,y,pressure)` + `GetTouchpadCountEXT` / `GetTouchpadFingerCountEXT` | `SDL_GetNumGamepadTouchpads`, `SDL_GetNumGamepadTouchpadFingers`, `SDL_GetGamepadTouchpadFinger`; events `SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN/MOTION/UP` | fake backend + synthetic touchpad events | ✓ (HIDAPI PS4/5) | ✓ | ✓ | ~ | ✗ |
| `GamePad::GetPowerInfoEXT(player, out percent) -> BatteryStateEXT` | `SDL_GetGamepadPowerInfo` | fake reports state+percent | ✓ | ✓ | ✓ | ~ | ~ (Gamepad API battery, spotty) |
| `GamePad::GetPlayerIndexEXT` / `SetPlayerIndexEXT` (the player-number LED) | `SDL_GetGamepadPlayerIndex` / `SDL_SetGamepadPlayerIndex` | fake records set index | ✓ | ✓ | ✓ | ~ | ✗ |
| `GamePad::GetNameEXT` / `GetPathEXT` / `GetSerialEXT` / `GetFirmwareVersionEXT` / `GetSteamHandleEXT` | `SDL_GetGamepadName/Path/Serial/FirmwareVersion/SteamHandle` | fake returns canned metadata | ✓ | ✓ | ✓ | ~ | ~ (name only) |
| `GamePad::GetConnectionStateEXT -> {Wired,Wireless,Unknown}` | `SDL_GetGamepadConnectionState` | fake | ✓ | ✓ | ✓ | ~ | ✗ |
| `GamePad::GetSensorDataRateEXT(player, sensor)` | `SDL_GetGamepadSensorDataRate` | fake | ✓ | ✓ | ✓ | ~ | ✗ |
| `GamePadCapabilities::getHasTouchpadEXT` / `getTouchpadCountEXT` | `SDL_GetNumGamepadTouchpads` | fake caps | ✓ | ✓ | ✓ | ~ | ✗ |
| `GamePadCapabilities::getHasRumbleEXT` / `getHasRumbleTriggersEXT` (explicit flags) | `SDL_PROP_GAMEPAD_CAP_*` (already read internally) | fake caps | ✓ | ✓ | ✓ | ~ | ~ |

*Note:* CNA already exposes rumble/trigger-rumble via `SetVibration`/`SetTriggerVibrationEXT`; the touchpad
**finger positions** and **battery/player-index/metadata** are the genuinely new gamepad data.

### 5.2 `GamePadButtons` / `Buttons`

- SDL3 defines gamepad button *labels* per type (ABXY vs cross/circle/square/triangle):
  `SDL_GetGamepadButtonLabel`, `SDL_GetGamepadButtonLabelForType`. Proposal: `GamePad::GetButtonLabelEXT(player,
  button) -> GamePadButtonLabelEXT {A,B,X,Y,Cross,Circle,Square,Triangle}`. Lets UI prompts show the right
  glyph. Win/Lin/mac ✓, Android/Web ~ (type may be Unknown). Test: fake reports type → label.

### 5.3 `MouseState` / `Mouse`

| Proposed member | SDL3 | Test | Win | Lin | mac | And | Web |
|---|---|---|---|---|---|---|---|
| `Mouse::SetCursorVisibleEXT(bool)` / `getIsCursorVisibleEXT` | `SDL_ShowCursor`/`SDL_HideCursor`/`SDL_CursorVisible` | real hidden-window (Xvfb) | ✓ | ✓ | ✓ | ✗ | ✓ |
| `Mouse::SetCaptureEXT(bool)` (grab motion outside window) | `SDL_CaptureMouse` | real window | ✓ | ✓ | ✓ | ✗ | ✗ (security) |
| `Mouse::GetGlobalPositionEXT(out x,y)` / `WarpGlobalEXT` | `SDL_GetGlobalMouseState`, `SDL_WarpMouseGlobal` | real window (skip headless) | ✓ | ✓ | ✓ | ✗ | ✗ |
| `MouseState::getHorizontalScrollWheelValueEXT` (SDL wheel.x) | `SDL_MouseWheelEvent.x` (already dropped by DEC-18) | bridge synth wheel.x | ✓ | ✓ | ✓ | ~ | ✓ |
| `Mouse::CreateCursorFromSystemEXT` beyond the 12 stock (e.g. animated) | `SDL_CreateAnimatedCursor` | real cursor (Xvfb) | ✓ | ✓ | ✓ | ✗ | ~ |

*Horizontal wheel note:* XNA's `MouseState` has no horizontal wheel (DEC-18 drops SDL `wheel.x`). A
`getHorizontalScrollWheelValueEXT` accumulator would surface it without changing the XNA field. Low effort,
broadly supported — a good early candidate.

### 5.4 `KeyboardState` / `Keyboard`

| Proposed member | SDL3 | Test | Win | Lin | mac | And | Web |
|---|---|---|---|---|---|---|---|
| `Keyboard::GetModStateEXT -> KeyModifiersEXT` (flags: Shift/Ctrl/Alt/Gui/Caps/Num/Scroll/Mode) | `SDL_GetModState` | fake mod state | ✓ | ✓ | ✓ | ~ | ✓ |
| `Keyboard::GetKeyNameEXT(Keys)` / `GetScancodeNameEXT` | `SDL_GetKeyName`/`SDL_GetScancodeName` | deterministic (string map) | ✓ | ✓ | ✓ | ✓ | ✓ |
| `Keyboard::GetKeyFromNameEXT` / `GetScancodeFromNameEXT` | `SDL_GetKeyFromName`/`SDL_GetScancodeFromName` | deterministic | ✓ | ✓ | ✓ | ✓ | ✓ |

*`GetModState`/key-name helpers are pure/deterministic → fully unit-testable headless.* High value for
rebindable-controls UIs; strong early candidates.

### 5.5 `TouchLocation` / `TouchPanel`

- `TouchLocation::getPressureEXT` — SDL fingers carry pressure (`SDL_Finger.pressure`, 0..1). XNA 4.0
  dropped `Pressure`; a NOXNA getter restores it without changing the type's XNA shape. Win/Lin/mac/Android
  ✓ (device-dependent), Web ~ (Pointer Events pressure). Test: bridge synth finger with pressure.
- `TouchPanel::GetTouchDeviceTypeEXT -> {Direct,IndirectAbsolute,IndirectRelative}` — `SDL_GetTouchDeviceType`.
- `TouchPanel::GetTouchDevicesEXT()` / device names — `SDL_GetTouchDevices`, `SDL_GetTouchDeviceName` (see
  device enumeration §6.7).

---

## 6. Axis B — new `CNA::Input` types

These have no XNA analog, so they are standalone `NOXNA` types in `CNA::Input`. Each names its SDL3 backing,
proposed shape, testability, and platform reach.

### 6.1 `CNA::Input::Joystick` (raw joystick — flight sticks, wheels, throttles)

XNA only ever modeled Xbox-style gamepads. SDL3's raw joystick API exposes arbitrary axes/buttons/hats/balls
— essential for flight sims, racing wheels, HOTAS. **Proposed:** a `Joystick` handle type + a `Joysticks`
static enumerator.
- **Backing:** `SDL_GetJoysticks`, `SDL_OpenJoystick`, `SDL_GetNumJoystickAxes/Buttons/Hats/Balls`,
  `SDL_GetJoystickAxis/Button/Hat/Ball`, `SDL_GetJoystickName/GUID/Type/PowerInfo`, events
  `SDL_EVENT_JOYSTICK_AXIS/BUTTON/HAT/BALL/ADDED/REMOVED`. Optional: virtual joysticks
  (`SDL_AttachVirtualJoystick`, `SDL_SetJoystickVirtual*`) for testing/software pads.
- **Shape:** `JoystickState` (axes[], buttons[], hats[], balls[]) + `JoystickCapabilities` (counts, type,
  GUID, name, power) + `Joysticks::GetState(index)` / `GetCapabilities` / `Count` / connect events.
- **Test:** the virtual-joystick API OR an injectable joystick backend (mirror `ISdlGamepadBackend`) →
  fully unit-testable translation.
- **Platforms:** Win ✓ · Lin ✓ · mac ✓ · Android ~ (some controllers) · Web ~ (Gamepad API, mapped only).
- **Priority: HIGH** — biggest genuinely-missing input category; broad desktop support; testable via virtual.

### 6.2 `CNA::Input::Haptics` (force-feedback — **SDL_haptic deep-dive**, requested)

`SDL_haptic` is the full force-feedback API — far richer than the dual-motor `SetVibration` rumble CNA has
today. It targets wheels/joysticks with real actuators (and some gamepads/mice).

**Devices & opening:** `SDL_GetHaptics` (enumerate), `SDL_OpenHaptic` / `SDL_OpenHapticFromJoystick` /
`SDL_OpenHapticFromMouse`, `SDL_IsJoystickHaptic` / `SDL_IsMouseHaptic`, `SDL_CloseHaptic`,
`SDL_GetHapticName/ID`, `SDL_GetNumHapticAxes`.

**Feature detection:** `SDL_GetHapticFeatures` (bitmask), `SDL_HapticEffectSupported(effect)`,
`SDL_GetMaxHapticEffects`, `SDL_GetMaxHapticEffectsPlaying`, `SDL_HapticRumbleSupported`.

**Effect families (all `SDL_HAPTIC_*` confirmed in the header):**
- *Simple rumble:* `SDL_InitHapticRumble`, `SDL_PlayHapticRumble`, `SDL_StopHapticRumble` (magnitude+duration).
- *Constant:* `SDL_HAPTIC_CONSTANT` — steady directional push.
- *Periodic waveforms:* `SDL_HAPTIC_SINE`, `SDL_HAPTIC_SQUARE`, `SDL_HAPTIC_TRIANGLE`, `SDL_HAPTIC_SAWTOOTHUP`,
  `SDL_HAPTIC_SAWTOOTHDOWN` (period, magnitude, offset, phase, attack/fade envelope).
- *Ramp:* `SDL_HAPTIC_RAMP` — linear start→end magnitude.
- *Condition effects (wheels):* `SDL_HAPTIC_SPRING`, `SDL_HAPTIC_DAMPER`, `SDL_HAPTIC_INERTIA`,
  `SDL_HAPTIC_FRICTION` (per-axis, with `SDL_HAPTIC_STEERING_AXIS`).
- *Left/right (gamepad motors):* `SDL_HAPTIC_LEFTRIGHT`.
- *Custom:* `SDL_HAPTIC_CUSTOM` (raw sample buffer).
- *Global modifiers:* `SDL_SetHapticGain` (`SDL_HAPTIC_GAIN`), `SDL_SetHapticAutocenter` (`SDL_HAPTIC_AUTOCENTER`),
  `SDL_PauseHaptic`/`SDL_ResumeHaptic` (`SDL_HAPTIC_PAUSE`/`SDL_HAPTIC_STATUS`).
- *Directions:* polar / cartesian / spherical (`SDL_HAPTIC_POLAR`/`CARTESIAN`/`SPHERICAL`), `SDL_HAPTIC_INFINITY`
  for endless effects.

**Lifecycle:** `SDL_CreateHapticEffect` → `SDL_RunHapticEffect(count)` → `SDL_UpdateHapticEffect` →
`SDL_GetHapticEffectStatus` → `SDL_StopHapticEffect` / `SDL_StopHapticEffects` → `SDL_DestroyHapticEffect`.

- **Proposed shape (`CNA::Input`):** `Haptics::Open(deviceOrJoystick)` → `HapticDevice` (RAII, like
  `MouseCursor`); `HapticDevice::getFeaturesEXT()` (flags), `getMaxEffectsEXT()`; a `HapticEffect` builder
  wrapping each family (`Constant`, `Periodic{Sine,Square,…}`, `Ramp`, `Condition{Spring,Damper,Inertia,
  Friction}`, `LeftRight`, `Custom`) with the envelope/direction params mapped to CNA `Vector`/`TimeSpan`;
  `HapticDevice::Run/Update/Stop(effectHandle, iterations)`; `SetGain`/`SetAutocenter`/`Pause`/`Resume`;
  plus a convenience `HapticDevice::Rumble(strength, duration)` for the common case. **No SDL type leaks** —
  the effect params are CNA structs converted internally to `SDL_HapticEffect`.
- **Test:** an injectable `IHapticBackend` + fake records opened devices, created/updated/run/stopped effects
  and their parameters — translation and bookkeeping fully unit-tested; **real actuation is manual (`[!]`)**.
- **Platforms:** Win ✓ (DirectInput/XInput FFB) · Lin ✓ (evdev force-feedback) · mac ~ (limited FFB) ·
  Android ✗ (rumble only, not `SDL_haptic` effects) · Web ✗ (Gamepad API "dual-rumble"/"trigger-rumble"
  actuators only — no SDL_haptic effect model). → On Android/Web, `Haptics::Open` returns "unsupported" and
  callers fall back to `GamePad::SetVibration`.
- **Priority: MEDIUM-HIGH** — unlocks proper wheel/joystick FFB on desktop; must degrade cleanly on Web/Android.

### 6.3 `CNA::Input::Pen` (stylus / tablet — SDL3-new)

SDL3 has a first-class pen API absent from XNA and MonoGame. High-value for drawing/note apps.
- **Backing:** events `SDL_EVENT_PEN_DOWN/UP/MOTION/BUTTON_DOWN/BUTTON_UP/AXIS/PROXIMITY_IN/PROXIMITY_OUT`;
  axes `SDL_PEN_AXIS_{PRESSURE,XTILT,YTILT,ROTATION,DISTANCE,SLIDER,TANGENTIAL_PRESSURE}`; input flags
  `SDL_PEN_INPUT_{DOWN,BUTTON_*,ERASER_TIP,IN_PROXIMITY}`; device type `SDL_PEN_DEVICE_TYPE_*`.
- **Shape:** `PenState` (position, pressure, tiltX/Y, rotation, distance, buttons, eraser, inProximity) +
  event-driven `PenPanel`-style accumulation in InputManager, or a `Pen::GetState()` snapshot. `PenSample`
  for stroke events (mirrors `GestureSample`).
- **Test:** bridge decodes synthetic `SDL_EVENT_PEN_*` → deterministic unit tests (like the touch/gesture
  bridge tests). Fully headless-testable.
- **Platforms:** Win ✓ · Lin ✓ (Wayland/X11 tablet) · mac ✓ · Android ✓ (stylus) · Web ~ (Pointer Events
  pen). Broadly supported.
- **Priority: MEDIUM-HIGH** — modern, well-supported, cleanly testable, genuinely new.

### 6.4 `CNA::Input::Sensor` (device-level motion sensors)

Distinct from gamepad gyro/accel (already EXT): the *host device's* own sensors (phone/tablet/laptop).
- **Backing:** `SDL_GetSensors`, `SDL_OpenSensor`, `SDL_GetSensorType` (`SDL_SENSOR_ACCEL`/`GYRO`/`_L`/`_R`),
  `SDL_GetSensorData`, `SDL_GetSensorDataRate`, event `SDL_EVENT_SENSOR_UPDATE`.
- **Shape:** `Sensors::GetAccelerometer(out Vector3)` / `GetGyroscope(out Vector3)` / enumeration + a
  `SensorState`; standard SDL units (accel m/s², gyro rad/s).
- **Test:** injectable sensor backend + fake feeds canned samples → testable; real values manual.
- **Platforms:** Win ~ (Sensor API) · Lin ~ (iio) · mac ✗ (no laptop motion) · Android ✓ · Web ~
  (DeviceMotion/Orientation, permission-gated). → Mainly a mobile feature.
- **Priority: MEDIUM** — valuable on Android; sparse on desktop.

### 6.5 `CNA::Input::Clipboard`

Ctrl+V text is already synthesized, but there is no read/write clipboard API. Broadly useful (text fields,
copy/paste, drag content).
- **Backing:** `SDL_GetClipboardText`/`SDL_SetClipboardText`/`SDL_HasClipboardText`; typed data
  `SDL_GetClipboardData`/`SDL_SetClipboardData`/`SDL_GetClipboardMimeTypes`/`SDL_HasClipboardData`/
  `SDL_ClearClipboardData`; primary selection (`SDL_*PrimarySelectionText`, X11).
- **Shape:** `Clipboard::GetTextEXT()` / `SetTextEXT(string)` / `HasTextEXT()`; optionally typed
  `GetDataEXT(mime)` / `SetDataEXT(mime, bytes)`.
- **Test:** desktop round-trip test (set→get) under a real video subsystem (Xvfb); text path deterministic.
- **Platforms:** Win ✓ · Lin ✓ · mac ✓ · Android ✓ (text) · Web ~ (async Clipboard API, **permission +
  user-gesture gated**, text mostly; `SetText` may be blocked without a user gesture).
- **Priority: MEDIUM-HIGH** — small surface, big utility, near-universal (Web caveats documented).

### 6.6 `CNA::Input::TextComposition` (IME candidates — extends TextInputEXT)

SDL3 adds candidate-list IME events XNA/older-SDL lacked.
- **Backing:** `SDL_EVENT_TEXT_EDITING_CANDIDATES` (candidate strings + selected index + horizontal/vertical),
  `SDL_SetTextInputArea`/`SDL_GetTextInputArea`, `SDL_TextInputType` hints (`SDL_StartTextInputWithProperties`).
- **Shape:** extend `TextInputEXT` with a `TextEditingCandidatesEXT` multicast event
  `(candidates[], selected, horizontal)` and a `StartTextInputWithTypeEXT(TextInputTypeEXT)` for input hints
  (text / URL / email / number / password). Could also live as `CNA::Input::TextComposition`.
- **Test:** bridge decodes a synthetic candidates event → deterministic; real IME is manual (`[!]`).
- **Platforms:** Win ✓ · Lin ✓ · mac ✓ · Android ✓ · Web ~ (browser IME). CJK-input value.
- **Priority: MEDIUM** — high value for CJK locales; candidate decode is testable, real composition is manual.

### 6.7 `CNA::Input::InputDevices` (enumeration + hot-plug)

XNA assumes one keyboard/one mouse/4 gamepads. SDL3 enumerates every device with names and hot-plug events.
- **Backing:** `SDL_GetMice`/`SDL_GetMouseNameForID` + `SDL_EVENT_MOUSE_ADDED/REMOVED`;
  `SDL_GetKeyboards`/`SDL_GetKeyboardNameForID` + `SDL_EVENT_KEYBOARD_ADDED/REMOVED`;
  `SDL_GetTouchDevices`/`SDL_GetTouchDeviceName`; (gamepads/joysticks via their own enumerators).
- **Shape:** `InputDevices::GetMice()` / `GetKeyboards()` / `GetTouchDevices()` → id+name lists; hot-plug
  events. (Note: XNA state stays *merged* — this is metadata/enumeration, not per-device state routing,
  which would be a much larger design.)
- **Test:** enumeration is queryable; hot-plug via synthetic ADDED/REMOVED events (deterministic).
- **Platforms:** Win ✓ · Lin ✓ · mac ~ · Android ✗ (single logical device) · Web ✗ (browser abstracts).
- **Priority: LOW-MEDIUM** — desktop-only metadata; nice-to-have.

### 6.8 `CNA::Input::Power` (system battery)

- **Backing:** `SDL_GetPowerInfo(out seconds, out percent) -> SDL_PowerState`.
- **Shape:** `Power::GetInfoEXT(out secondsLeft, out percent) -> PowerStateEXT {Unknown,OnBattery,NoBattery,
  Charging,Charged}`.
- **Test:** trivially wrappable behind a seam; value manual.
- **Platforms:** Win ✓ · Lin ✓ · mac ✓ · Android ✓ · Web ~ (Battery API, deprecated in some browsers).
- **Priority: LOW** — tiny surface, occasionally useful (pause on low battery).

---

## 7. Master platform-support matrix (SDL3)

Legend: ✓ full · ~ partial/limited · ✗ unsupported (SDL3 platform limitation, not a CNA gap).

| Capability | Win | Linux | macOS | Android | Web (Emscripten) |
|---|---|---|---|---|---|
| Gamepad touchpad fingers | ✓ | ✓ | ✓ | ~ | ✗ |
| Gamepad battery / player-index / metadata | ✓ | ✓ | ✓ | ~ | ~ |
| Gamepad button labels (ABXY vs ✕◯□△) | ✓ | ✓ | ✓ | ~ | ~ |
| Raw Joystick (axes/buttons/hats/balls) | ✓ | ✓ | ✓ | ~ | ~ (mapped) |
| Virtual joystick (for tests) | ✓ | ✓ | ✓ | ✓ | ✓ |
| **SDL_haptic** force-feedback effects | ✓ | ✓ | ~ | ✗ | ✗ |
| Simple rumble (fallback) | ✓ | ✓ | ✓ | ✓ | ~ |
| Pen / stylus (pressure/tilt/rotation) | ✓ | ✓ | ✓ | ✓ | ~ |
| Device sensors (accel/gyro) | ~ | ~ | ✗ | ✓ | ~ (permission) |
| Clipboard text | ✓ | ✓ | ✓ | ✓ | ~ (permission/gesture) |
| IME candidate lists | ✓ | ✓ | ✓ | ✓ | ~ |
| Device enumeration + hot-plug | ✓ | ✓ | ~ | ✗ | ✗ |
| Mouse capture / global position | ✓ | ✓ | ✓ | ✗ | ✗ |
| Cursor visibility | ✓ | ✓ | ✓ | ✗ | ✓ |
| Horizontal scroll wheel | ✓ | ✓ | ✓ | ~ | ✓ |
| Keyboard mod-state / key names | ✓ | ✓ | ✓ | ~ | ✓ |
| System power / battery | ✓ | ✓ | ✓ | ✓ | ~ |

## 8. Proposed prioritization / phasing

Ordered by (value × SDL3 support-breadth × headless-testability):

**Phase P1 — high value, broadly supported, fully headless-testable (do first):**
1. `Keyboard` mod-state + key/scancode name helpers (§5.4) — pure/deterministic, universal.
2. `Mouse` cursor visibility + horizontal-wheel EXT (§5.3) — small, broad, testable.
3. `CNA::Input::Clipboard` text (§6.5) — tiny surface, near-universal, desktop round-trip testable.
4. `TouchLocation::getPressureEXT` (§5.5) — one field, event-decodable.

**Phase P2 — high value, needs an injectable seam, desktop-strong:**
5. `CNA::Input::Joystick` raw joystick (§6.1) — biggest gap; test via **virtual joystick**.
6. `GamePad` touchpad fingers + battery + player-index + metadata + button labels (§5.1/5.2).
7. `CNA::Input::Pen` (§6.3) — modern, broad, event-decodable.

**Phase P3 — powerful but platform-narrow or manual-actuation:**
8. `CNA::Input::Haptics` / `SDL_haptic` (§6.2) — desktop FFB; fake-tested translation, manual actuation;
   graceful "unsupported" on Android/Web.
9. `CNA::Input::TextComposition` IME candidates (§6.6) — CJK value; candidate decode testable, composition manual.
10. `CNA::Input::Sensor` device motion (§6.4) — mainly Android.
11. `Mouse` capture / global-position (§5.3), `CNA::Input::InputDevices` (§6.7), `CNA::Input::Power` (§6.8).

## 9. Testability strategy

- **Deterministic (pure) helpers** — mod-state, key/scancode names, clipboard text round-trip, horizontal
  wheel — are directly unit-testable headless.
- **Event-sourced input** (pen, gamepad touchpad, IME candidates, hot-plug) — decode synthetic `SDL_Event`s
  through `SdlInputBridge::ProcessEvent` and assert accumulated state, exactly like the existing touch/
  gesture/text bridge tests. Fully headless.
- **Device-query input** (raw joystick, haptics, sensors, gamepad metadata/battery) — put an internal
  interface (`IJoystickBackend`, `IHapticBackend`, `ISensorBackend`) with a fake, mirroring
  `ISdlGamepadBackend`/`FakeSdlGamepadBackend`. Unit-test translation + bookkeeping; mark **real actuation/
  readback manual (`[!]`)** in the hardware checklist.
- **Virtual joystick** (`SDL_AttachVirtualJoystick`) is a genuine SDL3 way to drive the raw-joystick path
  without hardware — usable in an integration test.
- Every new capability gets its own `ctest -L input` coverage and stays ASan/UBSan-clean and
  order-independent (shuffle×5), like the current suite.

## 10. Risks & open design questions

1. **Scope creep.** The catalog is deliberately broad; P1 alone is a solid, shippable increment. Recommend
   implementing phase-by-phase, one type/capability per PR, mirroring the audit's one-task-one-commit rhythm.
2. **No-SDL-leak discipline** for the richer types (Haptics effect structs, Joystick handles) — must convert
   SDL structs to CNA structs at the boundary; verify with `PublicApiInputCompileTests`.
3. **Graceful degradation contract.** Decide the uniform "unsupported" convention (return `false`/empty +
   an `IsSupportedEXT` query) so Web/Android callers branch cleanly. Haptics especially must fall back to
   `GamePad::SetVibration`.
4. **Namespace shape.** Confirm `CNA::Input` (vs `CNA::Internal::Input`, which is internal-only) as the
   public home for the new standalone types.
5. **Event volume.** Pen/touchpad/sensor emit high-frequency events; ensure the single-funnel bridge stays
   O(1) per event (it already is for touch).
6. **`SDL_INIT_*` ownership.** Haptic/sensor/joystick each need their subsystem initialized idempotently
   (as gamepad already is via `EnsureGamepadSubsystemInitialized`); extend that pattern, don't init eagerly.
7. **Manual-validation debt.** Haptics actuation, real pen hardware, device sensors, IME composition, and
   FFB wheels all add to the Phase-11 hardware checklist — budget for it.

## 11. Bottom line

SDL3 alone can back a large, coherent set of NOXNA input extensions with **no additional dependency**. The
best early wins are the pure/deterministic helpers (keyboard names + mod state, clipboard text, cursor
visibility, horizontal wheel, touch pressure) and the raw **Joystick** type (testable via SDL3 virtual
joysticks). **`SDL_haptic`** is the richest single capability — a full force-feedback model (constant,
periodic, ramp, condition, custom, plus gain/autocenter) that far exceeds today's rumble — but it is
desktop-only in SDL3 (✗ on Web/Android), so it ships behind an injectable seam with a clean rumble fallback.
Everything is additive, `NOXNA`/`EXT`-tagged, SDL-leak-free, and testable through the proven fake-backend +
synthetic-event patterns; only real hardware actuation stays manual.
