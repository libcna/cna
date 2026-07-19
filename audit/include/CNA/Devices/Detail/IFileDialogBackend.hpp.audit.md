# Audit: include/CNA/Devices/Detail/IFileDialogBackend.hpp

## Metadata

- Source file: `include/CNA/Devices/Detail/IFileDialogBackend.hpp`
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

Declares IFileDialogBackend (the swappable-backend interface FileDialog calls through) and FileDialogResultCallback.

## Executive Verdict

Healthy — the interface design itself is sound; see FileDialog.cpp's own report for the confirmed bug in its consumer's synchronization.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Candidly documents a real historical incident that motivated this interface: calling the real backend from an automated test once left orphaned `zenity` processes running on a real developer desktop during this class's own development.

### Testing
No dedicated test needed (declaration-only) or covered indirectly.

## Detailed Findings

Candidly documents a real historical incident that motivated this interface: calling the real backend from an automated test once left orphaned `zenity` processes running on a real developer desktop during this class's own development.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated test needed (declaration-only) or covered indirectly.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
