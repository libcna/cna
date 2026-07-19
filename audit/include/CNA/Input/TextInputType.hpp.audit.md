# Audit: include/CNA/Input/TextInputType.hpp

## Metadata

- Source file: `include/CNA/Input/TextInputType.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares TextInputTypeEXT: a hint describing the kind of text being entered (for on-screen-keyboard/IME layout selection), mirroring SDL3's SDL_TextInputType.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Verified exact match against the real SDL3 header** (`SDL_keyboard.h`): all 9 values, same order, no negative sentinel in either enum (unlike PowerState/SensorType) — a clean, genuinely 1:1-safe mirror.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

**Verified exact match against the real SDL3 header** (`SDL_keyboard.h`): all 9 values, same order, no negative sentinel in either enum (unlike PowerState/SensorType) — a clean, genuinely 1:1-safe mirror.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
