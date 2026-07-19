# Audit: include/CNA/Input/HapticFeature.hpp

## Metadata

- Source file: `include/CNA/Input/HapticFeature.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares HapticFeatureEXT: a bit-flag set of force-feedback effect families and global capabilities a device supports, mirroring SDL3's SDL_HapticFeatures bitmask exactly.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Every one of the 17 explicit bit values was independently verified against the real SDL3 header** (`SDL_haptic.h`) — exact match, including the intentional gaps in the bit sequence (bits 12-14 skipped between `LeftRight`=bit-11 and `Custom`=bit-15). This numeric identity is what makes `HapticDevice.cpp`'s `to_haptic_features_ext()` direct-cast shortcut (rather than an explicit bit-by-bit translation) genuinely safe.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

**Every one of the 17 explicit bit values was independently verified against the real SDL3 header** (`SDL_haptic.h`) — exact match, including the intentional gaps in the bit sequence (bits 12-14 skipped between `LeftRight`=bit-11 and `Custom`=bit-15). This numeric identity is what makes `HapticDevice.cpp`'s `to_haptic_features_ext()` direct-cast shortcut (rather than an explicit bit-by-bit translation) genuinely safe.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Verified bit-for-bit correct against the real SDL3 header, including 2 intentional gaps in the bit sequence — the numeric-identity claim this file's own comment makes is independently confirmed true.

## Final Assessment

See findings above.
