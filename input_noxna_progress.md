# CNA Input — NOXNA Extension Implementation Progress

> Tracks the autonomous implementation of `input_noxna.md`. **One task = one commit, never batched.**
> Each task: implement (SDL3-only, NOXNA/EXT-tagged, no SDL leak in public headers), build, test
> (`ctest -L input` green + ASan-clean where behavior changes), record here + commit + push.
> **If a task needs a decision only the owner can make, SKIP it and note why (owner is away).**

## Legend
- `[ ]` not started · `[~]` in progress · `[x]` done · `[!]` skipped (needs owner input) / hardware-gated

## Phase P1 — pure/deterministic, broadly supported, headless-testable
- [x] **N-001 `CNA::Input::Clipboard`** — text Get/Set/Has (`SDL_GetClipboardText`/`SetClipboardText`/`HasClipboardText`).
- [x] **N-002 `Keyboard` scancode-name helpers EXT** — `GetScancodeNameEXT`/`GetScancodeFromNameEXT` (physical, layout-independent).
- [x] **N-002b `Keyboard` keycode-name helpers EXT** — `GetKeyNameEXT`/`GetKeyFromNameEXT` (layout-dependent).
- [x] **N-003 `Keyboard::GetModStateEXT` + `KeyModifiersEXT`** — modifier flags (Shift/Ctrl/Alt/Gui + Caps/Num/Scroll/Mode) via `SDL_GetModState` seam.
- [!] **N-004 `Mouse` cursor visibility EXT** — **SKIPPED (superseded, would conflict).** CNA `Game::IsMouseVisible`
  (`Game.hpp:128`) already owns cursor visibility and calls `SDL_ShowCursor()`/`SDL_HideCursor()` with its own
  cached `IsMouseVisible_`. A `Mouse::SetCursorVisibleEXT` would be a second path to the same global SDL state
  and desync Game's cache. `Game.IsMouseVisible` is the XNA-idiomatic API — do not duplicate. (Engineering
  decision, not an owner question.)
- [x] **N-005 Mouse horizontal scroll wheel EXT** — surface SDL `wheel.x` (currently dropped, DEC-18).
- [x] **N-006 `TouchLocation::getPressureEXT`** — expose SDL finger pressure (XNA dropped Pressure).

## Phase P2 — needs an injectable seam, desktop-strong
- [ ] **N-007 `CNA::Input::Joystick`** — raw joystick (axes/buttons/hats/balls); test via virtual joystick.
- [x] **N-008 `GamePad` touchpad fingers EXT** — `GetTouchpadCountEXT`/`GetTouchpadFingerCountEXT`/`GetTouchpadFingerEXT` (poll-based).
- [x] **N-009 `GamePad` player-index EXT** — `Get/SetPlayerIndexEXT` (SDL device player-number LED).
- [x] **N-009b `GamePad` battery/power EXT** — `GetPowerInfoEXT` (`SDL_GetGamepadPowerInfo`) + shared `CNA::Input::PowerStateEXT`.
- [x] **N-010 `GamePad` metadata EXT** — `Get{Name,Path,Serial,FirmwareVersion,SteamHandle}EXT`.
- [x] **N-010b `GamePad` connection-state EXT** — `GetConnectionStateEXT -> {Wired,Wireless,Unknown}`.
- [x] **N-011 `GamePad` button labels EXT** — `GetButtonLabelEXT` (ABXY vs cross/circle/square/triangle).
- [ ] **N-012 `CNA::Input::Pen`** — stylus (pressure/tilt/rotation/eraser/buttons); event-decoded.

