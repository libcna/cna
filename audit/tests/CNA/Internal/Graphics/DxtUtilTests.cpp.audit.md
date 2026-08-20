# Audit: tests/CNA/Internal/Graphics/DxtUtilTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Graphics/DxtUtilTests.cpp` (166 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Graphics::DxtUtil::DecompressDxt1`/`DecompressDxt3`/
  `DecompressDxt5` (CNA-internal DXT texture decompression helper, no direct FNA equivalent — FNA
  relies on the GPU's native DXT support rather than a software decompressor)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests DXT1/3/5 block decompression correctness (solid-color blocks, DXT1's punch-through-alpha
mode, DXT5's interpolated alpha, non-square multi-block images) and bounds-checking against a
too-small `dataSize`.

## Executive Verdict
Correct, with genuinely well-chosen hand-computed test blocks (each block's raw bytes are commented
with the exact RGB565/alpha values they encode) and a real, cited security-relevant regression test
(a heap-buffer-overflow found via fuzzing, now covered directly here too).

## Checklist Results
- `DecompressDxt1_TransparentPixel_WhenC0_le_C1_Index3` correctly tests DXT1's punch-through-alpha
  mode (`c0 <= c1` triggers 3-color+transparent instead of 4-color mode) — a real, easy-to-miss
  format subtlety, not just the common 4-color case.
- `DecompressDxt1_NonSquare_Width8_Height4` correctly verifies multi-block layout (2 blocks
  horizontally) with per-pixel-position assertions distinguishing left/right halves — proves block
  indexing/striding is correct, not just single-block decoding.
- `DecompressDxt{1,3,5}_DataSizeTooSmall_ThrowsOutOfRange` and
  `DecompressDxt1_ExactlyEnoughData_DoesNotThrow` correctly test both sides of the boundary (too
  little data throws; exactly enough succeeds) for all three DXT variants — this file's own comment
  cites `plans/plan_xnb.md XNB-43/47` and states this exact scenario was "confirmed as a real
  heap-buffer-overflow under -DCNA_SANITIZE=address,undefined," found via a whole-container fuzz
  test — a real, previously-discovered security-relevant bug, not a hypothetical concern.
- Correctly uses `std::out_of_range` for the bounds-check exception — consistent with this
  internal utility's own established exception-type convention for this kind of low-level parsing
  code (not flagged as an exception-type-convention violation, since `std::out_of_range` is itself
  the semantically-correct standard type for this exact kind of error, unlike the XNA-facing-API
  cases elsewhere in this audit where `System::*` exceptions are the established, and violated,
  convention).

## Detailed Findings
None.

## Cross-File Observations
None beyond the general observation that this project's fuzz-testing discipline (evidenced by the
XNB-43/47 citation) appears to be a genuinely productive practice across multiple subsystems audited
in this shard (also seen in `XactParserFuzzTests.cpp`).

## Missing or Weak Tests
`DecompressDxt3`'s explicit 4-bit alpha decode (as opposed to DXT5's interpolated alpha) has only
the solid-red case tested, no partial/varying-alpha case analogous to `DecompressDxt5_PartialAlpha` —
a minor coverage gap, low priority given DXT3's alpha decode (nibble expansion `0xF -> 0xFF`) is
simpler than DXT5's interpolation and less likely to hide a subtle bug.

## Positive Findings
Every test block's raw bytes are accompanied by a comment deriving the exact RGB565/alpha values
they encode — this makes each test's correctness independently verifiable without needing to trust
the implementation under test.

## Final Assessment
No findings.
