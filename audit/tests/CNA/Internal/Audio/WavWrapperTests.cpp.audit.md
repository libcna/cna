# Audit: tests/CNA/Internal/Audio/WavWrapperTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Audio/WavWrapperTests.cpp` (165 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Audio::BuildWavFromWaveFormatEx`/
  `BuildStandardMsAdpcmExtension`/`AppendSmplChunkIfLooped` (CNA-internal, no direct FNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Directly, field-by-field validates the WAV byte layout `BuildWavFromWaveFormatEx` produces (RIFF
header, fmt chunk with/without extension, fact chunk, data chunk, smpl/loop chunk), rather than only
checking "did SDL successfully decode it."

## Executive Verdict
Excellent, precise test design. The file's own top-of-file comment correctly identifies the gap
this closes: every other test exercising this wrapper only checks decodability, which would not
catch a technically-decodable-but-subtly-wrong byte layout (e.g. an off-by-N RIFF size a lenient
parser tolerates). Every byte offset asserted here is derived from the documented WAV format spec
(RIFF/WAVE chunk structure), not merely copied from whatever the implementation currently emits.

## Checklist Results
- `PlainPcmNoExtensionNoFactProducesExactly44ByteHeaderPlusData` correctly asserts the exact,
  well-known 44-byte canonical WAV header size for a plain PCM file with no extension/fact chunk.
- `ExtensionDataIsCopiedVerbatimWithCorrectCbSize` verifies both the cbSize field and a byte-for-byte
  memcmp of the extension payload — a genuinely thorough check, not just a size assertion.
- `FactChunkPresentOnlyWhenSampleFramesNonzero` correctly tests both the absence (scanning the
  entire buffer for a stray "fact" tag) and presence cases.
- `RiffSizeAccountsForEveryChunkIncludingFactAndExtension` includes a real sanity cross-check
  (`ReadU32(wav,4) + 8 == wav.size()`) in addition to the direct assertion — a nice belt-and-suspenders
  verification of internal consistency.
- `AppendSmplChunkIfLoopedIsNoOpForZeroOrNegativeLength` correctly tests both boundary conditions
  (zero and negative length) for the no-op path.

## Detailed Findings
None.

## Cross-File Observations
None beyond what's noted in `AudioMixerTests.cpp.audit.md` regarding this subsystem's general test
quality.

## Missing or Weak Tests
No test combines a non-trivial extension AND a fact chunk AND a smpl/loop chunk all at once (each
combination is tested pairwise, not all three together) — a low-priority gap given each component's
byte-offset math is independently verified correct.

## Positive Findings
Every byte-offset assertion is calculated from the real WAV format spec inline in the test itself
(e.g. `12 + 8 + 50` for chunk-header + fmtSize arithmetic), not hardcoded magic numbers with no
derivation shown — this makes the test's correctness independently auditable without needing to
trust the implementation.

## Final Assessment
No findings.
