# Audit: tests/CNA/Internal/Xnb/LzxDecoderFuzzTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/LzxDecoderFuzzTests.cpp` (153 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::DecompressXnbPayload` (backs all LZX-compressed
  `.xnb` content loading), Task XNB-30A
- Main related tests: complements `LzxDecoderTests.cpp`/`LzxDecoderDifferentialTests.cpp` (both
  read separately, same folder); reuses the same deterministic-LCG convention already established
  in `SdlInputBridgeFuzzTests.cpp` (explicitly cross-referenced)

## Purpose
Deterministically fuzzes `DecompressXnbPayload` by mutating REAL, externally-produced LZX-compressed
payloads (byte flips, truncation, and adversarial decompressed-size-hint corruption), asserting
2000 iterations either complete successfully with the exact declared output size or fail with
exactly one of two clean, expected exception types.

## Executive Verdict
Excellent, genuinely adversarial fuzzing that directly answers this fork directive's own scrutiny
question about this shard's fuzz-test quality. The file's own header comment gives a sound,
specific reason for mutating REAL data rather than generating random bytes from scratch: "valid LZX
bytes cannot be authored by hand, so mutation of real data is the only practical way to fuzz this
format" — a correct observation about LZX's structural complexity that a naive from-scratch
random-byte generator would almost never produce anything past the earliest decode step for.

## Checklist Results
- The `Mutate()` function applies a genuinely varied mutation strategy across 4 distinct classes
  (single-bit flip, truncation to a random shorter length, full-byte overwrite, and — notably —
  corrupting the DECOMPRESSED-SIZE HINT itself, including specifically adversarial values like 0,
  -1, and a random full-range int32) with 1-8 compounded mutations per iteration — this correctly
  covers both "corrupt compressed bytes" and "adversarial size field" as distinct input classes in
  the same fuzz harness, rather than only fuzzing the payload bytes.
- The result-size assertion (`EXPECT_EQ(result.size(), decompressedSize)` for the "completed"
  branch) is a valuable extra correctness check beyond mere crash-safety: even when a mutated input
  HAPPENS to decompress "successfully," the output must still honor its own declared contract
  (exactly the requested size) — catching a class of bug where mutation causes a subtly wrong-length
  result that would otherwise pass a crash-only check.
- The `catch` list is precisely scoped to the two documented, expected clean-failure types
  (`ContentLoadException`, `EndOfStreamException`) with no broad `catch (...)` — meaning any other
  exception type genuinely fails the test, which is the correct, strict behavior for a fuzz-safety
  net (a permissive catch-all would silently mask a genuinely new exception-type regression).
- The test's own comment honestly and accurately discloses its detection boundary: it explicitly
  states this must also be run under an ASan+UBSan build as "the real net for memory-safety/UB
  issues a plain exception-type check cannot detect on its own" — correctly acknowledging that a
  plain-build pass alone does not itself prove memory safety, consistent with the same honest
  disclosure pattern already seen in `SoundEffectContentTypeReaderPropertyTests.cpp` and
  `Texture2DContentTypeReaderTests.cpp`'s own confirmed-fixed heap-overflow finding earlier in this
  folder.
- The fixed seed (`0x4C5A58ULL`, "LZX" in hex-ish) and explicit avoidance of wall-clock/
  `std::random_device` seeding correctly ensure reproducibility across CI runs, matching the same
  deterministic-fuzzing discipline established by `XactParserFuzzTests.cpp` and
  `SdlInputBridgeFuzzTests.cpp` elsewhere in this shard.
- The final assertion deliberately does not assert a specific accept/reject split (consistent with
  the same well-reasoned choice already seen in `SoundEffectContentTypeReaderPropertyTests.cpp`),
  correctly focusing on "every mutation resolved cleanly" rather than an arbitrary proportion.

## Detailed Findings
None.

## Cross-File Observations
This file directly answers this shard's own standing scrutiny question (per this fork's original
directive) about whether the `Xnb/` fuzz tests are genuinely adversarial and deterministic: YES —
real-data mutation (not synthetic-from-scratch generation), a fixed non-wall-clock seed, a precise
(non-permissive) exception catch list, and an honest disclosure of the ASan/UBSan dependency for
full memory-safety coverage all confirm this is a rigorous, well-designed fuzz harness consistent
with this shard's other fuzz tests (`XactParserFuzzTests.cpp`, `SdlInputBridgeFuzzTests.cpp`).

## Missing or Weak Tests
None identified.

## Positive Findings
The mutation strategy's inclusion of adversarial decompressed-size-hint corruption (0, -1, random
full-range values) alongside payload-byte mutation, and the result-size contract check on
successful decompression, both meaningfully exceed a bare "doesn't crash" fuzz test.

## Final Assessment
No findings. This fuzz test is genuinely adversarial and deterministic.
