# Audit: include/CNA/Input/PowerState.hpp

## Metadata

- Source file: `include/CNA/Input/PowerState.hpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares PowerStateEXT: battery/charge state shared by GamePad::GetPowerInfoEXT and CNA::Input::Power, mirroring SDL3's SDL_PowerState.

## Executive Verdict

Needs attention — confirmed numeric-ordinal mismatch against the real SDL enum (informational, not a bug in this file itself, since no explicit numeric values are claimed or needed here).

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: this enum's ordinals do NOT numerically align with real `SDL_PowerState`'s own ordinals** — SDL's real enum starts at `-1` (`SDL_POWERSTATE_ERROR = -1`, then `Unknown=0, OnBattery=1, NoBattery=2, Charging=3, Charged=4`), while `PowerStateEXT` (no explicit values) is 0-based sequential (`Error=0, Unknown=1, OnBattery=2, NoBattery=3, Charging=4, Charged=5`) — every value is offset by exactly +1 from SDL's own, and SDL's own `Error` case is negative. This file's own doc comment doesn't claim numeric identity (unlike `HapticFeatureEXT`'s explicit claim), so this by itself isn't a documentation-accuracy defect — but it DOES mean any consumer that maps between the two via a raw cast instead of an explicit switch would be silently wrong. Confirmed SAFE in this shard's own consumer (`Power.cpp`'s `to_power_state_ext()`, an explicit switch) — the `JoystickCapabilitiesEXT::powerState` population site (elsewhere, not yet audited) should be checked for the same care.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

**Confirmed: this enum's ordinals do NOT numerically align with real `SDL_PowerState`'s own ordinals** — SDL's real enum starts at `-1` (`SDL_POWERSTATE_ERROR = -1`, then `Unknown=0, OnBattery=1, NoBattery=2, Charging=3, Charged=4`), while `PowerStateEXT` (no explicit values) is 0-based sequential (`Error=0, Unknown=1, OnBattery=2, NoBattery=3, Charging=4, Charged=5`) — every value is offset by exactly +1 from SDL's own, and SDL's own `Error` case is negative. This file's own doc comment doesn't claim numeric identity (unlike `HapticFeatureEXT`'s explicit claim), so this by itself isn't a documentation-accuracy defect — but it DOES mean any consumer that maps between the two via a raw cast instead of an explicit switch would be silently wrong. Confirmed SAFE in this shard's own consumer (`Power.cpp`'s `to_power_state_ext()`, an explicit switch) — the `JoystickCapabilitiesEXT::powerState` population site (elsewhere, not yet audited) should be checked for the same care.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Clean, correct, well-documented NOXNA extension type.

## Final Assessment

See findings above.
