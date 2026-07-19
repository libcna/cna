# Audit: tests/CNA/Devices/CameraTests.cpp

## Metadata
- Source file: `tests/CNA/Devices/CameraTests.cpp` (161 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Devices::Camera` (NOXNA extension, no FNA/XNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `Camera::getStateProperty`/`getFrameWidthProperty`/`getFrameHeightProperty`/`TryAcquireFrame`
via a fake backend injected through `Camera`'s constructor, avoiding a real camera device open.

## Executive Verdict
Correct and well-designed. `Camera` injects its fake backend via constructor argument (not a
process-wide static swap like `FileDialog`/`MessageBox`), so this class structurally cannot share
their confirmed mutex-scoping use-after-free bug — each `Camera` instance owns its own backend for
its own lifetime, with no shared global state to race on.

## Checklist Results
- `TryAcquireFrameReturnsFalseWhenTextureDimensionsDoNotMatch` (lines 132-143) and
  `TryAcquireFrameUploadsPixelsWhenReadyAndDimensionsMatch` (lines 145-159): both real, meaningful
  tests distinguishing the dimension-mismatch-rejection path from the successful-upload path, with
  the latter asserting the fake's call count to confirm the backend was actually invoked.
- `TryAcquireFrameReturnsFalseWhenBackendHasNoFrame`/`...WhenNotReady`: correctly test two distinct
  rejection reasons (no frame available vs. wrong state) separately, not conflated into one test.

## Detailed Findings
None.

## Cross-File Observations
Confirms, from the test-design angle, this project's own cross-cutting finding that
`SystemTray`/`Camera` "do NOT share" the `FileDialog`/`MessageBox` bug (per-instance
constructor-injected backends, not global swappable state) — see
`audit/AUDIT_CROSS_CUTTING_FINDINGS.md`'s "Recurring memory/resource risk patterns" section.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Constructor-injection design (vs. a global swap) sidesteps an entire class of bug by construction.

## Final Assessment
No findings.
