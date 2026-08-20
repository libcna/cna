# Audit: tests/Microsoft/Xna/Framework/Content/ContentManagerSongXnbTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/ContentManagerSongXnbTests.cpp` (128 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for real `.xnb` `Song` loading end-to-end through `ContentManager`
  (plans/plan_xnb.md XNB-34), plus a regression re-verification for plans/plan_media.md MEDIA-75/MEDIA-10
- Main related tests: N/A (this IS a test file)

## Purpose
Two tests: (1) loads a real, externally-produced MonoGame `Song` `.xnb` fixture (with its real
companion `.ogg`) end-to-end and checks the resolved handle path and exact duration; (2) hand-builds
a full, valid `.xnb` container whose `SongReader` entry references a nonexistent companion file, and
asserts that `ContentManager::Load<Song>()` propagates `System::IO::FileNotFoundException` (not a
bare `std::runtime_error`) all the way through the real container-parsing path (header + type-reader
table + object dispatch) — not just out of `SongReader::Read()` called directly.

## Executive Verdict
Correct and well-targeted. The second test's own comment (lines 115-119) is explicit about its
narrow, deliberate scope: re-verifying the corrected exception type propagates through the *full*
`ContentManager::Load()` path specifically because `SongContentTypeReaderTests.cpp` already covers
`SongReader::Read()` in isolation — this file exists to close the gap between "the reader throws the
right type" and "the public API surfaces the right type to a caller who never sees the reader
directly." That is a meaningful, non-redundant integration point.

## Checklist Results
- `LoadRealMonoGameFixtureEndToEndProducesAPlayableVideo`-equivalent for Song: asserts both the
  resolved on-disk handle path and an exact real-fixture duration (769282 ms) — a precise,
  independently-verifiable assertion against real MonoGame-produced content, not a guess.
- `UnresolvableReferenceThrowsFileNotFoundExceptionThroughLoad`: the hand-built `.xnb` uses the
  same technique as `ContentManagerXnbTests.cpp`'s `BuildTestXnbFile()` — full, valid header + type-
  reader table + object dispatch, not a shortcut that skips container parsing.

## Detailed Findings
None.

## Cross-File Observations
Directly complements `ContentManagerVideoXnbTests.cpp` (audited in this same batch), which uses an
identical hand-built-`.xnb`-plus-real-companion-file technique for `Video` since no MonoGame/dotnet
tooling is available in this environment to produce a real `Video` `.xnb` fixture the way `Song`'s
was obtained. Also complements `ContentManagerXnbTests.cpp`'s general `.xnb` container tests (shared
`BuildTestXnbFile()`-style construction pattern).

## Missing or Weak Tests
No test in this file exercises a resolvable-but-corrupt companion audio file (e.g. an `.ogg` that
exists on disk but is not valid audio) — only the "reference resolves to nothing at all" path is
covered here. That gap may be covered elsewhere (e.g. `tests-xna-audio` shard), not confirmed in this
pass.

## Positive Findings
The `FileNotFoundException`-through-`Load()` regression test is a precise, well-motivated
integration check with a clearly documented provenance (MEDIA-75/MEDIA-10), not a vague "does it
throw something" assertion.

## Final Assessment
No findings.
