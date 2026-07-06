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
- [ ] **N-004 `Mouse` cursor visibility EXT** — `SetCursorVisibleEXT`/`getIsCursorVisibleEXT`.
- [ ] **N-005 Mouse horizontal scroll wheel EXT** — surface SDL `wheel.x` (currently dropped, DEC-18).
- [ ] **N-006 `TouchLocation::getPressureEXT`** — expose SDL finger pressure (XNA dropped Pressure).

## Phase P2 — needs an injectable seam, desktop-strong
- [ ] **N-007 `CNA::Input::Joystick`** — raw joystick (axes/buttons/hats/balls); test via virtual joystick.
- [ ] **N-008 `GamePad` touchpad fingers EXT** — `GetTouchpadFingerEXT` + counts + touchpad events.
- [ ] **N-009 `GamePad` battery/power + player-index EXT** — `GetPowerInfoEXT`, `Get/SetPlayerIndexEXT`.
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
- **N-001 done (2026-07-06):** `CNA::Input::Clipboard` — new public `CNA::Input` namespace established
  (`include/CNA/Input/Clipboard.hpp`, `src/CNA/Input/Clipboard.cpp`). `GetTextEXT`/`SetTextEXT`/`HasTextEXT`
  wrap SDL3 clipboard (SDL_free'd read). Tests `CnaInputClipboardTest` (UTF-8 round-trip + empty) — 2 tests,
  Xvfb-gated with GTEST_SKIP fallback. Added `*CnaInput*` to `CNA_INPUT_TEST_FILTER` (one-time, covers all
  future CNA::Input suites named `CnaInput*`). `ctest -L input` 100% green; ASan-clean. Convention set:
  new `CNA::Input` types are `NOXNA` at the class, methods keep the `EXT` suffix, test suites are
  `CnaInput<Type>Test`.
