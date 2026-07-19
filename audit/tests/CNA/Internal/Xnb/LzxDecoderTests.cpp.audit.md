# Audit: tests/CNA/Internal/Xnb/LzxDecoderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/LzxDecoderTests.cpp` (189 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::LzxDecoder`/`DecompressXnbPayload` (backs all LZX-
  compressed `.xnb` content loading), Tasks XNB-28/29/30B
- Main related tests: complements `LzxDecoderDifferentialTests.cpp`/`LzxDecoderFuzzTests.cpp` (both
  read separately in this folder)

## Purpose
Tests LZX decompression against two real, externally-produced compressed `.xnb` fixtures (a
single-block SoundEffect and a multi-block-spanning SpriteFont), plus wrong/oversized declared-size
rejection and unsupported LZX window-size rejection.

## Executive Verdict
Excellent, with a genuinely clever indirect correctness-verification technique in both real-fixture
tests: rather than (or in addition to) a byte-for-byte reference comparison, the tests parse the
DECOMPRESSED OUTPUT as a real type-reader table and then further-structured fields, verifying the
result forms a semantically coherent, real object structure — a subtly wrong decompression is
extremely unlikely to accidentally produce well-formed, semantically sensible structured data by
chance.

## Checklist Results
- `RealCompressedMonoGameFixtureDecompressesToAValidTypeReaderTable`'s own comment makes this
  reasoning explicit: "a corrupted decompression would almost certainly fail this parse or produce
  nonsense instead of a real, recognizable reader name" — and the test goes further, verifying an
  ARITHMETIC IDENTITY that holds across two independently-read parts of the decompressed stream
  (width×height read early vs. the level-0 byte count read much later, after many more decompressed
  bytes) — its own comment correctly notes "a subtly wrong LZX decompression could not produce this
  exact relationship by chance," which is a genuinely strong, self-verifying correctness check that
  needs no separate reference file.
- `MultiBlockRealFixtureDecompressesCorrectlyAcrossBlockBoundary` specifically selects a fixture
  whose decompressed size (44032 bytes) spans MORE than one 32KB LZX block, with an explicit
  `ASSERT_GT(decompressedSize, 32768)` regression guard ensuring the fixture doesn't silently shrink
  below the multi-block threshold in some future change — this genuinely exercises block-framing
  state persistence across block boundaries, not just a single `Decompress()` call, and the test
  further verifies the ENTIRE resulting type-reader-table name SET (sorted and compared exactly)
  matches the real, semantically sensible structure a genuine SpriteFont reader-set should have
  (glyph atlas Texture2D, cropping/glyph Rectangle lists, character list, kerning Vector3 list) —
  including correctly nested generic names recovered from real decompressed bytes.
- `WrongDecompressedSizeThrowsContentLoadException` and
  `OversizedDecompressedSizeThrowsContentLoadExceptionBeforeAllocating` correctly test two distinct
  size-mismatch scenarios: a plausible-but-wrong declared size, and an adversarially huge
  (`0x7FFFFFFF`) declared size — the latter's own test name makes explicit that rejection must
  happen BEFORE attempting the corresponding huge allocation, not merely eventually throwing some
  exception after already trying to allocate.
- `UnsupportedWindowSizeThrows` correctly tests both invalid boundary values (14, 22) AND the valid
  boundary value (16) in the same test, with the specific project exception type
  (`UnsupportedLzxWindowSizeRange`) rather than a generic exception — precise, complete boundary
  coverage for the window-size constructor parameter.

## Detailed Findings
None.

## Cross-File Observations
This file's arithmetic-identity-across-independently-read-fields technique is a distinct and
complementary correctness-verification approach from `LzxDecoderDifferentialTests.cpp`'s
byte-for-byte reference-implementation comparison — together the two files give strong,
non-redundant confidence (one via exact reference matching, one via internal structural
self-consistency) that the decompressor produces genuinely correct output, not just plausible-
looking output.

## Missing or Weak Tests
None identified.

## Positive Findings
The arithmetic-identity self-verification technique (proving decompression correctness via an
internal consistency relationship between two independently-read, widely-separated parts of the
output, rather than requiring a separate reference file) is a genuinely clever and underused test
design pattern.

## Final Assessment
No findings.
