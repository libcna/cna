# Audit: include/CNA/Input/InputDevices.hpp

## Metadata

- Source file: `include/CNA/Input/InputDevices.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares InputDevices: static-only enumeration of connected mice/keyboards/touch devices plus hot-plug events.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
`MouseConnectedEXT`/`KeyboardConnectedEXT` etc. correctly use `uint32_t` (narrower than `InputDeviceInfoEXT::id`'s `uint64_t`) — verified consistent with real `SDL_MouseID`/`SDL_KeyboardID` both being 32-bit types.

### Testing
Has dedicated tests: `tests/CNA/Input/InputDevicesTests.cpp`/`InputDevicesHotplugTests.cpp`.

## Detailed Findings

`MouseConnectedEXT`/`KeyboardConnectedEXT` etc. correctly use `uint32_t` (narrower than `InputDeviceInfoEXT::id`'s `uint64_t`) — verified consistent with real `SDL_MouseID`/`SDL_KeyboardID` both being 32-bit types.

## Cross-File Observations

None.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Input/InputDevicesTests.cpp`/`InputDevicesHotplugTests.cpp`.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
