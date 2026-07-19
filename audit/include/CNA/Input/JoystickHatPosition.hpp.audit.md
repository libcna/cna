# Audit: include/CNA/Input/JoystickHatPosition.hpp

## Metadata

- Source file: `include/CNA/Input/JoystickHatPosition.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares JoystickHatPositionEXT: the 9 reachable positions of a joystick's POV hat, mirroring SDL3's SDL_HAT_* bit-combination values as an enumerated set.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Complete (Centered + 4 cardinal + 4 diagonal = 9), matching SDL's own reachable hat-position combinations.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

Complete (Centered + 4 cardinal + 4 diagonal = 9), matching SDL's own reachable hat-position combinations.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
