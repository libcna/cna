# Audit: src/CNA/Input/Power.cpp

## Metadata

- Source file: `src/CNA/Input/Power.cpp`
- Audit status: AUDITED
- Subsystem: `cna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A — all of `CNA::Input` is a NOXNA extension (raw joystick access, haptics,
  clipboard, sensors, power, multi-device enumeration); XNA 4.0 has no equivalent APIs for any of it
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Implements Power::GetInfoEXT via CNA::Internal::Input::system_power_backend(), including the SDL_PowerState-to-PowerStateEXT mapping.

## Executive Verdict

Healthy — a genuine positive finding given the ordinal mismatch discovered.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: `to_power_state_ext()` correctly uses an explicit, exhaustive switch statement, NOT a raw numeric cast** — this matters because `SDL_PowerState`'s real ordinals (`Error=-1, Unknown=0, OnBattery=1, NoBattery=2, Charging=3, Charged=4`, verified against the real SDL3 header) do NOT numerically align with `PowerStateEXT`'s own ordinals (`Error=0, Unknown=1, OnBattery=2, NoBattery=3, Charging=4, Charged=5` — no explicit values given, so 0-based sequential). A raw cast here would have silently misclassified every single power state. This file gets it right via an explicit switch; the same care should be verified for `JoystickCapabilitiesEXT::powerState`'s own population site (in `SdlInputBridge`, not yet audited) when `cna-internal-core`/`cna-devices` are reached.

### Testing
No dedicated GTest coverage found for this specific file's own public API surface.

## Detailed Findings

**Confirmed: `to_power_state_ext()` correctly uses an explicit, exhaustive switch statement, NOT a raw numeric cast** — this matters because `SDL_PowerState`'s real ordinals (`Error=-1, Unknown=0, OnBattery=1, NoBattery=2, Charging=3, Charged=4`, verified against the real SDL3 header) do NOT numerically align with `PowerStateEXT`'s own ordinals (`Error=0, Unknown=1, OnBattery=2, NoBattery=3, Charging=4, Charged=5` — no explicit values given, so 0-based sequential). A raw cast here would have silently misclassified every single power state. This file gets it right via an explicit switch; the same care should be verified for `JoystickCapabilitiesEXT::powerState`'s own population site (in `SdlInputBridge`, not yet audited) when `cna-internal-core`/`cna-devices` are reached.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own public API surface.

## Positive Findings

Correctly avoids a real, verified numeric-cast trap (SDL_PowerState's ordinals do not align with PowerStateEXT's own) via an explicit switch — a genuinely important correctness check that passes.

## Final Assessment

See findings above.