## Phase P3 — powerful but platform-narrow / manual actuation
- [ ] **N-013 `CNA::Input::Haptics`** — SDL_haptic force-feedback (constant/periodic/ramp/condition/custom + gain/autocenter).
- [ ] **N-014 `CNA::Input::TextComposition`** — IME candidate lists (`SDL_EVENT_TEXT_EDITING_CANDIDATES`) + input-type hints.
- [x] **N-015 `CNA::Input::Sensors`** — device-level accelerometer/gyro + enumeration (`SDL_sensor`) via seam.
- [x] **N-016 `Mouse` capture / global-position EXT** — `SetCaptureEXT`, `GetGlobalPositionEXT`, `WarpGlobalEXT` via seam.
- [x] **N-017 `CNA::Input::InputDevices`** — enumeration (mice/keyboards/touch id+name) via seam. Hot-plug events -> N-017b.
- [x] **N-017b `InputDevices` hot-plug events** — `Mouse/Keyboard Connected/DisconnectedEXT` multicast events.
- [x] **N-018 `CNA::Input::Power`** — system battery (`SDL_GetPowerInfo`) via injectable seam; reuses `PowerStateEXT`.

## Notes
- New standalone types live in the **public `CNA::Input`** namespace (`include/CNA/Input/`, `src/CNA/Input/`),
  `NOXNA` at the class. NOXNA members on existing XNA types keep the `EXT` suffix + update the signature-freeze
  test + `docs/input-public-api-frozen.md` in the SAME commit.
- No SDL type may appear in a public header (opaque `uintptr_t` / internal seam only).
- Device-query capabilities get an injectable backend + fake (mirror `ISdlGamepadBackend`); real actuation is `[!]`.
- CMake auto-globs `src/**/*.cpp` + `tests/**/*.cpp` (CONFIGURE_DEPENDS) — reconfigure if a fresh file isn't picked up.

## Log
(most recent first — filled as tasks complete)
- **N-017b done (2026-07-06):** `InputDevices::{Mouse,Keyboard}{Connected,Disconnected}EXT` — four
  `System::MulticastAction<uint32_t>` hot-plug events carrying the SDL device id (use `+=`). The
  bridge's ProcessEvent decodes `SDL_EVENT_{MOUSE,KEYBOARD}_{ADDED,REMOVED}` (`event.mdevice.which`/
  `event.kdevice.which`) and Invokes the matching event; `InputDevices::ResetForTests` clears all
  four. Additive NOXNA (no freeze pin). Tests `CnaInputDevicesHotplugTest` feed synthetic device
  events through ProcessEvent and assert the right event fires with the right id + no cross-firing.
  `ctest -L input` green; ASan-clean. Completes the InputDevices feature (enumeration + hot-plug).
- **N-008 done (2026-07-06):** `GamePad::GetTouchpadCountEXT` / `GetTouchpadFingerCountEXT(touchpad)`
  / `GetTouchpadFingerEXT(touchpad, finger, out down,x,y,pressure)` — PS4/PS5-style touchpad finger
  data, **poll-based** (like GetGyro/AccelEXT), NOT event-driven, so no InputManager changes.
  Gamepad-seam pattern #1: added `GetNumGamepadTouchpadFingers` + `GetGamepadTouchpadFinger` to
  `ISdlGamepadBackend` (real = the matching SDL fns; reused existing `GetNumGamepadTouchpads`); fake
  gained `FakeTouchpadFinger` + `FakeGamepadConfig.touchpadFingers[[touchpad]][finger]`. Bridge
  resolves the slot and returns 0/false (out-params reset) when disconnected/out of range. Pinned
  all three in the freeze test + documented. Tests: counts + finger contact/pos read, out-of-range
  touchpad/finger, disconnected slot. `ctest -L input` green; ASan-clean.
- **N-015 done (2026-07-06):** `CNA::Input::Sensors` — host-device motion sensors (distinct from the
  gamepad gyro/accel EXT). `GetSensorsEXT() -> vector<SensorInfoEXT{id,name,SensorTypeEXT}>` +
  `GetAccelerometerEXT(out Vector3)`/`GetGyroscopeEXT(out Vector3)` (m/s² / rad/s), returning false
  when absent. New public header `include/CNA/Input/Sensors.hpp` (enum `SensorTypeEXT` + struct
  `SensorInfoEXT` w/ ==/!= + class). New injectable `ISystemSensorBackend` seam (real enumerates
  `SDL_GetSensors`, maps SDL_SensorType<->EXT, and ReadSensor does open->`SDL_GetSensorData`->close
  on the first matching sensor) with `SetSystemSensorBackendForTests`. Whole types NOXNA/additive
  (no freeze pin). Named `Sensors` (plural, matches the plan's `Sensors::` shape + InputDevices
  style). Tests `CnaInputSensorsTest` + `CnaInputSensorInfoEXTTest` drive a fake: enumeration
  forwarding, accel/gyro sample reads, false-when-absent (out-param untouched), descriptor equality.
  `ctest -L input` green; ASan-clean.
