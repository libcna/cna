# Audit: tests/CNA/Internal/Xnb/SongContentTypeReaderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/SongContentTypeReaderTests.cpp` (94 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::SongContentTypeReader` (backs `.xnb`-based loading
  of `Microsoft::Xna::Framework::Media::Song`), Task XNB-34
- Main related tests: explicitly and correctly defers full end-to-end coverage to
  `SongContentManagerXnbTests.cpp` (outside this shard)

## Purpose
Tests `SongReader`'s registration and its extension-probing fallback behavior for both a
nonexistent-file reference and a real, on-disk fixture with a genuine 4-character extension.

## Executive Verdict
Correct, with a genuinely valuable piece of honest cross-project reasoning:
`ReferenceToNonexistentFileFallsBackToUnstrippedPathThenSongCtorRejectsIt`'s comment carefully
distinguishes the READER's own correct behavior (falling back to the un-stripped path, matching
FNA's `Normalize()` null-return semantics) from a SEPARATE, pre-existing, intentional CNA behavior
(the `Song` constructor eagerly validates the path and throws, stricter than FNA's lazy-until-
playback failure) — correctly attributing the resulting exception to the right layer rather than
conflating the two.

## Checklist Results
- `IsRegisteredUnderRealFnaCanonicalName` correctly verifies the exact canonical name string.
- `ReferenceToNonexistentFileFallsBackToUnstrippedPathThenSongCtorRejectsIt`'s comment additionally
  cross-references `plans/plan_media.md MEDIA-10/MEDIA-75` to confirm the specific exception type
  (`System::IO::FileNotFoundException`, matching the established `AudioEngine`/`SoundBank`/
  `WaveBank` precedent) propagates correctly all the way through `ContentManager::Load<Song>()`,
  not just at the raw constructor level — a meaningful, specific assertion beyond "it throws
  something."
- `ReferenceEndingInFourCharacterRealExtensionResolvesToRealFile` uses a REAL, externally-produced
  fixture (MonoGame's own vendored `one_two_three.ogg`/`.xnb` pair) and verifies the resolved path
  genuinely exists on disk via `std::filesystem::exists()` — proving the extension-probing logic's
  actual filesystem interaction, not just its string arithmetic.

## Detailed Findings
None.

## Cross-File Observations
The careful reader-vs-constructor attribution in the nonexistent-file test is a good example of
precise root-cause reasoning in a test comment, avoiding the common mistake of attributing an
observed exception to the wrong layer of a multi-layer call chain.

## Missing or Weak Tests
None identified for this file's scope; broader `Song` construction/playback behavior is correctly
left to `Song`'s own dedicated tests (outside this shard).

## Positive Findings
The precise separation of "reader fallback logic is correct" from "Song's constructor is
intentionally stricter than FNA" in the same test's reasoning is a good example of not conflating
two distinct layers' behavior into a single, less-precise claim.

## Final Assessment
No findings.
