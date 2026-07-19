# Audit: tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/MouseGlobalTests.cpp` (97 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::Mouse::SetCaptureEXT`/
  `GetGlobalPositionEXT`/`WarpGlobalEXT` (NOXNA)
- Main related tests: N/A (this IS a test file)

## Purpose
Covers the NOXNA global-mouse extension API (`SetCaptureEXT`, `GetGlobalPositionEXT`,
`WarpGlobalEXT`) via a fake `ISystemMouseBackend` injected for deterministic testing (CI has no
real desktop cursor to read/warp).

## Executive Verdict
No findings. Clean dependency-injection design (`FakeSystemMouseBackend`/
`SetSystemMouseBackendForTests`) mirroring `KeyboardModStateTests.cpp`'s equivalent pattern for the
keyboard side.

## Checklist Results
- `SetCaptureForwardsFlagAndReturnsBackendResult` correctly tests both a true and false backend
  result being forwarded through, not just the call-forwarding itself.
- `GetGlobalPositionReadsBackendAndTruncatesToInt` correctly tests truncation-toward-zero for a
  negative float (`-12.5f` -> `-12`, not `-13`), the case most likely to expose a
  floor-vs-truncate bug.
- `WarpGlobalForwardsCoordinatesAndReturnsBackendResult` verifies both the forwarded coordinate
  values and both possible backend result values.

## Detailed Findings
None.

## Cross-File Observations
Same fake-backend dependency-injection pattern as `KeyboardModStateTests.cpp` — a consistent,
reusable testing seam for hardware-backed NOXNA extensions across this shard.

## Missing or Weak Tests
None identified for this NOXNA API's surface.

## Positive Findings
The negative-float truncation check is a well-chosen edge case for catching a `floor()`-vs-`trunc()`
class of bug.

## Final Assessment
No findings.
