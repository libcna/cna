# Audit: include/CNA/Devices/SystemTray.hpp

## Metadata

- Source file: `include/CNA/Devices/SystemTray.hpp`
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

Declares SystemTray: a system tray icon with a flat menu, via a constructor-injected Detail::ITrayBackend.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Correctly uses per-instance constructor-injection (not a global swappable-backend pattern), the same safe design as `Camera` — confirmed structurally immune to the `FileDialog`/`MessageBox` use-after-free bug family found elsewhere in this shard.

### Testing
Has dedicated tests: `tests/CNA/Devices/SystemTrayTests.cpp`.

## Detailed Findings

Correctly uses per-instance constructor-injection (not a global swappable-backend pattern), the same safe design as `Camera` — confirmed structurally immune to the `FileDialog`/`MessageBox` use-after-free bug family found elsewhere in this shard.

## Cross-File Observations

None.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/SystemTrayTests.cpp`.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
