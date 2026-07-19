# Audit: include/CNA/Input/InputDeviceInfo.hpp

## Metadata

- Source file: `include/CNA/Input/InputDeviceInfo.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares InputDeviceInfoEXT: identity (uint64 id + name) of one enumerated mouse/keyboard/touch device.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
`id`'s `uint64_t` width was verified as a deliberate, correct choice: real `SDL_MouseID`/`SDL_KeyboardID` are 32-bit but `SDL_TouchID` is 64-bit (confirmed via the real SDL3 headers) — this struct's field is widened to accommodate the largest of the 3 device-id types it's shared across.

### Testing
Has dedicated tests: `tests/CNA/Input/InputDevicesTests.cpp`/`InputDevicesHotplugTests.cpp`.

## Detailed Findings

`id`'s `uint64_t` width was verified as a deliberate, correct choice: real `SDL_MouseID`/`SDL_KeyboardID` are 32-bit but `SDL_TouchID` is 64-bit (confirmed via the real SDL3 headers) — this struct's field is widened to accommodate the largest of the 3 device-id types it's shared across.

## Cross-File Observations

None.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Input/InputDevicesTests.cpp`/`InputDevicesHotplugTests.cpp`.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