- **N-016 done (2026-07-06):** `Mouse::SetCaptureEXT(bool)`, `GetGlobalPositionEXT(out x,y)`,
  `WarpGlobalEXT(x,y)` — mouse capture + desktop-global cursor position/warp. New injectable
  `ISystemMouseBackend` seam (real = `SDL_CaptureMouse`/`SDL_GetGlobalMouseState`/`SDL_WarpMouse
  Global`) with `SetSystemMouseBackendForTests` — chosen over the Mouse class's usual direct-SDL +
  real-window tests so the global ops are deterministically testable headless (no real desktop
  cursor on CI). GetGlobalPositionEXT truncates SDL's float coords to int (XNA MouseState is int).
  Pinned all three in the freeze test + documented. Tests `MouseGlobalEXTTest` (fake seam): capture
  flag+result forwarding, global position read+truncation, warp coord forwarding+result. `ctest -L
  input` green; ASan-clean.
- **N-017 done (2026-07-06):** `CNA::Input::InputDevices::Get{Mice,Keyboards,TouchDevices}EXT() ->
  vector<InputDeviceInfoEXT{id,name}>` — device enumeration (metadata only; XNA state stays merged).
  New descriptor `include/CNA/Input/InputDeviceInfo.hpp` (id+name, with ==/!=) and public class
  `include/CNA/Input/InputDevices.hpp`. New injectable `ISystemDeviceBackend` seam (real =
  `SDL_GetMice`/`SDL_GetKeyboards`/`SDL_GetTouchDevices` + the *NameForID getters, NULL->"", arrays
  SDL_free'd) with `SetSystemDeviceBackendForTests`. Whole types NOXNA/additive (like Clipboard/
  Power — no freeze pin). Tests `CnaInputDevicesTest` + `CnaInputDeviceInfoEXTTest` drive a fake:
  per-category forwarding, empty lists, and descriptor equality. `ctest -L input` green; ASan-clean.
  Scoped to enumeration; hot-plug ADDED/REMOVED events deferred to N-017b.
- **N-002b done (2026-07-06):** `Keyboard::GetKeyNameEXT(Keys) -> std::string` +
  `GetKeyFromNameEXT(std::string) -> Keys` — the layout-dependent (virtual-key) name and its inverse,
  completing the N-002 name helpers. Bridge resolves Keys -> SDL_Scancode -> (keymap)
  SDL_GetKeyFromScancode -> SDL_GetKeyName; the inverse is SDL_GetKeyFromName -> try_convert_sdl_key.
  Unmapped key -> "", unrecognized name -> Keys::None. Pinned in the freeze test + documented. Tests
  `KeyboardKeyNameEXTTest` gate on SDL_INIT_VIDEO (the keymap needs it; Xvfb in CI) with GTEST_SKIP,
  then assert US-layout names (A/Z/Space), empty for None, name<->key round-trip, unrecognized ->
  None. `ctest -L input` green; ASan-clean.
- **N-002 done (2026-07-06):** `Keyboard::GetScancodeNameEXT(Keys) -> std::string` +
  `GetScancodeFromNameEXT(std::string) -> Keys` — the physical, layout-independent key name and its
  inverse (for rebind UIs). Reuses the bridge's existing `try_convert_keys_to_sdl_scancode` /
  `try_convert_sdl_scancode` tables around `SDL_GetScancodeName` / `SDL_GetScancodeFromName`;
  unmapped key -> "", unrecognized name -> Keys::None. Keyboard delegates to the bridge (same
  pattern as the other keyboard EXT). Fully deterministic headless (static SDL tables, no video/
  layout). Pinned both in the freeze test + documented. Tests `KeyboardScancodeNameEXTTest`: stable
  names (A/Z/Space), empty for None, name<->key round-trip over a key set, unrecognized -> None.
  `ctest -L input` green; ASan-clean. Split the plan's N-002 into scancode-names (this commit,
  deterministic) + N-002b keycode-names (layout-dependent, deferred).
