# Audit: tests/Microsoft/Xna/Framework/Media/SongTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Media/SongTests.cpp`
- Audit status: AUDITED (full read, 329 lines)
- Subsystem: `tests-xna-media` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Song` (confirmed genuine FNA implementation)
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `Song`'s constructors (missing-file, three-arg), FNA-faithful hardcoded getters (`IsProtected`/`IsRated`/`Rating`/`TrackNumber` for standalone songs), content-based `GetHashCode`, handle-based equality, `PlayCount`, disposal, `GetTypeName`, and an extensive `FromUri` suite covering RFC 8089 file-URI parsing edge cases.

## Executive Verdict
**PASS — the single strongest file in this entire shard.** The `FromUri` test suite (lines 125-329) is an exceptional example of URI-parsing edge-case coverage, with multiple tests explicitly documenting bugs in EARLIER versions of the SAME tests that were only caught by mutation testing or external code review, and correcting them rather than leaving a false sense of coverage. No MEDIUM-or-higher findings.

## Checklist Results
- `ConstructorThrowsFileNotFoundExceptionForMissingFile` (MEDIA-10): correctly upgraded from a bare `std::runtime_error` to the FNA-matching typed `System::IO::FileNotFoundException(fileName)`.
- `IsProtectedIsAlwaysFalse`/`IsRatedIsFalseForAStandaloneSong`/`RatingIsZeroForAStandaloneSong`/`TrackNumberIsZeroForAStandaloneSong` (MEDIA-13): each explicitly documents WHY the hardcoded value is correct (a standalone `Song` has no scanned tag data) rather than assuming it's an unfinished stub, and cross-references the library-backed counterpart tests in `MediaLibraryTests.cpp` that DO carry real tag data — an excellent "don't 'fix' correct behavior" guard comment per this project's own recurring anti-pattern.
- `GetHashCodeIsContentBasedNotIdentityBased` (MEDIA-14): explicitly documents and locks in an INTENTIONAL deviation from FNA's identity-based `base.GetHashCode()` — CNA's `Song::GetHashCode()` is content-based (same resolved handle ⇒ same hash across distinct instances). Correctly framed as a documented improvement, not an accidental divergence.
- `EqualsAndOperatorsCompareUnequalForDifferentHandles` (MEDIA-76): proves equality is genuinely handle-based by using two DIFFERENT files with the SAME display name — the only way to prove `Equals`/`==`/`!=` don't just always return true. Correct anti-vacuity design.
- `FromUriTreatsARemoteAuthorityAsUncRatherThanSilentlyDroppingIt` (line 254): the test's own comment documents that an EARLIER version of this exact test used a fixture path where the correct and buggy behaviors produced the SAME (both-fail) result, meaning the test could pass against broken code — the comment explicitly credits mutation testing for catching this, and the fixed version is constructed so correct and buggy code diverge (UNC-path-not-found vs. local-file-found). This is one of the most rigorous single test-design corrections seen in the entire audit so far.
- `FromUriTreatsPercentEncodedDelimitersAsLiteralFilenameCharacters` (line 312): documents choosing `#` over `?` specifically because `?` is illegal in Windows filenames, making an earlier version of this test non-portable — correct, portable fixture design.
- `FromUriAcceptsEveryLocalFileUriSpelling` (MEDIA-219): tests all three RFC 8089 local-file URI spellings (`file:///path`, `file://localhost/path`, `file:/path`) after documenting that an earlier fix only handled one of them.

## Detailed Findings
None at MEDIUM or higher.

## Cross-File Observations
- The `MakeFileUri` helper (lines 125-150) correctly builds a portable file URI for BOTH platforms via `generic_string()` and manual leading-slash insertion, with an inline comment explaining exactly why naive string concatenation (`"file://" + path.string()`) fails on Windows (backslashes are invalid in URIs; even after slash-conversion, `"file://C:/x"` misparses `"C:"` as a network authority) — a genuinely useful, portable pattern that could be extracted as a shared test utility if other test files ever need to construct file URIs (currently no other file in this shard does).
- This file's exception-type discipline (typed `System::` exceptions throughout, e.g. `System::IO::FileNotFoundException`, `System::InvalidOperationException`) is fully consistent with the corrected convention seen in `MediaQueueTests.cpp`/`SongCollectionTests.cpp`, in contrast to the one raw `std::runtime_error` assertion noted in `VideoPlayerTests.cpp.audit.md`.

## Missing or Weak Tests
- None identified; this file's coverage is exceptionally thorough for its scope.

## Positive Findings
- This file is the best evidence in the whole shard of a test suite that CAUGHT ITS OWN PAST MISTAKES via mutation testing / external review, and left the history of those mistakes documented in comments rather than silently rewriting them away — exactly the kind of transparency the project's audit effort as a whole is trying to encourage in every shard.

## Final Assessment
No changes needed. Reference-quality file; worth citing as an example in any future guidance on writing rigorous URI/path-parsing regression tests.
