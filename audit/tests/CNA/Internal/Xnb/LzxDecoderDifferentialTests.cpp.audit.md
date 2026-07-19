# Audit: tests/CNA/Internal/Xnb/LzxDecoderDifferentialTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/LzxDecoderDifferentialTests.cpp` (73 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::DecompressXnbPayload` (backs all compressed `.xnb`
  content loading), Task XNB-30A
- Main related tests: complements `LzxDecoderTests.cpp`/`LzxDecoderFuzzTests.cpp` (read separately,
  same folder)

## Purpose
Differential-tests CNA's LZX decompression byte-for-byte against reference output produced by
running FNA's own, actual, unmodified `LzxDecoder.cs` under Mono.

## Executive Verdict
Excellent — this is the strongest possible form of cross-implementation validation available for a
compression-algorithm port: the reference bytes come from EXECUTING the real C# source this port
was based on (via Mono), not from re-deriving expected output by reasoning about the algorithm a
second time. This eliminates the "the test author made the same mistake as the implementer" risk
that a hand-derived expected-value test cannot rule out.

## Checklist Results
- `ExpectMatchesReference`'s own comment correctly cites exactly how the reference files were
  generated and how to reproduce them (a documented `README.md` alongside the fixtures), giving
  real reproducibility and auditability for the reference data itself, not just a black-box
  "trust these bytes."
- The two tests cover both a single-block and a multi-block LZX-compressed fixture
  (`Explosion.xnb`/`FontCalibri14.xnb`) — a meaningful distinction since multi-block decompression
  exercises block-boundary/window-continuation logic a single-block fixture cannot.
- The test correctly parses the real XNB header first (asserting `XnbCompression::Lzx`) before
  decompressing, ensuring the fixture genuinely exercises the LZX path rather than accidentally
  testing an uncompressed passthrough.
- The byte-for-byte size AND content comparison (`ASSERT_EQ` on size before the content `EXPECT_EQ`)
  gives a clearer failure signal (size-mismatch vs. content-mismatch) than a single combined
  comparison would.

## Detailed Findings
None.

## Cross-File Observations
This differential-testing methodology is a uniquely strong validation approach among this shard's
`Xnb/` tests — most other readers are tested via hand-constructed or independently-derived expected
values; this file instead cross-checks against genuine reference-implementation execution.

## Missing or Weak Tests
None identified for the two available fixtures; broader LZX edge-case/fuzz coverage is correctly
handled in the sibling `LzxDecoderTests.cpp`/`LzxDecoderFuzzTests.cpp` files rather than duplicated
here.

## Positive Findings
Executing the actual reference C# implementation to generate ground truth, with documented
reproduction steps, is one of the strongest test-validation techniques found in this entire audit.

## Final Assessment
No findings.
