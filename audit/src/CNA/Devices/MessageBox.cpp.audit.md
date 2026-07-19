# Audit: src/CNA/Devices/MessageBox.cpp

## Metadata

- Source file: `src/CNA/Devices/MessageBox.cpp`
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

Implements MessageBox: delegation to a global, mutex-guarded, swappable Detail::IMessageBoxBackend.

## Executive Verdict

Needs attention — CONFIRMED HIGH-severity use-after-free window, byte-for-byte identical to FileDialog.cpp's own.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: this file's `GetBackend()`/`SetBackendForTesting()` implementation is structurally identical to `FileDialog.cpp`'s own confirmed use-after-free-window bug** — the mutex is released before the returned raw pointer is dereferenced by `ShowSimple`/`Show`. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup.

### Testing
Has dedicated tests: `tests/CNA/Devices/MessageBoxTests.cpp` — none exercise concurrent `SetBackendForTesting()` + dialog calls, the scenario that would reveal this bug.

## Detailed Findings

**Confirmed: this file's `GetBackend()`/`SetBackendForTesting()` implementation is structurally identical to `FileDialog.cpp`'s own confirmed use-after-free-window bug** — the mutex is released before the returned raw pointer is dereferenced by `ShowSimple`/`Show`. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup.

## Cross-File Observations

Identical bug shape to `FileDialog.cpp` — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s dedicated "Recurring memory/resource risk patterns" section.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/MessageBoxTests.cpp` — none exercise concurrent `SetBackendForTesting()` + dialog calls, the scenario that would reveal this bug.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
