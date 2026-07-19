# Audit: include/CNA/Devices/LocaleInfo.hpp

## Metadata

- Source file: `include/CNA/Devices/LocaleInfo.hpp`
- Audit status: AUDITED
- Subsystem: `cna-devices` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Devices` is a NOXNA extension gated behind the `CNA_DEVICES` CMake
  option (default OFF), independent of `CNA_NOXNA`; XNA 4.0/WP7 has no equivalent for any of this shard's
  features (camera, file dialogs, message boxes, system tray, locale, power, system info, URL launching,
  display info, clipboard)
- Graphics backend relevance: none directly (device/OS-integration subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares LocaleInfo: one user-preferred locale (ISO-639 language + optional ISO-3166 country), mirroring SDL3's SDL_Locale.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Minimal, correct struct with accurate ISO-code documentation.

### Testing
No dedicated test needed (declaration-only) or covered indirectly.

## Detailed Findings

Minimal, correct struct with accurate ISO-code documentation.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated test needed (declaration-only) or covered indirectly.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
