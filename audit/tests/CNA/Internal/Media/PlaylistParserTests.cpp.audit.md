# Audit: tests/CNA/Internal/Media/PlaylistParserTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Media/PlaylistParserTests.cpp` (60 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Media::PlaylistParser` (backs
  `Microsoft::Xna::Framework::Media::Playlist`-equivalent scanning; CNA-internal M3U/M3U8 parser, no
  direct FNA equivalent)
- Main related tests: none in this shard

## Purpose
Tests M3U/M3U8 playlist parsing: valid-entry extraction with a missing-file entry correctly skipped,
non-ASCII (UTF-8) M3U8 entries, a missing playlist file, and directory-wide playlist scanning.

## Executive Verdict
Correct, small, and appropriately focused, using real fixture playlist files rather than inline
strings.

## Checklist Results
- `ParsesFavoritesM3UAndSkipsTheMissingEntry` correctly verifies BOTH that a deliberately-broken
  entry (pointing at a nonexistent file) is skipped rather than causing a fatal error, AND that
  every surviving entry genuinely resolves to a file that exists on disk — a real, filesystem-
  verified assertion rather than merely counting entries.
  `ParsesInternationalM3U8WithNonAsciiEntry` (MEDIA-58) correctly exercises UTF-8 handling with a
  real non-ASCII ("Étoile") filename, consistent with this shard's broader pattern of testing
  non-ASCII paths/tags rather than ASCII-only fixtures.
- `MissingPlaylistFileProducesEmptyResultWithoutCrashing` and `ScanDirectoryFindsBothFixturePlaylists`
  correctly cover the negative (missing file) and the aggregate (directory scan finds all fixtures)
  cases alongside the single-file happy path.

## Detailed Findings
None.

## Cross-File Observations
Consistent in style with the other `Media/` scanner tests in this shard (`MediaLibraryIndexTests.cpp`,
`PictureLibraryIndexTests.cpp`) — real fixture corpus, filesystem-verified assertions.

## Missing or Weak Tests
None identified for a file of this scope.

## Positive Findings
The filesystem-existence verification of every surviving playlist entry (not just a count check) is
a good, easy-to-skip extra rigor step.

## Final Assessment
No findings.
