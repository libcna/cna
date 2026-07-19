# Audit: tests/CNA/Internal/Audio/XactParserFuzzTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Audio/XactParserFuzzTests.cpp` (267 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Audio::ParseXwb`/`ParseXsb` (CNA-internal, no direct
  FNA equivalent — XACT is an XNA-adjacent authoring tool format, not part of the FNA reference tree)
- Main related tests: N/A (this IS a test file); complements `XactParserTests.cpp`'s hand-authored
  regression fixtures

## Purpose
Deterministic (fixed-seed, no wall-clock/`random_device`) mutation-based fuzzing of hand-built,
well-formed XWB/XSB seed files, asserting every mutated input either parses successfully or fails
with a clean `std::runtime_error` — never crashes, hangs, or takes unreasonable time.

## Executive Verdict
A well-designed, deterministic fuzz harness with a genuine track record: the file's own comment
states this exact technique already found two real defects during its own development (AUD-11-018's
heap-buffer-overflow, AUD-11-026's own unbounded-allocation gap) — not a hypothetical justification,
a demonstrated one.

## Checklist Results
- `Rng` is a simple, deterministic PCG-style linear congruential generator seeded with a fixed
  constant — correctly reproducible across runs (no `std::random_device`/wall-clock dependency),
  matching this project's own established convention against non-determinism in scripted/generated
  tests (per this codebase's own memory: `Date.now()`/`Math.random()` are explicitly disallowed in
  a different but philosophically related context — reproducibility is clearly valued here).
  d
- `Mutate()`'s three mutation kinds (bit-flip, truncate, byte-replace) provide reasonable structural
  diversity for a fuzzer without needing a full grammar-aware mutator, appropriate given the seed
  files are hand-built and the goal is crash/hang-freedom, not deep semantic coverage.
- The 2000ms-per-iteration watchdog (`ASSERT_LT(..., 2000)`) correctly catches a hang without
  requiring a separate spawned process (unlike the `AudioMixerTests.cpp` hardware-dependent tests) —
  appropriate here since a parser hang is a same-process, catchable-via-timeout risk rather than a
  same-process-corruption risk.
- `EXPECT_EQ(completed + cleanlyRejected, iterations)` at the end of `RunFuzz` is a real, meaningful
  assertion beyond "didn't throw an uncaught exception" — it confirms every iteration was actually
  counted (i.e. the try/catch itself didn't silently swallow an iteration).

## Detailed Findings
None.

## Cross-File Observations
Complements `XactParserTests.cpp`'s hand-authored regression fixtures for specific, named defects
(IN-2/IN-3/IN-6/IN-7/IN-8/A-12/AUD-11-003/005) — this file's broader, randomized mutation coverage
is a reasonable complement to those precise, single-purpose regression tests rather than a
duplicate of them.

## Missing or Weak Tests
2000 iterations per seed (3 seeds total) is a modest fuzz budget by industry fuzzing standards
(e.g. compared to a continuous libFuzzer/AFL campaign), though reasonable for a fast, deterministic
CI-integrated smoke check rather than a dedicated fuzzing campaign — not a defect, just a scope
note.

## Positive Findings
The explicit acknowledgment that this exact technique already found two real, previously-undiscovered
defects during its own development is strong, concrete evidence of this test's practical value.

## Final Assessment
No findings.
