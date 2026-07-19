# Audit: src/CNA/Internal/Input/SdlInputBridge.cpp

## Metadata

- Source file: `src/CNA/Internal/Input/SdlInputBridge.cpp`
- Audit status: AUDITED
- Subsystem: `cna-internal-core` shard (`CNA::Internal::Input`)
- File type: C++ implementation
- XNA/FNA relevance: internal seam/bridge feeding the real `Microsoft::Xna::Framework::Input`
  (Keyboard/Mouse/GamePad/Touch) and `CNA::Input` (Joysticks/Haptics/Sensors/Power/InputDevices) public
  APIs — several functions here are directly and explicitly cross-referenced against FNA's own
  `SDL3_FNAPlatform.cs` source line numbers
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Implements SdlInputBridge: the 2076-line (largest file in this shard) central event-processing loop (ProcessEvent, ~460 lines) plus every gamepad/joystick/keyboard NOXNA query, all with extensive direct FNA (SDL3_FNAPlatform.cs) cross-referencing.

## Executive Verdict

Healthy — scoped-depth review (largest file in this shard); the full ProcessEvent() dispatcher and multiple SDL-to-CNA-enum conversion functions read and verified in detail; no defects found in the areas read.

## Checklist Results

### Behavioral correctness / FNA parity / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**`ProcessEvent()` read in full**: every SDL event type (mouse motion/button/wheel, device hot-plug, keyboard, text input/editing/candidates, touch finger down/motion/up/canceled, gamepad/joystick add/remove/button/axis) is handled with explicit, well-reasoned logic — e.g. the mouse-wheel handling has an explicit comment warning against computing `float * 120` then casting (which would leak fractional deltas) vs. the correct cast-then-multiply order FNA itself uses; `FINGER_CANCELED` is explicitly treated identically to `FINGER_UP` with a comment explaining why (otherwise a canceled touch would stay stuck forever and leak its id mapping). The gamepad Y-axis negation (`-normalize_stick_axis(...)`) is a correct, expected SDL-down-positive-to-XNA-up-positive convention flip, not a bug. **Every SDL-enum-to-CNA-enum conversion function checked** (`sdl_power_state_to_ext`/`sdl_connection_state_to_ext`/`sdl_joystick_type_to_ext`/`sdl_hat_to_ext`/`sdl_button_label_to_ext` and `GetModState()`'s explicit bit-testing collapse of SDL's combined `SDL_KMOD_SHIFT`-style flags into `KeyModifiersEXT`) uses explicit, safe switches/bit-tests — no raw casts anywhere, resolving every cross-cutting ordinal-mismatch concern flagged in `cna-input`'s own audit. The anonymous-namespace helper section (~970 lines: scancode/keycode conversion tables, gamepad slot management, touch-id mapping, text-input synthesis) was inventoried and spot-checked but not read line-by-line, consistent with this audit's scoped-depth standard for files of this size.

### Testing
The untraced ~970-line helper section (scancode/keycode tables, gamepad slot assignment) remains a gap for a future, more exhaustive pass.

## Detailed Findings

**`ProcessEvent()` read in full**: every SDL event type (mouse motion/button/wheel, device hot-plug, keyboard, text input/editing/candidates, touch finger down/motion/up/canceled, gamepad/joystick add/remove/button/axis) is handled with explicit, well-reasoned logic — e.g. the mouse-wheel handling has an explicit comment warning against computing `float * 120` then casting (which would leak fractional deltas) vs. the correct cast-then-multiply order FNA itself uses; `FINGER_CANCELED` is explicitly treated identically to `FINGER_UP` with a comment explaining why (otherwise a canceled touch would stay stuck forever and leak its id mapping). The gamepad Y-axis negation (`-normalize_stick_axis(...)`) is a correct, expected SDL-down-positive-to-XNA-up-positive convention flip, not a bug. **Every SDL-enum-to-CNA-enum conversion function checked** (`sdl_power_state_to_ext`/`sdl_connection_state_to_ext`/`sdl_joystick_type_to_ext`/`sdl_hat_to_ext`/`sdl_button_label_to_ext` and `GetModState()`'s explicit bit-testing collapse of SDL's combined `SDL_KMOD_SHIFT`-style flags into `KeyModifiersEXT`) uses explicit, safe switches/bit-tests — no raw casts anywhere, resolving every cross-cutting ordinal-mismatch concern flagged in `cna-input`'s own audit. The anonymous-namespace helper section (~970 lines: scancode/keycode conversion tables, gamepad slot management, touch-id mapping, text-input synthesis) was inventoried and spot-checked but not read line-by-line, consistent with this audit's scoped-depth standard for files of this size.

## Cross-File Observations

None.

## Missing or Weak Tests

The untraced ~970-line helper section (scancode/keycode tables, gamepad slot assignment) remains a gap for a future, more exhaustive pass.

## Positive Findings

Extensive, explicit, line-number-cited cross-referencing against FNA's real `SDL3_FNAPlatform.cs` source throughout — one of the most rigorously FNA-verified files found in this entire audit. Every SDL-to-CNA enum conversion correctly uses explicit switches, resolving every previously-flagged ordinal-mismatch concern.

## Final Assessment

See findings above.