- **N-003 done (2026-07-06):** `Keyboard::GetModStateEXT() -> CNA::Input::KeyModifiersEXT`
  {None,Shift,Ctrl,Alt,Gui,Caps,Num,Scroll,Mode} — active modifier + lock state. New flags enum
  header `include/CNA/Input/KeyModifiers.hpp` (constexpr |,&,~,|=,&= like Buttons/GestureType).
  New injectable `ISystemKeyboardBackend` seam (`SDL_GetModState`) with `SetSystemKeyboardBackend
  ForTests`, so tests inject mod state deterministically (SDL's real mod state only updates on real
  key events). Bridge `GetModState` collapses SDL's L/R variants (KMOD_SHIFT/CTRL/ALT/GUI) into one
  flag each + the 4 lock/mode bits; Keyboard delegates (same pattern as GetKeyFromScancodeEXT).
  Pinned in the freeze test + documented. Tests `KeyboardModStateEXTTest` (fake seam): None,
  per-bit mapping incl. L/R collapse, and a combined state. `ctest -L input` green; ASan-clean.
- **N-006 done (2026-07-06):** `TouchLocation::getPressureEXT()` restores SDL finger pressure (0..1)
  that XNA 4.0 dropped. Added a NOXNA `pressure_` field (default 0) + two NOXNA pressure-carrying
  ctors (4-arg and 6-arg); the XNA ctors leave it 0. **Excluded from Equals/GetHashCode/ToString**
  (kept FNA-frozen — `has_frozen_equality` still holds). Event path: `InternalTouchLocationState`
  gained a `Pressure` field; `InputManager::SetTouchState` takes a defaulted `float pressure` and
  `GetTouchState` builds the snapshot via the pressure ctors; the bridge's 3 FINGER_DOWN/MOTION/UP
  handlers forward `event.tfinger.pressure`. Pinned the getter + both ctors in the freeze test +
  documented in `docs/input-public-api-frozen.md`. Tests: TouchLocation ctor/default + Equals/hash/
  ToString-ignore-pressure (TouchInputTests), and end-to-end pressure through GetState with a
  MOTION update (SdlInputBridgeTouchGestureTests, synthetic finger events). `ctest -L input` green;
  ASan-clean. Files: TouchLocation.hpp/.cpp, InputManager.hpp/.cpp, SdlInputBridge.cpp, freeze
  test, frozen-API doc, TouchInputTests.cpp, SdlInputBridgeTouchGestureTests.cpp.
- **N-018 done (2026-07-06):** `CNA::Input::Power::GetInfoEXT(out secondsLeft, out percent) ->
  PowerStateEXT` — host system battery/charge, reusing the shared `PowerStateEXT` from N-009b.
  Established the "standalone CNA::Input type + injectable *system* seam" pattern (reusable for
  N-016/N-017): new `ISystemPowerBackend` seam (`include/CNA/Internal/Input/SystemPowerBackend.hpp`
  + `.cpp`, real = `SDL_GetPowerInfo`) with `SetSystemPowerBackendForTests`. `Power` resets the
  out-params to -1 then maps SDL->EXT. New public type `include/CNA/Input/Power.hpp` + `src/CNA/
  Input/Power.cpp` (whole class NOXNA, like Clipboard — additive, so no freeze pin / frozen-doc
  entry). Tests `CnaInputPowerTest` drive a fake backend: exhaustive 6-state mapping with
  seconds/percent forwarding + the out-param-reset default. `ctest -L input` green; ASan-clean.
