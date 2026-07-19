# Audit: include/CNA/Input/JoystickCapabilities.hpp

## Metadata

- Source file: `include/CNA/Input/JoystickCapabilities.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares JoystickCapabilitiesEXT: the static hardware shape and identity of a raw joystick (axis/button/hat/ball counts, type, name, GUID, battery state).

## Executive Verdict

Needs attention — 1 cross-file verification note, not a defect in this file itself.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Complete, correct struct with an exhaustive equality operator. **Cross-file note**: `powerState`'s `PowerStateEXT` values have different numeric ordinals than SDL3's own `SDL_PowerState` (SDL starts at -1 for `Error`; `PowerStateEXT` starts at 0) — confirmed this shard's own `Power.cpp` maps between them via an explicit, safe switch, but this struct's own `powerState` field is populated elsewhere (likely `SdlInputBridge`, in the not-yet-audited `cna-internal-core`/`cna-devices` shards) — worth verifying that consumer also uses an explicit switch, not a raw numeric cast, when that shard is reached.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

Complete, correct struct with an exhaustive equality operator. **Cross-file note**: `powerState`'s `PowerStateEXT` values have different numeric ordinals than SDL3's own `SDL_PowerState` (SDL starts at -1 for `Error`; `PowerStateEXT` starts at 0) — confirmed this shard's own `Power.cpp` maps between them via an explicit, safe switch, but this struct's own `powerState` field is populated elsewhere (likely `SdlInputBridge`, in the not-yet-audited `cna-internal-core`/`cna-devices` shards) — worth verifying that consumer also uses an explicit switch, not a raw numeric cast, when that shard is reached.

## Cross-File Observations

See `PowerState.hpp`'s own report for the full ordinal-mismatch detail and the verified-safe `Power.cpp` consumer within this shard.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
