# Audit: tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/GamePadButtonsTests.cpp` (196 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::GamePadButtons` and
  `GamePadDPad`
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `GamePadButtons`'s default/flags/`FromButtonArray` constructors, equality, and
`GetHashCode`; and `GamePadDPad`'s default/explicit/`FromButtonArray` constructors, equality, and
`GetHashCode` (including the exact FNA bit-weighted formula).

## Executive Verdict
No findings. `GamePadDPadTest.GetHashCodeMatchesFnaBitWeightedFormula` correctly asserts a real,
verified FNA formula (`(Down?1:0)+(Left?2:0)+(Right?4:0)+(Up?8:0)`) with three distinct cases
(partial, none, all) — this is a genuinely FNA-derived hash formula, unlike the `GamePadState`/
`MouseState` cases flagged elsewhere in this audit, so no documentation-mismatch concern applies
here.

## Checklist Results
- `FromButtonArrayCombinesMultipleFlagsAcrossElements`/`...CombinesAcrossSeparateListElements`
  correctly verify the array form ORs flags across *separate list elements*, not just within a
  single combined-flags value — a real, distinct code path from the single-value constructor.
- `GetHashCodeMatchesUnderlyingFlagsValueAndIsConsistent` correctly asserts both an exact expected
  value (the raw flags integer) and cross-instance consistency for equal states.
- Both classes' equality tests correctly cover both the equal and differing cases.

## Detailed Findings
None.

## Cross-File Observations
`GamePadDPadTest.GetHashCodeMatchesFnaBitWeightedFormula`'s explicit "FNA:" comment citing the real
bit-weighted formula is a useful contrast case to keep in mind alongside this audit's
`GamePadStateTests.cpp`/`MouseInputTests.cpp` findings about `GetHashCode()` formulas that are
*not* actually FNA-derived — this one genuinely is.

## Missing or Weak Tests
None identified for either class's public surface.

## Positive Findings
Correct, well-organized coverage for both value types, with a genuinely FNA-verified hash formula
test for `GamePadDPad`.

## Final Assessment
No findings.
