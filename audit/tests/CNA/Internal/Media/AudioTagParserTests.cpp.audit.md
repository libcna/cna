# Audit: tests/CNA/Internal/Media/AudioTagParserTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Media/AudioTagParserTests.cpp` (304 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Media::AudioTagParser` (backs
  `Microsoft::Xna::Framework::Media::Song`'s title/artist/album/genre/track/rating metadata; a
  CNA-internal implementation detail, no direct FNA equivalent since FNA typically defers to
  platform media libraries for tag reading)
- Main related tests: consumed by `MediaLibraryIndexTests.cpp` (read separately, same batch)

## Purpose
Tests real-format audio-tag extraction: Ogg Vorbis comments (incl. non-ASCII), ID3v2.3/2.4 text
frames across all four documented text-encoding bytes (Latin-1/UTF-16+BOM-LE/UTF-16+BOM-BE/
UTF-16BE-no-BOM/UTF-8), FLAC and Opus's differently-located Vorbis-comment blocks, embedded APIC
cover art, POPM/Vorbis RATING rating conversion to XNA's 0-10 scale, filename/folder fallback
heuristics, and malformed/truncated-input hardening.

## Executive Verdict
Exceptional. This is one of the most rigorous test files in this entire audit: it uses real,
ground-truth-verified media fixtures (explicitly cross-checked with ffmpeg/ffprobe during fixture
authoring, per the file's own comments) rather than hand-waved synthetic data for the format-
detection tests, AND hand-builds byte-exact synthetic ID3v2.4 frames (with correct synchsafe integer
encoding) to exhaustively cover the encoding-byte matrix that no single real fixture would exercise
in one file.

## Checklist Results
- `Id3v2TextFrameDecodesLatin1Encoding`/`...UtfWithLittleEndianBom`/`...UtfWithBigEndianBom`/
  `...Utf16BigEndianWithoutBom`/`...Utf8Encoding` collectively give complete coverage of ID3v2's
  4-value text-encoding byte, each hand-constructed via a shared `BuildId3v24WithTitleFrame` helper
  that correctly encodes synchsafe (7-bit-per-byte) frame/tag sizes matching the real format — this
  is real protocol-level test-fixture engineering, not just calling the parser with pre-made files.
- `ReadsRealFlacVorbisCommentBlock`/`ReadsRealOpusTagsHeader`'s own comment explains WHY these need
  separate code paths from the Ogg Vorbis reader (different container framing around the identical
  comment-list format) and explicitly states the "reader finds nothing in either" negative case was
  verified empirically, not merely assumed — good methodological discipline.
- `ExtractsEmbeddedApicArtFromMp3` doesn't stop at "returns non-empty bytes" — it verifies the JPEG
  SOI marker (0xFF 0xD8) AND decodes the extracted bytes through the real `ImageLoader` to confirm
  exact width/height, which is the strongest possible proof that the APIC frame-boundary parsing is
  correctly aligned (a misaligned boundary would very likely fail to decode as a valid JPEG at all,
  making this a self-verifying test).
- `ReadsId3v2PopmRatingAndConvertsToXnaScale` independently verifies the 0-255→0-10 scale conversion
  arithmetic (196×10/255=7.68→8) AND confirms the surrounding title/genre/track frames still parse
  correctly — proving the frame walk doesn't desynchronize when it encounters the binary (non-text)
  POPM frame, a genuinely important adjacency check.
- `UnratedFileReportsNoRatingRatherThanARatingOfZero` correctly distinguishes "no rating" from "a
  real rating of zero" — the exact kind of has-value/is-default distinction (mirroring XNA's
  `IsRated`) that a naive `rating == 0` check would collapse incorrectly.
- The two graceful-failure-on-malformed-input tests (`VorbisParserFailsGracefullyOnMalformedInput`/
  `...OnEmptyInput`, `Id3v2ParserFailsGracefullyOnMalformedInput`/`...OnNonId3Input`) and
  `TruncatedFlacIsRejectedWithoutReadingPastTheBuffer` correctly test hostile/truncated input
  wrapped in `EXPECT_NO_THROW`, confirming safe rejection rather than a crash or OOB read — the
  truncated-FLAC test explicitly resizes a real file down to 20 bytes (keeping just the magic +
  a block header claiming more data than remains), a realistic truncation scenario.
- `NormalizesCaseVariantArtistNamesToOneCanonicalValue`'s deliberate cross-file test (verified in
  `MediaLibraryIndexTests.cpp`, not this file) is set up here by `ReadsRealId3v24TagsFromMp3`
  confirming the raw, un-normalized "ARTIST ONE" tag value is read faithfully — the normalization
  itself is correctly the indexer's job, not the tag parser's, and this file stays properly scoped.

## Detailed Findings
None.

## Cross-File Observations
This file's raw (non-normalized) tag reads are the deliberate precondition for
`MediaLibraryIndexTests.cpp`'s `NormalizesCaseVariantArtistNamesToOneCanonicalValue` test — the two
files correctly divide responsibility (parser reads raw tags faithfully; indexer normalizes across
files) with no gap or duplicated logic between them.

## Missing or Weak Tests
None identified.

## Positive Findings
The hand-built synchsafe-integer ID3v2.4 frame construction and the self-verifying APIC-art
JPEG-decode test are both exceptionally rigorous test-engineering choices; the empirically-verified
(not assumed) FLAC/Opus negative-case claims show real methodological care.

## Final Assessment
No findings.
