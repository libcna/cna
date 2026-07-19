# Audit: include/CNA/Devices/MessageBox.hpp

## Metadata

- Source file: `include/CNA/Devices/MessageBox.hpp`
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

Declares MessageBox: native, modal, synchronous message/alert dialogs, via a swappable Detail::IMessageBoxBackend.

## Executive Verdict

Needs attention — HIGH-severity confirmed defect in the paired .cpp's synchronization, not in this header itself.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Excellent, evidence-based platform documentation (confirmed real backends exist for every video backend this project vendors, by reading `third_party/SDL/src/video/` directly — the broadest cross-platform reach of any `CNA::Devices` capability). The header's own API design is sound; the confirmed use-after-free-window bug lives entirely in the `.cpp`'s `GetBackend()`/`SetBackendForTesting()` synchronization — see that file's own report and `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### Testing
Has dedicated tests: `tests/CNA/Devices/MessageBoxTests.cpp`.

## Detailed Findings

Excellent, evidence-based platform documentation (confirmed real backends exist for every video backend this project vendors, by reading `third_party/SDL/src/video/` directly — the broadest cross-platform reach of any `CNA::Devices` capability). The header's own API design is sound; the confirmed use-after-free-window bug lives entirely in the `.cpp`'s `GetBackend()`/`SetBackendForTesting()` synchronization — see that file's own report and `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

None.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/MessageBoxTests.cpp`.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
