# Audit: tests/Microsoft/Xna/Framework/Content/CnjCacheIsolationTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/CnjCacheIsolationTests.cpp` (133 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `.cnj` `Texture2D` sidecar `colorKey` isolation from the shared weak
  texture cache (NOXNA content pipeline extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Pins the invariant that a `.cnj` sidecar's `colorKey` metadata transform never leaks into a
separately-cached, separately-requested load of the same underlying native texture file, in either
load order.

## Executive Verdict
Correct, and a genuinely valuable regression guard with an unusually honest top comment: it
explicitly states that the investigated failure mechanism (`ApplyColorKey`'s `SetData()` mutating a
shared cached texture backend in place) "does not currently reproduce," because
`Texture2D::SetData(const Color*, int)` reassigns `backend_`/`cpuPixels_` to freshly created
targets rather than mutating in place — and that these tests exist specifically to catch a
*regression* if that ever changes. This is a mature testing philosophy: pinning a currently-safe
invariant as a guard against future changes, not just testing today's happy path.

## Checklist Results
`NativeLoadedFirstWithLiveHandleUnaffectedBySidecar`'s own comment (lines 119-121) correctly
explains why a live handle must be kept: without it, the weak cache's entry could expire before the
sidecar recursively loads it, silently skipping the actual cache-reuse code path this test intends
to exercise. This is a real, non-obvious test-design correctness point, correctly reasoned.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The "pin a currently-safe invariant as a regression guard against a specific investigated failure
mode" philosophy, combined with the live-handle-required insight for actually exercising the
weak-cache-reuse path, reflects careful, non-superficial test engineering.

## Final Assessment
No findings.
