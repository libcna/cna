# Audit: tests/CNA/Devices/MessageBoxTests.cpp

## Metadata
- Source file: `tests/CNA/Devices/MessageBoxTests.cpp` (143 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Devices::MessageBox` (NOXNA extension, no FNA/XNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `MessageBox::ShowSimple`/`Show`/`getIsSupportedProperty` via a fake backend injected through
`SetBackendForTesting()`, avoiding a real, interactive, modal OS dialog (the file's own comment
cites "the same class of incident already hit once during `FileDialog`'s own development").

## Executive Verdict
**MEDIUM finding, identical shape to the sibling `FileDialogTests.cpp`**: this file exercises only
sequential, single-threaded backend-swap usage, never the concurrent `SetBackendForTesting()`-
during-an-in-flight-call scenario the already-confirmed `FileDialog.cpp`/`MessageBox.cpp`
mutex-scoping use-after-free bug requires (see `audit/AUDIT_CROSS_CUTTING_FINDINGS.md`, "Recurring
memory/resource risk patterns"). Same conclusion as `FileDialogTests.cpp`: the test suite's
sequential-only coverage shape is why this real bug went uncaught until static code reading found
it.

## Checklist Results
- `ShowSimpleForwardsParametersToBackend` (lines 101-111) and
  `ShowForwardsParametersToBackendAndReturnsClickedIndex` (lines 113-127): both real, meaningful
  tests asserting the fake recorded exact parameters and (for `Show`) that the simulated button
  index was correctly returned.
- `GetIsSupportedPropertyIsAlwaysTrue` (line 96-99): a real assertion, though the name
  ("AlwaysTrue") is a slightly stronger claim than what's verifiable in a single-platform test run
  — reasonable given `MessageBox`'s documented contract (unlike `FileDialog`, which has a genuinely
  conditional `IsSupported`).
- `SetBackendForTestingNullRestoresDefaultBackendBehavior` (lines 129-140): honestly scoped, same
  pattern as `FileDialogTests.cpp`'s equivalent test — explicitly notes it only proves no crash, not
  that the real backend's dialog behavior is exercised.

## Detailed Findings

### MEDIUM — No test exercises the confirmed concurrent-`SetBackendForTesting()` use-after-free scenario
Identical finding to `FileDialogTests.cpp` — see that report for the full analysis. This is the
second of two test files sharing the exact same coverage gap for the exact same underlying
production bug class.

## Cross-File Observations
See `FileDialogTests.cpp.audit.md` — both files share an identical structural pattern (fake
backend + RAII restore + sequential-only tests) and the identical coverage gap.

## Missing or Weak Tests
A concurrent/TSAN-stress test for `SetBackendForTesting()` racing an in-flight `Show`/`ShowSimple`
call.

## Positive Findings
The fake-backend pattern, explicitly modeled on `FileDialogTests.cpp`'s own established
convention (per this file's own comment), is consistently and correctly applied.

## Final Assessment
One MEDIUM finding: no test exercises the concurrent-backend-swap scenario the confirmed
`FileDialog`/`MessageBox` use-after-free bug requires — the second of two test files confirming
this same gap.
