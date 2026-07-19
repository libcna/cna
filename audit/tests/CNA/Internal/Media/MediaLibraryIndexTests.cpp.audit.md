# Audit: tests/CNA/Internal/Media/MediaLibraryIndexTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Media/MediaLibraryIndexTests.cpp` (172 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Media::MediaLibraryIndex` (backs
  `Microsoft::Xna::Framework::Media::MediaLibrary`/`SongCollection`, a CNA-internal filesystem
  scanner — no direct FNA equivalent since desktop FNA typically has no real media-library scan)
- Main related tests: consumes `AudioTagParser` (tested separately in `AudioTagParserTests.cpp`,
  same batch)

## Purpose
Tests the recursive music-library filesystem scanner: complete-corpus scanning, per-song tag
correctness, cross-file artist-name case normalization, empty/missing-root handling, symlink-cycle
termination, and permission-denied subdirectory skipping.

## Executive Verdict
Correct and genuinely careful about real-filesystem hazards — `ScansEveryFixtureSong` computes its
expected count by walking the filesystem itself (rather than a hardcoded magic number), explicitly
noting in its own comment that a hardcoded count had already broken once when the fixture corpus
grew, which is a real, self-correcting test-maintenance choice rather than a guess.

## Checklist Results
- `NormalizesCaseVariantArtistNamesToOneCanonicalValue` (MEDIA-54/D10) tests a genuinely subtle
  correctness property: "Artist One" and its deliberately-tagged case variant "ARTIST ONE" (in a
  different file, confirmed set up in `AudioTagParserTests.cpp`'s own raw-tag-read test) must
  collapse to ONE canonical artist across the whole library, with a specific, documented tie-break
  rule (first-seen casing wins, verified against the real scan order) — this is real, precise
  behavior-pinning, not just "does normalization happen at all."
- `TerminatesOnASelfReferentialSymlinkCycle` (MEDIA-53) is a genuinely important hardening test: a
  self-referential symlink is a classic infinite-recursion hazard for any recursive directory walker,
  and the test's own comment correctly notes the real assertion is implicit — a regression here
  would hang the whole test binary, not just fail an assertion, which is exactly the failure mode
  such a test needs to catch. It also gracefully `GTEST_SKIP()`s if symlink creation itself fails in
  a sandboxed environment, rather than falsely failing.
- `SkipsAnUnreadableSubdirectoryWithoutCrashing` (MEDIA-53/113) is an unusually thorough permission
  test: it creates a REAL locked directory via actual Unix permission bits (not a mock or a
  simulated error), confirms the readable sibling is still scanned while the locked one is silently
  skipped, and correctly detects and skips itself when running as root (since root bypasses Unix
  permission bits, making the scenario genuinely unexercisable there) — careful, environment-aware
  test design. It also correctly restores permissions before cleanup so `remove_all` can recurse
  into the directory it locked.
- `EmptyOrMissingRootProducesNoSongsWithoutCrashing` correctly tests the simple absent-root case
  separately from the more complex symlink/permission hazards, giving each edge case its own
  focused test.

## Detailed Findings
None.

## Cross-File Observations
The case-normalization test correctly depends on `AudioTagParserTests.cpp` having already confirmed
the raw (un-normalized) tag values are read faithfully — the two files divide responsibility
cleanly with no coverage gap.

## Missing or Weak Tests
None identified.

## Positive Findings
The self-correcting filesystem-walk-based expected count, the real-Unix-permissions hardening test,
and the symlink-cycle termination test are all genuinely rigorous real-filesystem-hazard tests, well
beyond what a typical unit test suite would attempt.

## Final Assessment
No findings.
