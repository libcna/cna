# Audit: tests/Microsoft/Xna/Framework/Content/ContentManagerVideoXnbTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/ContentManagerVideoXnbTests.cpp` (130 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for real `.xnb` `Video` loading end-to-end through `ContentManager`
  (plans/plan_media.md MEDIA-73)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `ContentManager::Load<Video>()`'s full container-parsing path (header + type-reader table +
object dispatch) against a hand-built `.xnb` byte sequence (matching FNA's real `VideoReader` binary
layout field-for-field) paired with a real checked-in companion video file
(`tests/assets/media/video/chroma_420.mkv`), then proves the resulting `Video` is genuinely playable
via `VideoPlayer`.

## Executive Verdict
Correct, with an honestly disclosed methodological compromise. The file's own top comment (lines 3-16)
explains clearly why this fixture is hand-built rather than vendored from MonoGame like `Song`'s: no
MonoGame/dotnet content-pipeline tooling is available in this environment to produce a real `Video`
`.xnb` the same way, and this is "documented as an honest gap in plans/plan_media.md/NEXTmedia.md." The
comment reasons soundly that the container-parsing machinery doesn't care whether the bytes came from
mgcb or were hand-assembled to match the same real binary format, so the test still proves the full
path end-to-end. This is a defensible, disclosed compromise, not a silently weaker test.

## Checklist Results
- `BuildVideoXnbFile()` constructs a full, valid `.xnb` (magic, platform byte, version, compression
  flags, total length, type-reader table, root-object dispatch) — matches the same real-format
  construction technique used in `ContentManagerXnbTests.cpp`/`ContentManagerSongXnbTests.cpp`.
- `LoadRealFixtureEndToEndProducesAPlayableVideo` asserts exact width/height/fps/soundtrack-type/
  duration round-trip, then goes further than a field-round-trip check: it actually calls
  `VideoPlayer::Play()` and asserts the resulting decoded texture's dimensions match — a genuine
  proof of playability, not just that the `Video` object's fields deserialize correctly.
- Test cleans up its own written fixture file (`std::filesystem::remove(...)`, line 129) at the end,
  avoiding leaving a stray `.xnb` in the checked-in `tests/assets/media/video` directory across runs.

## Detailed Findings
None.

## Cross-File Observations
Complements `VideoContentTypeReaderTests.cpp` (not in this shard's batch) — this file's own comment
(line 15) states the reader-only tests do not cover genuine `VideoPlayer` playability, which this file
closes. Also directly parallels `ContentManagerSongXnbTests.cpp`'s hand-built-header-plus-real-
companion-file technique, applied to a different media type.

## Missing or Weak Tests
Only the happy path is covered; no negative-path test (e.g. a `Video` `.xnb` reference pointing to a
missing or corrupt companion video file) exists in this file. Given `ContentManagerSongXnbTests.cpp`
in the very same shard *does* cover exactly this negative case for `Song`, its absence here is a real,
if minor, asymmetry — a `FileNotFoundException`-style regression test for `Video` may not exist
anywhere in the suite.

## Positive Findings
The "prove genuine playability via `VideoPlayer::GetTexture()`, not just field round-trip" design is a
meaningfully stronger test than the norm, and the disclosed rationale for the hand-built fixture is a
model example of documenting an honest testing gap rather than silently working around it.

## Final Assessment
MEDIUM: no negative-path (`FileNotFoundException`-on-missing-companion) test exists for `Video`
loading through `ContentManager`, unlike the parallel, already-covered case for `Song` in this same
shard (`ContentManagerSongXnbTests.cpp::UnresolvableReferenceThrowsFileNotFoundExceptionThroughLoad`).
