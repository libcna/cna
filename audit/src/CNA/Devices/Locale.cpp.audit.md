# Audit: src/CNA/Devices/Locale.cpp

## Metadata

- Source file: `src/CNA/Devices/Locale.cpp`
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

Implements Locale::getPreferredLocalesProperty via SDL_GetPreferredLocales.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Correctly frees the single heap block `SDL_GetPreferredLocales()` returns (which contains both the pointer array and the pointed-to `SDL_Locale` structs) with one `SDL_free()` call on the outer array, matching SDL3's own documented single-allocation contract — not a leak or a double-free.

### Testing
Has dedicated tests: `tests/CNA/Devices/LocaleTests.cpp`.

## Detailed Findings

Correctly frees the single heap block `SDL_GetPreferredLocales()` returns (which contains both the pointer array and the pointed-to `SDL_Locale` structs) with one `SDL_free()` call on the outer array, matching SDL3's own documented single-allocation contract — not a leak or a double-free.

## Cross-File Observations

None.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/LocaleTests.cpp`.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
