# Audit: include/CNA/Input/HapticDevice.hpp

## Metadata

- Source file: `include/CNA/Input/HapticDevice.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares HapticDevice: a move-only RAII wrapper around an opened SDL haptic handle, exposing the full effect create/update/run/stop/destroy lifecycle plus simple rumble.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Correct move-only RAII design (copy deleted, move transfers ownership); every method safely no-ops/default-returns when the handle is null (moved-from or failed-to-open), confirmed against the `.cpp` implementation.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

Correct move-only RAII design (copy deleted, move transfers ownership); every method safely no-ops/default-returns when the handle is null (moved-from or failed-to-open), confirmed against the `.cpp` implementation.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