- **N-010b done (2026-07-06):** `GamePad::GetConnectionStateEXT -> CNA::Input::
  GamePadConnectionStateEXT {Unknown,Wired,Wireless}` — how the pad is attached. New enum header
  `include/CNA/Input/GamePadConnectionState.hpp`. Seam `GetGamepadConnectionState` (real =
  `SDL_GetGamepadConnectionState`; fake = config, returns INVALID for an unknown device). Bridge
  maps SDL {Invalid,Unknown}->Unknown, Wired->Wired, Wireless->Wireless; disconnected slot ->
  Unknown. Pinned in the freeze test + documented. Tests: 4-state SDL->EXT mapping + disconnected
  path. `ctest -L input` green; ASan-clean. Files: GamePadConnectionState.hpp (new),
  SdlGamepadBackend.hpp/.cpp, FakeSdlGamepadBackend.hpp, SdlInputBridge.hpp/.cpp, GamePad.hpp/.cpp,
  freeze test, frozen-API doc, SdlGamepadBackendTests.cpp.
- **N-010 done (2026-07-06):** `GamePad::Get{Name,Path,Serial}EXT -> std::string` +
  `GetFirmwareVersionEXT -> uint16` + `GetSteamHandleEXT -> uint64` — device metadata via the
  gamepad seam. Seam gains `GetGamepad{Name,Path,Serial,FirmwareVersion,SteamHandle}` (real =
  the matching SDL getters, with NULL->"" for the string ones; fake = canned config). Bridge
  getters return ""/0 for a disconnected slot. Pinned all five in the freeze test + documented
  in `docs/input-public-api-frozen.md`. Tests: canned-value forwarding + disconnected empties.
  `ctest -L input` green; ASan-clean. Split the tracker's original N-010 into metadata (this
  commit) + N-010b connection-state (new enum). Files: SdlGamepadBackend.hpp/.cpp,
  FakeSdlGamepadBackend.hpp, SdlInputBridge.hpp/.cpp, GamePad.hpp/.cpp, freeze test, frozen-API
  doc, SdlGamepadBackendTests.cpp.
- **N-011 done (2026-07-06):** `GamePad::GetButtonLabelEXT(player, Buttons) -> CNA::Input::
  GamePadButtonLabelEXT {Unknown,A,B,X,Y,Cross,Circle,Square,Triangle}` — the printed glyph for a
  face button, so UI prompts show the right symbol per controller family. New enum header
  `include/CNA/Input/GamePadButtonLabel.hpp`. Seam: `ISdlGamepadBackend::GetGamepadButtonLabel`
  (real = `SDL_GetGamepadButtonLabel`; fake = per-button `buttonLabels` config map). Bridge adds
  `try_convert_xna_button_to_sdl` (public `Buttons` -> `SDL_GamepadButton`, inverse of the existing
  SDL->internal map) + `sdl_button_label_to_ext`, and `GetButtonLabel` returns Unknown for a
  disconnected pad or a non-physical `Buttons` value (stick dirs/triggers) without touching the
  device. Pinned in the freeze test + documented in `docs/input-public-api-frozen.md`. Tests: Xbox
  glyphs, PlayStation glyphs (full label mapping), and the Unknown paths (non-physical / unlabeled /
  disconnected). `ctest -L input` green; ASan-clean. Files: GamePadButtonLabel.hpp (new),
  SdlGamepadBackend.hpp/.cpp, FakeSdlGamepadBackend.hpp, SdlInputBridge.hpp/.cpp, GamePad.hpp/.cpp,
  freeze test, frozen-API doc, SdlGamepadBackendTests.cpp.
- **N-009b done (2026-07-06):** `GamePad::GetPowerInfoEXT(player, out percent)` — reads the pad's
  battery/charge state via the gamepad seam. Introduced the shared public enum
  `CNA::Input::PowerStateEXT {Error,Unknown,OnBattery,NoBattery,Charging,Charged}`
  (`include/CNA/Input/PowerState.hpp`), mirroring `SDL_PowerState`; N-018 (system Power) will
  reuse it. Seam: `ISdlGamepadBackend::GetGamepadPowerInfo` (real = `SDL_GetGamepadPowerInfo`;
  fake = `FakeGamepadConfig.powerState`/`powerPercent`). Bridge `GetPowerInfo` resolves the slot,
  maps SDL→EXT, and returns `Error`+percent=-1 when disconnected. Pinned in the freeze test +
  documented in `docs/input-public-api-frozen.md`. Tests: exhaustive SDL_PowerState→PowerStateEXT
  mapping (6 states) with percent round-trip + disconnected→Error. `ctest -L input` green;
  ASan-clean. Files: PowerState.hpp (new), SdlGamepadBackend.hpp/.cpp, FakeSdlGamepadBackend.hpp,
  SdlInputBridge.hpp/.cpp, GamePad.hpp/.cpp, freeze test, frozen-API doc, SdlGamepadBackendTests.cpp.
