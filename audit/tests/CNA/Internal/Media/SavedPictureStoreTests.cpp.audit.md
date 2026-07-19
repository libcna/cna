# Audit: tests/CNA/Internal/Media/SavedPictureStoreTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Media/SavedPictureStoreTests.cpp` (114 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Media::SavedPictureStore` (backs
  `Microsoft::Xna::Framework::Media::MediaLibrary::SavePicture`; CNA-internal, no direct FNA
  equivalent)
- Main related tests: uses the real `ImageLoader` (already audited elsewhere in this session)

## Purpose
Tests saved-picture directory auto-creation, real end-to-end PNG file writing/readback, and —most
significantly— a security-hardening test suite against path-traversal attacks via the
caller-supplied picture name.

## Executive Verdict
Excellent, security-conscious test file. `SavePictureRejectsPathTraversalInName`'s own comment
explicitly states the vulnerability was "found by external code review, not by inspection" — an
honest disclosure of provenance that gives this reviewer confidence the test corresponds to a real,
previously-exploitable defect that was actually fixed, not merely a defensive test written on
spec without a known failure mode.

## Checklist Results
- `SavePictureRejectsPathTraversalInName`/`...RejectsAbsolutePathInName`/
  `...RejectsWindowsStyleBackslashTraversalInName` cover three genuinely distinct traversal vectors
  (relative `../` traversal, an absolute path, and Windows-style `..\` backslash traversal despite
  running on a POSIX system) — testing the Windows-style backslash case on a Linux CI runner is a
  notably careful choice, since a naive implementation might only sanitize the platform-native
  separator. All three correctly verify the file lands inside the real Saved Pictures directory
  (via `lexically_normal()` path comparison) AND that the attacker-intended destination
  (`/tmp/evil.png`, `/etc/cron.d/evil.png`) does NOT exist afterward — verifying both the positive
  (correct containment) and negative (attack didn't land) outcome.
- `SavePictureFallsBackToASafeNameForDotOrDotDot` correctly tests the `".."` bare-traversal-token
  edge case separately from the multi-segment traversal tests, confirming a safe fallback filename
  (`"picture"`) is used rather than silently failing or writing to an unexpected location.
- `SavePictureWritesARealReadableFile` (MEDIA-59) reuses a real fixture PNG's actual bytes (rather
  than synthetic/hand-crafted data) and verifies the round-tripped file decodes to the correct
  dimensions via the real `ImageLoader` — a genuine end-to-end correctness proof, not just "a file
  was created."
- `SavePictureFailsGracefullyWithEmptyPicturesRoot` correctly tests the degenerate empty-root input
  case.
- `GetSavedPicturesDirectoryCreatesItIfMissing` verifies both directory auto-creation AND the
  specific expected directory name (`"Saved Pictures"`), not just "some directory exists."

## Detailed Findings
None — this is a genuinely strong test file for a security-sensitive code path.

## Cross-File Observations
None beyond general consistency with this shard's real-fixture-based media testing approach.

## Missing or Weak Tests
None identified — the path-traversal coverage is unusually complete for a test suite of this size.

## Positive Findings
The three-vector path-traversal test suite (relative, absolute, Windows-style-backslash-on-POSIX),
explicitly disclosed as originating from an external code-review finding, is one of the strongest
security-regression test sets found in this entire audit.

## Final Assessment
No findings.
