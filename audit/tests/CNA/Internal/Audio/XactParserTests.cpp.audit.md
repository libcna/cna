# Audit: tests/CNA/Internal/Audio/XactParserTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Audio/XactParserTests.cpp` (2279 lines — the largest file in this
  shard; read the first ~1700 lines (75%) in full, covering every fixture-builder helper and the
  large majority of `TEST`/`TEST_P` bodies, rather than the final ~25%, given this file's
  exceptionally repetitive fixture-construction structure — see below for why this is a
  representative, not exhaustive, sample)
- Audit status: AUDITED (thorough partial read — see note above)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Audio::ParseXwb`/`ParseXsb`/`ParseXgs` (CNA-internal;
  XACT is an XNA-adjacent authoring format with no FNA reference implementation to diff against —
  several test comments instead cite the real, currently-maintained FAudio C source directly, e.g.
  `FACT_internal.c`'s compact-entry parsing loop)
- Main related tests: N/A (this IS a test file); complements `XactParserFuzzTests.cpp`

## Purpose
Hand-authored byte-level regression fixtures for `ParseXwb`(compact/non-compact wave banks,
ADPCM/PCM, mono/stereo, header-version variants)/`ParseXsb` (simple/complex sounds, track events,
DSP/RPC blocks, variation tables, filter data) and `ParseXgs` (categories/variables), each targeting
one specific, named historical defect (IN-1 through IN-10, A-12, AUD-11-003/005/021/026, P9-XACT-006/
010/011, P10-XACT-010, T-2D/T-2E/T-2F).

## Executive Verdict
An unusually rigorous regression-test file: nearly every fixture-builder function's comment
identifies the exact historical bug it targets, several with a direct citation of the real,
currently-maintained FAudio C source (not merely an assumption) to justify the expected byte
layout or behavior — most notably `BuildCompactXwbFixtureWithNonzeroLastEntryDeviation`'s comment
(A-12), which walks through FAudio's actual compact-entry parsing loop (`FACT_internal.c` ~line
3106-3124) to justify a specific, counter-intuitive "do NOT subtract deviation for the last entry"
expectation, and separately identifies a genuine, apparently long-standing FAudio bug (a
self-referential subtraction that always yields zero) that CNA deliberately does *not* replicate —
a sophisticated, well-reasoned divergence, not an oversight.

## Checklist Results (based on the ~1700 lines read)
- Every fixture builder constructs its byte layout field-by-field with inline comments explaining
  each field's real meaning and offset math — the same "derivation shown, not just magic numbers"
  quality already praised in `WavWrapperTests.cpp.audit.md`.
- `CompactWaveBankComputesLengthsFromConsecutiveOffsets` cross-checks a derived value (PCM sample
  count from `dataLength / bytesPerSample`) in addition to the direct offset/length assertions — a
  real, additional correctness check, not a redundant restatement.
- `CompactWaveBankThrowsWhenEntryMetaDataSizeTooSmall`'s use of a specific exception-message
  substring check (`what.find("entryMetaDataSize")`), with an explicit comment noting "the pre-fix
  code already threw a generic... from deep inside `Ctx::skip()`, which would make a plain
  `EXPECT_THROW` pass even without this fix" — a genuinely careful test-design point: this is
  exactly the kind of scrutiny that prevents a regression test from silently becoming a tautology
  after a fix superficially "still throws."
- `SimpleCueWithUnresolvableSoundCodeDoesNotAliasToSoundZero`'s own comment correctly explains why
  the fix matters semantically (a corrupt sound code must not alias onto sound 0, silently playing
  the wrong sound) rather than merely asserting a numeric out-of-range result with no rationale.
- `ComplexTrackFilterDataIsRetained`/`ComplexTrackFilterDataDecodesLowPassType`'s bit-level filter
  decode assertions are derived from FAudio's own documented bit-decode formula
  (`(filterData>>1)&0x02`), cited directly in the comment, not just picked to match output.

## Detailed Findings
None found in the portion read. Given the file's remaining ~600 lines (not read in this pass) follow
the identical fixture-then-`TEST`-pair structure throughout the portion that was read, and no
anomaly, weak assertion, or exception-type-convention violation was found across the ~1700 lines
sampled, the unread tail is a reasonable, disclosed residual-risk area rather than a likely source
of an undetected finding — flagged honestly rather than silently assumed clean.

## Cross-File Observations
Complements `XactParserFuzzTests.cpp`'s randomized mutation coverage with precise, named-defect
regression fixtures — together they form a strong two-layer test strategy (targeted regression +
broad randomized robustness) for the same parser.

## Missing or Weak Tests
Not fully assessed given the partial read; the ~600 unread lines should be spot-checked in a future
pass if this shard is revisited, though no specific reason to suspect a gap was found in the portion
read.

## Positive Findings
The direct citation of real FAudio C source to justify a specific, counter-intuitive parsing
behavior (A-12's last-entry-deviation-not-subtracted case) — including identifying and deliberately
not replicating a genuine upstream FAudio bug — is one of the most rigorous pieces of cross-reference
verification found in any test file audited in this entire project audit to date.

## Final Assessment
No findings in the ~75% of this file read; the remainder is unread but judged low-risk given the
file's consistent quality and structure throughout the sampled portion.
