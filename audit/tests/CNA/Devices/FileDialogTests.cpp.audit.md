# Audit: tests/CNA/Devices/FileDialogTests.cpp

## Metadata
- Source file: `tests/CNA/Devices/FileDialogTests.cpp` (228 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Devices::FileDialog` (NOXNA extension, no FNA/XNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `FileDialog::ShowOpenFile`/`ShowSaveFile`/`ShowOpenFile`/`getIsSupportedProperty` via a fake
backend injected through `SetBackendForTesting()`, avoiding ever launching a real, interactive OS
dialog (the file's own comment recounts a real prior incident: orphaned `zenity` processes left
running on a development machine).

## Executive Verdict
**MEDIUM finding, confirming a hypothesis from this project's own already-committed cross-cutting
findings**: this project's `AUDIT_CROSS_CUTTING_FINDINGS.md` ("Recurring memory/resource risk
patterns" section) documents a confirmed, real use-after-free bug in `FileDialog.cpp`'s
`GetBackend()` helper — the mutex protecting the swappable backend pointer is released before the
retrieved pointer is actually dereferenced, so a concurrent `SetBackendForTesting()` call between
retrieval and use can free the object out from under an in-flight call. This test file exercises
**only sequential, single-threaded usage** — every test installs a fake via
`ScopedFakeFileDialogBackend`'s constructor, makes calls, and restores the real backend in the
destructor, all on one thread, one call at a time. No test spawns a second thread to call
`SetBackendForTesting()` concurrently with an in-flight `ShowOpenFile`/`ShowSaveFile`/
`ShowOpenFolder` call. This confirms the hypothesis exactly: the existing test suite's coverage
shape is precisely why this real bug went uncaught by tests until this audit's code reading found
it.

## Checklist Results
- `ShowOpenFileForwardsParametersToBackendAndInvokesCallback`/`ShowSaveFile.../ShowOpenFolder...`
  (lines 131-199): each is a real, meaningful test — asserts the fake recorded the exact filters/
  default location/allow-multiple flag passed through, and that the callback was invoked with the
  simulated result. Not tautological.
- `EmptyResultMeansCanceledOrError` (lines 201-212): correctly tests the documented
  cancel-and-error-are-indistinguishable simplification, with an explicit comment citing
  `FileDialog`'s own doc comment for why.
- `SetBackendForTestingNullRestoresDefaultBackendBehavior` (lines 214-226): honestly scoped — its
  own comment explicitly states it "only proves `SetBackendForTesting(nullptr)` does not crash,"
  not that the real backend's dialog-launching behavior itself is exercised (which would require
  the real, interactive dialog this whole file is designed to avoid).
- `ScopedFakeFileDialogBackend`'s RAII pattern (restore-in-destructor) correctly prevents test
  pollution across the suite, given `FileDialog`'s backend is process-wide static state.

## Detailed Findings

### MEDIUM — No test exercises the confirmed concurrent-`SetBackendForTesting()` use-after-free scenario
See Executive Verdict. A regression test for the already-fixed-or-to-be-fixed bug would need to
launch a second thread that calls `FileDialog::SetBackendForTesting(...)` while a first thread is
mid-call inside `ShowOpenFile`'s callback-invoking body — genuinely difficult to write
deterministically (a real race requires precise timing), but even a best-effort
ThreadSanitizer-instrumented stress test (many iterations, tight loop) would likely have surfaced
the bug's TSAN-visible data race, the same way this project's `xna-audio`/`microsoft-devices`
shards' own test suites are cited as having caught similar races via TSAN elsewhere in this
codebase.

## Cross-File Observations
Identical situation confirmed in the sibling `MessageBoxTests.cpp` (audited separately) — both
`FileDialog` and `MessageBox` share the exact same "swappable global backend behind a mutex, tested
only sequentially" pattern and the exact same test-coverage gap.

## Missing or Weak Tests
A concurrent/TSAN-stress test for `SetBackendForTesting()` racing an in-flight dialog call, as
described above.

## Positive Findings
The fake-backend pattern itself (avoiding a real interactive dialog in CI) is well-designed and
explicitly justified by a real prior incident, not a hypothetical concern.

## Final Assessment
One MEDIUM finding: no test exercises the concurrent-backend-swap scenario the confirmed
`FileDialog`/`MessageBox` use-after-free bug requires — confirms why it went uncaught by testing.
