# Audit: src/CNA/Devices/FileDialog.cpp

## Metadata

- Source file: `src/CNA/Devices/FileDialog.cpp`
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

Implements FileDialog: platform-support check and delegation to a global, mutex-guarded, swappable Detail::IFileDialogBackend.

## Executive Verdict

Needs attention — CONFIRMED HIGH-severity use-after-free window in GetBackend()'s synchronization.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: `GetBackend()` releases its mutex before returning the raw backend pointer, and every public entry point (`ShowOpenFile`/`ShowSaveFile`/`ShowOpenFolder`) then dereferences that pointer (`GetBackend()->ShowOpenFile(...)`) AFTER the lock is gone.** If `SetBackendForTesting()` runs on another thread between the pointer's retrieval and its use, the previous backend object (owned by the `unique_ptr` that call just reassigned) is destroyed while still being called through — a genuine use-after-free, not merely a data race. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup (2 confirmed instances, `MessageBox.cpp` shares the identical pattern byte-for-byte).

### Testing
Has dedicated tests: `tests/CNA/Devices/FileDialogTests.cpp` — none exercise concurrent `SetBackendForTesting()` + dialog calls, the scenario that would reveal this bug.

## Detailed Findings

**Confirmed: `GetBackend()` releases its mutex before returning the raw backend pointer, and every public entry point (`ShowOpenFile`/`ShowSaveFile`/`ShowOpenFolder`) then dereferences that pointer (`GetBackend()->ShowOpenFile(...)`) AFTER the lock is gone.** If `SetBackendForTesting()` runs on another thread between the pointer's retrieval and its use, the previous backend object (owned by the `unique_ptr` that call just reassigned) is destroyed while still being called through — a genuine use-after-free, not merely a data race. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup (2 confirmed instances, `MessageBox.cpp` shares the identical pattern byte-for-byte).

## Cross-File Observations

Identical bug shape to `MessageBox.cpp` — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s dedicated "Recurring memory/resource risk patterns" section.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/FileDialogTests.cpp` — none exercise concurrent `SetBackendForTesting()` + dialog calls, the scenario that would reveal this bug.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