- **N-009 done (2026-07-06):** `GamePad::Get/SetPlayerIndexEXT` — reads/sets the SDL device player
  index (the 0-based player-number LED) through the injectable gamepad seam. Established the
  "gamepad-seam-extension" flow: added `GetGamepadPlayerIndex`/`SetGamepadPlayerIndex` to
  `ISdlGamepadBackend` (real = `SDL_Get/SetGamepadPlayerIndex`) + the fake
  (`FakeGamepadConfig.playerIndex` + `setPlayerIndexCalls`/`lastSetPlayerIndex` introspection);
  bridge `Get/SetPlayerIndex` resolve the slot and return -1/false when disconnected; public
  `GamePad::Get/SetPlayerIndexEXT` (NOXNA) delegate. Pinned both in `PublicApiInputSignatureFreeze
  Tests` + documented in `docs/input-public-api-frozen.md`. Tests: `FakeGamepadTest.PlayerIndex
  RoundTripsThroughBackend` (Get reads device index, Set forwards + Get reads back) + `...IsSafe
  ForDisconnectedSlot` (Get→-1, Set→false, backend untouched). `ctest -L input` 100% green;
  ASan-clean. Split the tracker's old N-009 into player-index (this commit) + N-009b battery/power.
  Files: SdlGamepadBackend.hpp/.cpp, FakeSdlGamepadBackend.hpp, SdlInputBridge.hpp/.cpp,
  GamePad.hpp/.cpp, freeze test, frozen-API doc, SdlGamepadBackendTests.cpp.
- **N-005 done (2026-07-06):** Mouse horizontal scroll wheel EXT (reverses DEC-18's drop of `wheel.x`).
  `MouseState::getHorizontalScrollWheelValueEXTProperty` + a NOXNA 9-arg ctor (8-arg XNA ctor unchanged,
  leaves it 0); `InputManager` gained a `HorizontalScrollWheelValue` accumulator + `AddHorizontalScroll
  WheelDelta`; the bridge MOUSE_WHEEL handler now accumulates `(int)wheel.x * 120` (same cast-then-scale
  notch truncation as vertical). **Excluded from Equals/GetHashCode** so those stay FNA-frozen. Established
  the "NOXNA-member-on-frozen-type" flow: pinned the getter + 9-arg ctor in `PublicApiInputSignatureFreeze
  Tests`, documented both in `docs/input-public-api-frozen.md`. Tests: rewrote the old `HorizontalWheelIs
  Ignored` into independence + 120-notch accumulation + truncation tests, + 3 MouseState ctor/equality
  tests. `ctest -L input` 100% green; ASan-clean. Files: MouseState.hpp/.cpp, InputManager.hpp/.cpp,
  SdlInputBridge.cpp, freeze test, frozen-API doc, 2 test files.
- **N-001 done (2026-07-06):** `CNA::Input::Clipboard` — new public `CNA::Input` namespace established
  (`include/CNA/Input/Clipboard.hpp`, `src/CNA/Input/Clipboard.cpp`). `GetTextEXT`/`SetTextEXT`/`HasTextEXT`
  wrap SDL3 clipboard (SDL_free'd read). Tests `CnaInputClipboardTest` (UTF-8 round-trip + empty) — 2 tests,
  Xvfb-gated with GTEST_SKIP fallback. Added `*CnaInput*` to `CNA_INPUT_TEST_FILTER` (one-time, covers all
  future CNA::Input suites named `CnaInput*`). `ctest -L input` 100% green; ASan-clean. Convention set:
  new `CNA::Input` types are `NOXNA` at the class, methods keep the `EXT` suffix, test suites are
  `CnaInput<Type>Test`.
