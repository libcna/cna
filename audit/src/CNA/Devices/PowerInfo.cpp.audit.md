# Audit: src/CNA/Devices/PowerInfo.cpp

## Metadata

- Source file: `src/CNA/Devices/PowerInfo.cpp`
- Audit status: AUDITED
- Subsystem: `cna-devices` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A — all of `CNA::Devices` is a NOXNA extension gated behind the `CNA_DEVICES` CMake
  option (default OFF), independent of `CNA_NOXNA`; XNA 4.0/WP7 has no equivalent for any of this shard's
  features (camera, file dialogs, message boxes, system tray, locale, power, system info, URL launching,
  display info, clipboard)
- Graphics backend relevance: none directly (device/OS-integration subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Implements PowerInfo via SDL_GetPowerInfo, including the SDL_PowerState-to-PowerState conversion.

## Executive Verdict

Healthy in isolation — a genuine positive finding given the ordinal mismatch discovered.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: `ConvertSdlPowerState()` correctly uses an explicit, exhaustive switch, not a raw numeric cast** — this matters because, like `CNA::Input::PowerStateEXT`, this file's own `CNA::Devices::PowerState` enum's ordinals do NOT numerically align with real `SDL_PowerState`'s own ordinals (both CNA enums are 0-based sequential; SDL's own starts at -1 for Error). This is the 2nd independent confirmation (after `CNA::Input::Power.cpp`) that this specific SDL-to-CNA conversion is done safely — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s updated writeup.

### Testing
Has dedicated tests: `tests/CNA/Devices/PowerInfoTests.cpp`.

## Detailed Findings

**Confirmed: `ConvertSdlPowerState()` correctly uses an explicit, exhaustive switch, not a raw numeric cast** — this matters because, like `CNA::Input::PowerStateEXT`, this file's own `CNA::Devices::PowerState` enum's ordinals do NOT numerically align with real `SDL_PowerState`'s own ordinals (both CNA enums are 0-based sequential; SDL's own starts at -1 for Error). This is the 2nd independent confirmation (after `CNA::Input::Power.cpp`) that this specific SDL-to-CNA conversion is done safely — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s updated writeup.

## Cross-File Observations

None.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/PowerInfoTests.cpp`.

## Positive Findings

2nd independent confirmation that this codebase correctly avoids the PowerState ordinal-mismatch trap via an explicit switch, not a risky cast.

## Final Assessment

See findings above.
