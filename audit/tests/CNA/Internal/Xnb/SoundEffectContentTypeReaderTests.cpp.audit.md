# Audit: tests/CNA/Internal/Xnb/SoundEffectContentTypeReaderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/SoundEffectContentTypeReaderTests.cpp` (1105 lines — read in
  full)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::SoundEffectContentTypeReader` (backs `.xnb`-based
  loading of `Microsoft::Xna::Framework::Audio::SoundEffect`), Tasks XNB-33/33A + a long series of
  `plans/plan_audio.md` AUD-06 sub-tasks (2026-07-17 deep audit)
- Main related tests: uses `SoundEffectInstanceTestAccess` (a test-only accessor);
  complements `SoundEffectContentTypeReaderPropertyTests.cpp`'s boundary-value sweep and
  `XnbContainerFuzzTests.cpp`'s whole-container fuzzing (both read earlier in this folder)

## Purpose
The largest and most comprehensive reader-test file in this shard: exercises the full
WAVEFORMATEX-format support matrix (PCM16/8, IEEE float, MS-ADPCM, IMA-ADPCM all real-fixture-
backed; XMA2 rejected), Xbox big-endian byte-swapping, format-chunk-size boundary classes,
duration-oracle validation, loop-point unit-correctness (frames vs. compressed bytes), channel-
count rejection, and several confirmed-fixed exception-context/diagnostic-quality regressions.

## Executive Verdict
Exceptional — comparable in rigor to `ENetBackendTests.cpp` and `VideoDecoderTests.cpp` elsewhere in
this shard, and arguably the strongest audio-format-parsing test file encountered in this entire
audit. Its own header comment documents a real, deliberately-widened support matrix (PCM8/float/
MS-ADPCM/IMA-ADPCM all upgraded from "rejected" to real WAV-wrapped SDL3 decoding across the AUD-06
task series), and every non-trivial parsing branch this widened matrix introduces gets its own
carefully-reasoned, often adversarially-constructed test.

## Checklist Results
- The header comment's explicit rationale for using REAL, externally-produced fixtures rather than
  hand-crafted PCM buffers for the "LoadsSuccessfully" tests is sound: "a hand-authored PCM buffer
  would trivially 'pass' without proving the real WAVEFORMATEX byte layout was parsed correctly" —
  precisely the right skepticism about self-authored test data for a binary-format parser.
- `Xma2IsRejected` correctly explains why no real XMA2 fixture is vendored (neither this project's
  library nor MonoGame's own test corpus ships one) and instead hand-constructs a minimal but
  complete XNB stream matching the exact layout confirmed by hex-dumping a real fixture — a
  reasonable, well-justified alternative when a real negative-case fixture genuinely doesn't exist.
- `XboxPlatformByteSwapsWaveFormatFieldsCorrectly` (AUD-06-015) is a standout test: it writes every
  WAVEFORMATEX field in REAL big-endian byte order (matching genuine Xbox 360/PowerPC-produced XNB
  encoding) while correctly keeping every OTHER container field (formatLength, data length, loop
  points, and the raw PCM samples themselves) little-endian — exactly matching FNA's own
  `SoundEffectReader.Swap()` scope (it only ever wraps the WAVEFORMATEX structure's own fields, not
  the whole container). The test's own comment gives a precise, falsifiable prediction of what
  would happen if the swap were silently disabled (`44100` misread as a little-endian `uint32`
  without swapping decodes as `1,151,051,776` Hz) — genuinely strong discriminating power, not a
  test that could pass by accident.
- `NegativeDataLengthFailsCleanlyRatherThanCrashing` and
  `OversizedDataLengthAgainstTruncatedStreamFailsCleanlyNotWithHugeAllocation` (AUD-06-012) both
  correctly test the two edges of an untrusted declared data-length field — negative (a
  signed-to-unsigned-cast allocation-bomb hazard) and adversarially huge against a stream that
  provably can't contain that much data — with the CORRECT distinct exception types for each
  (`ContentLoadException` for the negative case; `System::IO::EndOfStreamException` for the
  truncated-stream case), showing precise understanding of which guard catches which failure mode.
- `FormatLengthOneByteTooSmallForCbSizeThrowsCleanly` and
  `PathologicallyLargeFormatLengthThrowsCleanlyNotDesync` (AUD-06-011) both target a genuinely
  subtle arithmetic hazard: `formatLength`-derived `skip = formatLength - 18[-34]` computations that
  must never silently desynchronize the reader onto the wrong subsequent field. The comments
  correctly reason through the exact arithmetic (why `formatLength=17` produces `skip=-1`, caught by
  the existing negative-count guard; why a near-`uint32_t`-ceiling `formatLength` stays bounded via
  `int64_t` intermediate arithmetic before any `int32_t` cast) — precise, mechanism-level reasoning
  rather than a vague "large values should fail."
- `UnknownFormatTagDiagnosticIncludesAllRelevantFields` (AUD-06-017) tests DIAGNOSTIC MESSAGE
  CONTENT specifically — verifying the exception text includes the format tag, bit depth, channel
  count, sample rate, AND the asset name, not just "some error occurred" — a genuinely useful class
  of test for real-world triage quality that many test suites skip entirely.
- `IncoherentBlockAlignForPcm8IsIgnoredNotTrustedBySdlDecoder` (AUD-06-013) is a careful empirical
  investigation turned into a regression test: it documents having CONFIRMED (not assumed) that
  SDL3's own WAV loader recomputes the correct PCM block size internally rather than trusting the
  file's declared `nBlockAlign`, and the test proves the resulting decode is correct DESPITE a wildly
  incoherent declared value (100 vs. the coherent value of 2) — with the comment explicitly stating
  no additional CNA-side coherence check is needed given this confirmed backend behavior. This is
  exactly the right way to document a "we investigated and found this isn't actually a problem"
  conclusion as a durable regression test, not just a comment.
- `DrasticDurationOracleDisagreementThrowsWithBothValues`/`SmallDurationOracleDisagreementDoesNotThrow`
  (AUD-06-010) correctly test both sides of a generous validation threshold (100× disagreement
  throws; a plausible ~4% whole-millisecond-rounding disagreement does not), with the disagreement
  test's own comment noting the threshold was calibrated against all 6 already-passing real-fixture
  tests elsewhere in the file (proving the generous threshold doesn't false-positive on legitimate
  content) — a well-grounded calibration rather than an arbitrary number.
- `ImaAdpcmLoopPointsSurviveAsDecodedFramesNotCompressedBytes` (AUD-06-014) is one of the most
  carefully engineered tests in the file: it deliberately chooses loop-point values (1500, 1900) that
  are SANE as decoded-frame indices (< 2020 decoded frames) but NONSENSICAL as compressed-byte
  offsets (> 1024 compressed bytes) — a real, empirical, not merely code-inspected, proof that loop
  points survive the WAV-wrapping compression path as frames, not bytes. It even includes a
  self-check (`decodedFrames` sanity assertion) confirming the fixture genuinely compresses ~4:1,
  ensuring the byte-vs-frame distinction the test exists to prove is actually meaningful for this
  specific fixture.
- `FormatLength16BareWaveFormatConsumesExactlyDeclaredBytes`/`FormatLength18WithZeroCbSizeConsumesExactlyDeclaredBytes`/
  `ExtendedFormatLengthWithRealCoefficientTableConsumesExactlyDeclaredBytes` (AUD-06-016) precisely
  target THREE distinct format-chunk-size code branches (`<=16` no cbSize; `==18` cbSize present but
  zero extension; `>18` real extension payload) and each verifies correctness not by inspection but
  by asserting the resulting DECODED FRAME COUNT matches a value only achievable if every subsequent
  field was read from the exactly correct offset — the comment correctly notes an off-by-N
  desync would either be caught by the AUD-06-012 guard (thrown exception) or silently produce a
  wrong frame count (caught by this assertion) — a genuinely bulletproof verification design for a
  variable-length-header parsing correctness question. The MS-ADPCM extension test additionally
  uses the exact 7 industry-standard coefficient pairs SDL3 itself validates against.
- `MultichannelXnbIsExplicitlyRejectedWithClearDiagnostic`/`ZeroChannelsXnbIsExplicitlyRejectedWithClearDiagnostic`
  (AUD-06-018) correctly test both sides of the mono/stereo-only channel-count policy (a plausible
  "sounds like it could be valid" 5.1-surround value, and the degenerate zero case), with both
  verifying the diagnostic text explicitly mentions "mono and stereo" — again testing diagnostic
  quality, not just rejection.
- `LooplessXnbEffectHasNoLoopRegionOnInstance` (AUD-06-019) correctly closes a specific,
  well-identified gap left by two OTHER existing tests (one proving loop-frame propagation, one
  proving loop-region PLAYBACK correctness) — the loopless (0,0) case specifically, checking it
  produces a genuine no-loop state rather than a spurious zero-length region that could be misread
  downstream.
- `Pcm8WithZeroSampleRateFailsCleanlyRatherThanCrashing`/`Pcm16WithZeroSampleRateFailsWithAssetContextNotRawException`
  (AUD-06-013/023) both document and test confirmed-fixed exception-context-loss regressions: a raw
  `NotSupportedException` used to escape with no asset name/context for BOTH the WAV-wrapped path
  and (found later, AUD-06-023 explicitly notes it was MISSED by the AUD-06-024 fix which only
  covered the wrapped path) the direct 16-bit-PCM fast path — both now verified to produce a
  `ContentLoadException` carrying the asset name and an inner-exception `--->` chain marker
  preserving the original root cause. The comment's honest acknowledgment that the first fix missed
  a second, structurally similar path is a valuable example of a test suite catching an incomplete
  prior remediation.

## Detailed Findings
None — this file is exemplary throughout its full 1105 lines.

## Cross-File Observations
This file's diagnostic-message-content tests (`UnknownFormatTagDiagnosticIncludesAllRelevantFields`,
`MultichannelXnbIsExplicitlyRejectedWithClearDiagnostic`, `DrasticDurationOracleDisagreementThrowsWithBothValues`)
represent an under-used but valuable test class not seen as consistently elsewhere in this shard:
verifying an exception's actual message TEXT contains specific, useful triage information, not just
its type. Combined with `SoundEffectContentTypeReaderPropertyTests.cpp`'s cross-product boundary
sweep and `XnbContainerFuzzTests.cpp`'s whole-container fuzzing, this trio of files gives the
SoundEffect `.xnb` reader some of the most complete test coverage of any single reader in the
project — unit tests for every named field/branch here, a property-based cross-product sweep there,
and whole-container adversarial fuzzing in the third.

## Missing or Weak Tests
None identified.

## Positive Findings
The Xbox byte-swap test's precise big-endian/little-endian field scoping (matching FNA's exact
`Swap()` behavior) with a falsifiable wrong-value prediction, and the IMA-ADPCM loop-point
frames-vs-bytes test's self-verifying compression-ratio sanity check, are both exceptionally
well-engineered tests that leave essentially no room for a passing-by-accident false positive.

## Final Assessment
No findings. Alongside `ENetBackendTests.cpp` and `VideoDecoderTests.cpp` elsewhere in this shard,
this file should be considered a reference example of rigorous, mechanism-level, diagnostic-quality-
aware regression-test authorship for this project.
