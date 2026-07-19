# Audit: include/CNA/Devices/PowerState.hpp

## Metadata

- Source file: `include/CNA/Devices/PowerState.hpp`
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

Declares CNA::Devices::PowerState: battery/charge state, mirroring SDL3's SDL_PowerState.

## Executive Verdict

Needs attention — confirmed duplicate of CNA::Input::PowerStateEXT (same 6 values, same order), with the same ordinal mismatch against real SDL_PowerState.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: byte-for-byte the same 6 enumerators, in the same order, as `CNA::Input::PowerStateEXT`** (`Error, Unknown, OnBattery, NoBattery, Charging, Charged`) — a duplicate enum definition across 2 namespaces. Also shares the same numeric-ordinal mismatch against real `SDL_PowerState` already recorded for `CNA::Input::PowerStateEXT` (SDL: `Error=-1`...; this enum: 0-based sequential) — confirmed SAFE in this shard's own consumer (`PowerInfo.cpp`'s explicit switch).

### Testing
No dedicated test needed (declaration-only) or covered indirectly.

## Detailed Findings

**Confirmed: byte-for-byte the same 6 enumerators, in the same order, as `CNA::Input::PowerStateEXT`** (`Error, Unknown, OnBattery, NoBattery, Charging, Charged`) — a duplicate enum definition across 2 namespaces. Also shares the same numeric-ordinal mismatch against real `SDL_PowerState` already recorded for `CNA::Input::PowerStateEXT` (SDL: `Error=-1`...; this enum: 0-based sequential) — confirmed SAFE in this shard's own consumer (`PowerInfo.cpp`'s explicit switch).

## Cross-File Observations

Duplicate of `include/CNA/Input/PowerState.hpp` (`cna-input` shard) — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s dedicated section on this pattern.

## Missing or Weak Tests

No dedicated test needed (declaration-only) or covered indirectly.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
