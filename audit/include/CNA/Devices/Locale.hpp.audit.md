# Audit: include/CNA/Devices/Locale.hpp

## Metadata

- Source file: `include/CNA/Devices/Locale.hpp`
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

Declares Locale: the user's preferred locale(s), backed by SDL3's SDL_GetPreferredLocales.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Clean, correctly documents multi-locale and empty-result possibilities.

### Testing
Has dedicated tests: `tests/CNA/Devices/LocaleTests.cpp`.

## Detailed Findings

Clean, correctly documents multi-locale and empty-result possibilities.

## Cross-File Observations

None.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/LocaleTests.cpp`.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
