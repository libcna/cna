# Audit: tests/Microsoft/Xna/Framework/Media/AlbumTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Media/AlbumTests.cpp`
- Audit status: AUDITED (full read, 334 lines)
- Subsystem: `tests-xna-media` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Album`, `AlbumCollection` (both confirmed FNA `NotImplementedException` stubs; CNA implements real behavior for both, an intentional beyond-FNA extension per `plans/plan_media.md`)
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `Album`'s public API (art/thumbnail retrieval, artist/genre/songs links, equality, disposal, `GetTypeName`) and `AlbumCollection`'s indexer/dispose/`GetTypeName`, plus a dedicated fixture (`AlbumNameCollisionTest`) proving album equality is by `(Name, Artist)` rather than `Name` alone.

## Executive Verdict
**PASS.** This is a strong, well-justified test file. It correctly documents (via comments) that CNA's `Album`/`AlbumCollection` are real implementations layered on top of what FNA leaves as stubs, and the tests are written to prove real behavior (actual cover-art bytes, actual thumbnail downscaling, actual embedded-APIC extraction) rather than accepting placeholder behavior. No MEDIUM-or-higher findings.

## Checklist Results
- Every public method/property exercised: yes — `GetAlbumArt`, `GetThumbnail`, `HasArt`, `Name`, `Artist`, `Genre`, `Songs`, `Duration`, `Equals`/`==`/`!=`, `GetTypeName`, `Dispose`/`IsDisposed` (Album); indexer (in/out of bounds), `Dispose`/`IsDisposed`, `GetTypeName` (AlbumCollection).
- Exception types: correct — `System::InvalidOperationException` for art-less access, `System::ArgumentOutOfRangeException` for indexer overrun.
- Anti-vacuity guards: present and good (`HasArtAgreesWithWhatGetAlbumArtCanActuallyProduce` explicitly asserts both the with-art and without-art branches were exercised at least once).
- Real-artifact proofs: `GetThumbnailReturnsAGenuinelySmallerImageThanGetAlbumArt` decodes both images via `ImageLoader::LoadFromMemory` and checks real pixel dimensions rather than trusting byte-length alone; `AlbumWithoutAFolderCoverFallsBackToEmbeddedArt` decodes the extracted embedded art and checks its real (400x300) dimensions, proving actual APIC extraction rather than a fallback stub.

## Detailed Findings
None at MEDIUM or higher.

- **LOW** — `AlbumEqualitySetForEqualAndUnequalAlbums` (line 138) only proves inequality between two albums that differ in both Name and Artist; it does not directly test two SAME-name/DIFFERENT-artist albums from the shared fixture (there is no such collision there by design). This gap is fully closed by the separate `AlbumNameCollisionTest` fixture in the same file (lines 38-87), which builds a dedicated scratch tree for exactly that scenario, so the coverage gap is not real — noting it only because the two tests must be read together to see the full picture.

## Cross-File Observations
- Consistent with `MediaLibraryTests.cpp` (already audited): both files document, in comments, an evolution away from earlier tests that baked in placeholder/stub behavior as the "expected" contract (see MEDIA-209, MEDIA-206/208 references) — a healthy pattern of catching and correcting vacuous tests rather than leaving them.
- `AlbumCollectionGetTypeNameIsFullyQualified` (MEDIA-121) mirrors the same "collection's own GetTypeName was missed" pattern seen and already noted in the Artist/Genre collection tests (see `ArtistTests.cpp.audit.md`, `GenreTests.cpp.audit.md`).

## Missing or Weak Tests
- No test directly exercises `AlbumCollection`'s iteration (`for (Album* a : *albums)`) as a first-class assertion of visiting exactly `Count` items in sequence (the helper `FindAlbum` does iterate, but no test asserts iteration count equals `Count` the way `GenreCollectionIndexerAndIterationWork` does for `GenreCollection`). Minor, LOW severity — indexer coverage already proves all in-bounds slots are reachable.

## Positive Findings
- `AlbumDurationIsARealNonZeroSumOfMemberSongDurations` asserts a real, ballpark (not exact-millisecond) duration range, correctly acknowledging encoder/container rounding rather than asserting a brittle exact value.
- `GetThumbnailReturnsAGenuinelySmallerImageThanGetAlbumArt` and `AlbumWithoutAFolderCoverFallsBackToEmbeddedArt` both decode real image bytes via `ImageLoader` rather than trusting stream length alone — a meaningfully strong proof standard.
- Extensive, honest inline comments citing specific `plans/plan_media.md` task IDs and explaining exactly what defect each test guards against.

## Final Assessment
No changes needed. This file meets or exceeds the project's test-quality bar for the `Album`/`AlbumCollection` surface.
