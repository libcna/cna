# Audit: include/CNA/Devices/Detail/ICameraBackend.hpp

## Metadata

- Source file: `include/CNA/Devices/Detail/ICameraBackend.hpp`
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

Declares ICameraBackend (the swappable-backend interface Camera calls through) and CameraFrame (one captured frame's dimensions + tightly-packed RGBA8 pixels).

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Clean interface; the constructor-injection rationale (vs. `FileDialog`'s post-construction `SetBackendForTesting()`) is explicitly and correctly documented — the real backend's device-opening side effect runs at construction time, so a fake must already be in place before that point.

### Testing
No dedicated test needed (declaration-only) or covered indirectly.

## Detailed Findings

Clean interface; the constructor-injection rationale (vs. `FileDialog`'s post-construction `SetBackendForTesting()`) is explicitly and correctly documented — the real backend's device-opening side effect runs at construction time, so a fake must already be in place before that point.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated test needed (declaration-only) or covered indirectly.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
