# Audit: tests/Microsoft/Xna/Framework/Graphics/PackedVector/PackedVectorTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/PackedVector/PackedVectorTests.cpp` (1042 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for every type under `include/Microsoft/Xna/Framework/Graphics/PackedVector/`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises every `PackedVector` type's constructors, `PackFromVector4`/`ToVector4` round-trips,
equality, and (Task 199) edge cases: clamping above/below the valid range, `HalfTypeHelper` special
float values (NaN, ±infinity, denormals), and boundary round-trips.

## Executive Verdict
**Confirmed MISS (not merely weak coverage) for Item 8: no test anywhere in this file uses a
genuinely fractional (non-integer, non-boundary) input value for `Byte4`, `Short2`, or `Short4` —
the exact three types the sibling production-code fork confirmed truncate instead of round in
`Pack()`.** Every test for these three types either uses a raw-packed-uint constructor, an
all-zero input, or an exact-integer float value, none of which can distinguish truncation from
rounding.

## Checklist Results
- **Item 8 cross-check, `Byte4`**: `Byte4Test` has exactly 4 tests total — `DefaultPackedZero`,
  `CtorFromRawPacked` (constructs from a raw `uint32_t`, never calls `PackFromVector4`/the
  float-based constructor at all), `PackFromVector4Zero` (all-zero input — 0.0f truncates and
  rounds identically), `EqualityTrue`/`EqualityFalse` (also via the raw-uint constructor). **No test
  constructs a `Byte4` from a fractional float value at all.** Verdict: **MISSES** — no test exists
  that would reveal truncation vs. rounding either way.
- **Item 8 cross-check, `Short2`**: `Short2Test` has `DefaultPackedZero`, `CtorZero`,
  `ToVector4ZW`, `ToVector4XY` (uses `100.0f, -200.0f` — exact integers, round-trips identically
  under truncation or rounding), `EqualityTrue`/`EqualityFalse`. The separate `Short2EdgeTest`
  section (`ClampLargePositive`/`ClampLargeNegative`) tests clamping with `40000.0f`/`-40000.0f` —
  also exact integers, testing only the clamp boundary, not the rounding behavior within the valid
  range. **No fractional-value test exists.** Verdict: **MISSES**.
- **Item 8 cross-check, `Short4`**: `Short4Test.ToVector4XYZW` uses `10.0f, 20.0f, 30.0f, 40.0f` —
  again exact integers. `Short4EdgeTest.ClampAllChannels` uses `40000.0f, -40000.0f, 32767.0f,
  -32768.0f` — clamp-boundary values, not fractional. **No fractional-value test exists.**
  Verdict: **MISSES**.
- Contrast: `NormalizedByte2`/`NormalizedByte4`/`NormalizedShort2`/`NormalizedShort4` (the sibling
  types the production-code fork confirmed correctly use `std::lroundf`) also have no explicit
  fractional-rounding-tie test in this file, but their existing "Golden" tests (e.g.
  `NormalizedByte2Test.GoldenPackedValues` testing `0.5f → 0x4040`) do exercise a genuinely
  fractional-relative-to-the-integer-domain value (0.5 in a [-1,1]→[-127,127] mapping is not an
  exact integer post-scaling in the same trivial way `100.0f` is for `Short2`'s [-32768,32767]
  direct range) — though this audit did not independently re-derive whether these specific "Golden"
  values would actually distinguish round-vs-truncate for the Normalized types either; flagged as
  worth re-checking in a future pass, not asserted here as confirmed coverage.
- `HalfTypeHelperTest`'s special-value suite (`NegativeZero`, `PositiveInfinity`,
  `NegativeInfinity`, `NaNPreservesNaN`, `DenormalSmallest`, `DenormalMid`, `SmallFloatBecomesZero`)
  is thorough and well-targeted at real IEEE-754 half-float edge cases, independent of the
  Byte4/Short2/Short4 rounding question.

## Detailed Findings
None beyond the Item 8 cross-check misses documented above (these are the assigned investigative
question's answer, not separate new findings).

## Cross-File Observations
This confirms the sibling `vertex_packed` production-code fork's own MEDIUM finding
(`include/Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp.audit.md` and its `Short2`/`Short4`
siblings) has zero test coverage in either direction — the defect is neither caught nor baked in as
expected; it is simply invisible to this test suite because no test exercises the input domain
(fractional values) where truncation and rounding diverge.

## Missing or Weak Tests
A test constructing e.g. `Byte4(100.7f, ...)`/`Short2(100.7f, -200.3f)`/`Short4(100.7f, ...)` and
asserting the packed/round-tripped value reflects **rounding** (matching FNA's real `(uint)Math.Round(...)`
behavior, per the sibling production-code fork's finding) would both catch the current defect (it
would fail against today's truncating implementation) and serve as the correct regression test once
fixed.

## Positive Findings
The `HalfTypeHelperTest` special-value suite and the systematic clamp-boundary tests (`Byte4`
excepted, which has none) for every other type are thorough and well-targeted.

## Final Assessment
Confirmed miss for Item 8 across all three affected types (`Byte4`, `Short2`, `Short4`) — the
defect has zero test coverage in either direction, not a false-positive "passing" test.
