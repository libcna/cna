# Audit: tests/Microsoft/Xna/Framework/Media/MediaQueueTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Media/MediaQueueTests.cpp`
- Audit status: AUDITED (full read, 72 lines)
- Subsystem: `tests-xna-media` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `MediaQueue` (confirmed genuine FNA implementation)
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `MediaQueue`'s public API: `Add`, `Clear`, `Count`, `ActiveSongIndex`/`ActiveSong`, indexer (both bounds), and `GetTypeName`.

## Executive Verdict
**PASS.** Small, focused, correct. Notably documents (MEDIA-11) a prior fix that changed the indexer's exception type from a bare `std::out_of_range` to the project-standard `System::ArgumentOutOfRangeException`. No MEDIUM-or-higher findings.

## Checklist Results
- `IndexerThrowsArgumentOutOfRangeExceptionWhenOutOfBounds` (line 48): tests both negative (`-1`) and overrun (`1` with a 1-element queue) indices, and correctly asserts the project-standard exception type rather than a raw `std::out_of_range` — this is the CORRECT counterpart to the confirmed `PropertyDictionary` cross-check finding (which still bakes in the wrong type); this file shows the fix was already applied here.
- `StartsEmptyWithNoActiveSong` correctly checks the "no active song" sentinel state (`ActiveSongIndex == -1`, `ActiveSong == nullptr`) as a distinct baseline before any `Add`/`Clear` behavior is exercised.
- `ClearResetsCountAndActiveIndex` confirms `Clear()` resets BOTH `Count` and `ActiveSongIndex` to their empty-queue baseline — not just one or the other.

## Detailed Findings
None.

## Cross-File Observations
- This file's use of `System::ArgumentOutOfRangeException` (post-MEDIA-11 fix) stands in direct, useful contrast to the confirmed cross-check finding for `PropertyDictionary`'s missing-key indexer, which still bakes in `std::out_of_range` as the expected (wrong) type — evidence that the correction pattern (raw `std::` exception → project `System::` exception) was applied inconsistently across the codebase rather than systematically. Worth citing in `AUDIT_CROSS_CUTTING_FINDINGS.md` as a positive counter-example to that pattern.

## Missing or Weak Tests
- No explicit test for `Add` inserting into a queue that already has an `ActiveSongIndex` set (i.e., whether adding shifts or preserves the active index) — minor, LOW severity, since `AddIncreasesCountAndActiveSongTracksIndex` does implicitly cover setting the active index after two adds.

## Positive Findings
- Clean before/after regression note (MEDIA-11) directly in the source, giving future readers the exact reasoning for the exception-type choice.

## Final Assessment
No changes needed.
