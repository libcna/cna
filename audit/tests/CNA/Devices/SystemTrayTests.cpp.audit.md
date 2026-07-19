# Audit: tests/CNA/Devices/SystemTrayTests.cpp

## Metadata
- Source file: `tests/CNA/Devices/SystemTrayTests.cpp` (232 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Devices::SystemTray` (NOXNA extension, no FNA/XNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `SystemTray`'s constructor/destructor backend forwarding, tooltip/entry management, and
click-callback dispatch via a fake backend injected through the constructor.

## Executive Verdict
Correct, and notable for a genuinely well-documented, ASan-confirmed use-after-free lesson.
`FakeTrayBackend::DestroyedFlag`'s doc comment (lines 33-40) explicitly states: "reading
`DestroyCalled` through a dangling raw pointer afterward is a real use-after-free (caught by ASan
during this task's own development, not hypothetical)." `DestructorCallsDestroyOnTheInjectedFakeBackend`
(lines 141-160) correctly uses an externally-owned `shared_ptr<bool>` flag instead of reading the
(by-then-destroyed) fake backend's own member after `tray` goes out of scope — a real, previously
fixed test-authoring bug, not a hypothetical concern.

## Checklist Results
- `AddEntryForwardsParametersAndReturnsIncrementingIndices` (lines 174-190) correctly verifies both
  the returned index sequence and the forwarded per-entry flags (checkable/checked).
- `EntryClickCallbackFiresThroughTheBackend` (lines 192-206) correctly verifies the callback
  closure itself is invokable and fires the expected side effect, not just that it was stored.
- `SetAndGetEntryCheckedRoundTrip`/`SetAndGetEntryEnabledRoundTrip`: real round-trip tests, not
  tautological.

## Detailed Findings
None.

## Cross-File Observations
`SystemTray`'s constructor-injection design (like `Camera`'s) confirms this project's own
cross-cutting finding that `SystemTray`/`Camera` do not share the `FileDialog`/`MessageBox`
mutex-scoping use-after-free bug (per-instance owned backend, not global swappable state) — see
`audit/AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `DestroyedFlag` pattern is an excellent, concrete example of a test author correctly
recognizing and fixing a real use-after-free in their own test code (confirmed via ASan) rather
than leaving a subtly-broken test that happens to pass.

## Final Assessment
No findings.
