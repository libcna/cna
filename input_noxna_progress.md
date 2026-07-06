# CNA Input — NOXNA Extension Implementation Progress

> Tracks the autonomous implementation of `input_noxna.md`. **One task = one commit, never batched.**
> Each task: implement (SDL3-only, NOXNA/EXT-tagged, no SDL leak in public headers), build, test
> (`ctest -L input` green + ASan-clean where behavior changes), record here + commit + push.
> **If a task needs a decision only the owner can make, SKIP it and note why (owner is away).**

## Legend
- `[ ]` not started · `[~]` in progress · `[x]` done · `[!]` skipped (needs owner input) / hardware-gated

## Phase P1 — pure/deterministic, broadly supported, headless-testable
- [x] **N-001 `CNA::Input::Clipboard`** — text Get/Set/Has (`SDL_GetClipboardText`/`SetClipboardText`/`HasClipboardText`).
- [ ] **N-002 `Keyboard` name helpers EXT** — `GetKeyNameEXT`/`GetScancodeNameEXT`/`GetKeyFromNameEXT`/`GetScancodeFromNameEXT`.
- [ ] **N-003 `Keyboard::GetModStateEXT` + `KeyModifiersEXT`** — modifier flags (Shift/Ctrl/Alt/Gui + Caps/Num/Scroll lock).
- [!] **N-004 `Mouse` cursor visibility EXT** — **SKIPPED (superseded, would conflict).** CNA `Game::IsMouseVisible`
  (`Game.hpp:128`) already owns cursor visibility and calls `SDL_ShowCursor()`/`SDL_HideCursor()` with its own
  cached `IsMouseVisible_`. A `Mouse::SetCursorVisibleEXT` would be a second path to the same global SDL state
  and desync Game's cache. `Game.IsMouseVisible` is the XNA-idiomatic API — do not duplicate. (Engineering
  decision, not an owner question.)
- [x] **N-005 Mouse horizontal scroll wheel EXT** — surface SDL `wheel.x` (currently dropped, DEC-18).
- [ ] **N-006 `TouchLocation::getPressureEXT`** — expose SDL finger pressure (XNA dropped Pressure).

## Phase P2 — needs an injectable seam, desktop-strong
- [ ] **N-007 `CNA::Input::Joystick`** — raw joystick (axes/buttons/hats/balls); test via virtual joystick.
- [ ] **N-008 `GamePad` touchpad fingers EXT** — `GetTouchpadFingerEXT` + counts + touchpad events.
- [x] **N-009 `GamePad` player-index EXT** — `Get/SetPlayerIndexEXT` (SDL device player-number LED).
- [ ] **N-009b `GamePad` battery/power EXT** — `GetPowerInfoEXT` (`SDL_GetGamepadPowerInfo`), split off from N-009.
- [ ] **N-010 `GamePad` metadata EXT** — name/path/serial/firmware/Steam-handle/connection-state.
- [ ] **N-011 `GamePad` button labels EXT** — ABXY vs cross/circle/square/triangle.
- [ ] **N-012 `CNA::Input::Pen`** — stylus (pressure/tilt/rotation/eraser/buttons); event-decoded.

## Phase P3 — powerful but platform-narrow / manual actuation
- [ ] **N-013 `CNA::Input::Haptics`** — SDL_haptic force-feedback (constant/periodic/ramp/condition/custom + gain/autocenter).
- [ ] **N-014 `CNA::Input::TextComposition`** — IME candidate lists (`SDL_EVENT_TEXT_EDITING_CANDIDATES`) + input-type hints.
- [ ] **N-015 `CNA::Input::Sensor`** — device-level accelerometer/gyro (`SDL_sensor`).
- [ ] **N-016 `Mouse` capture / global-position EXT** — `SetCaptureEXT`, `GetGlobalPositionEXT`, `WarpGlobalEXT`.
- [ ] **N-017 `CNA::Input::InputDevices`** — enumeration + hot-plug (mice/keyboards/touch devices).
- [ ] **N-018 `CNA::Input::Power`** — system battery (`SDL_GetPowerInfo`).

## Notes
- New standalone types live in the **public `CNA::Input`** namespace (`include/CNA/Input/`, `src/CNA/Input/`),
  `NOXNA` at the class. NOXNA members on existing XNA types keep the `EXT` suffix + update the signature-freeze
  test + `docs/input-public-api-frozen.md` in the SAME commit.
- No SDL type may appear in a public header (opaque `uintptr_t` / internal seam only).
- Device-query capabilities get an injectable backend + fake (mirror `ISdlGamepadBackend`); real actuation is `[!]`.
- CMake auto-globs `src/**/*.cpp` + `tests/**/*.cpp` (CONFIGURE_DEPENDS) — reconfigure if a fresh file isn't picked up.

## Log
(most recent first — filled as tasks complete)
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
