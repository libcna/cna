# Audit: include/CNA/Input/Joysticks.hpp

## Metadata

- Source file: `include/CNA/Input/Joysticks.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares Joysticks: static-only raw joystick enumeration, capability/state query, and hot-plug events.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Clean, well-documented API distinguishing raw/unmapped joystick access from XNA's own `GamePad`.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

Clean, well-documented API distinguishing raw/unmapped joystick access from XNA's own `GamePad`.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
