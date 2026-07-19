# Audit: tests/Microsoft/Xna/Framework/Graphics/EffectParameterTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/EffectParameterTests.cpp` (707 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `EffectParameter.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `EffectParameter`'s full Get/SetValue overload set (scalar, bool, int, Vector2/3/4,
Matrix, Quaternion, string, textures, arrays) plus `SetValueTranspose`/`GetValueMatrixTranspose`.

## Executive Verdict
**CRITICAL finding, independently verified against real FNA source (not just trusted from the
sibling production-code audit): this test file's own comment and its
`SetValueTransposeRawLayoutDiffersFromSetValue` test assertions actively BAKE IN the exact-inverse
of real FNA's `SetValue(Matrix)`/`SetValueTranspose(Matrix)` storage convention, falsely claiming
"This matches FNA's semantic."** This is worse than merely missing the already-confirmed
production defect (`EffectParameter.cpp`'s Matrix Get/Set/Transpose semantics are inverted
relative to FNA) — it locks in the wrong behavior as the tested, "correct" contract.

## Checklist Results
- Direct FNA source verification (`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/EffectParameter.cs`,
  read directly for this audit):
  - `SetValueTranspose(Matrix value)` (FNA source lines 632-718): writes the internal buffer
    `dstPtr[0]=M11, dstPtr[1]=M12, dstPtr[2]=M13, dstPtr[3]=M14, dstPtr[4]=M21, ...` —
    **ROW-MAJOR** (straight, untransposed) storage.
  - `SetValue(Matrix value)` (FNA source lines 835-921): writes
    `dstPtr[0]=M11, dstPtr[1]=M21, dstPtr[2]=M31, dstPtr[3]=M41, dstPtr[4]=M12, ...` —
    **COLUMN-MAJOR** (transposed) storage.
  - This is the **opposite** of what this test file's own comment (lines 586-596) claims: "CNA
    stores `SetValue(Matrix)` in row-major order... `SetValueTranspose(m)`... stores column-major
    order of m... This matches FNA's semantic." Real FNA does it exactly backwards.
- `SetValueTransposeRawLayoutDiffersFromSetValue` (lines 611-629) asserts, using the fully-asymmetric
  test matrix `MakeAsymmetric()` (all 16 distinct elements — a genuinely strong test input, correctly
  chosen to distinguish row-major from column-major):
  ```cpp
  p.SetValue(m);
  const auto rowMajor = p.GetValueSingleArray(16);
  ...
  EXPECT_NEAR(rowMajor[1], 2.0f, 1e-5f);   // M12 — asserts SetValue stores row-major
  p.SetValueTranspose(m);
  const auto colMajor = p.GetValueSingleArray(16);
  ...
  EXPECT_NEAR(colMajor[1], 5.0f, 1e-5f);   // M21 — asserts SetValueTranspose stores column-major
  ```
  Per the direct FNA source reading above, real FNA's `SetValue(m)` would store `dstPtr[1] =
  value.M21` (5.0f for this matrix), **not** `M12` (2.0f) — and `SetValueTranspose(m)` would store
  `dstPtr[1] = value.M12` (2.0f), **not** `M21` (5.0f). This test's expected values are the exact
  inverse of the real FNA-faithful ones.
- The round-trip-only tests (`SetValueMatrixRoundTrip`, `SetValueGetValueMatrixRoundTrip`,
  `SetValueTransposeDoubleTransposeRoundTrip`, `SetValueTransposeEquivalentToSetValueOfTranspose`)
  cannot and do not catch this defect, exactly as the sibling production-code audit's own
  investigative question anticipated: since both the write (`SetValue`) and the read
  (`GetValueMatrix`) sides of this port use the same (wrong, self-consistent) convention paired
  together, a round trip through only this port's own methods returns the original value
  regardless of whether either individual method matches FNA. `SetValueMatrixRoundTrip` additionally
  uses `Matrix::CreateTranslation(5,6,7)` as its test matrix — a weak choice independent of the
  round-trip issue, since a pure-translation matrix's structure doesn't strongly distinguish
  row-major from column-major storage the way `MakeAsymmetric()` does.
- Every other Get/SetValue overload (scalar, bool, int, Vector2/3/4, Quaternion, string, textures,
  arrays) is correctly tested via meaningful round-trips with non-trivial values (including
  partial-count array reads, NaN handling matching FNA's documented non-debug-mode leniency, and
  type-mismatch `SetValue` calls correctly asserting FNA's real "no validation" behavior).
- `Elements`/`StructureMembers` properties: **no test exists for either property anywhere in this
  file** — confirmed via full read, zero references. The already-confirmed production defect
  (both permanently empty) has zero test coverage in either direction.

## Detailed Findings

### HIGH — Test suite bakes in the inverse of FNA's real `SetValue`/`SetValueTranspose` Matrix storage convention as the expected, "FNA-matching" behavior
See Checklist Results above for the full derivation. This is the single most severe finding in this
file: not a missing test, but an incorrect one that would actively resist a correct fix (fixing
`EffectParameter.cpp` to match real FNA would make `SetValueTransposeRawLayoutDiffersFromSetValue`
fail, since the fixed code would then produce the opposite raw-layout values this test currently
asserts as correct).

### MEDIUM — `Elements`/`StructureMembers` have zero test coverage
No test exists for either property. Given the confirmed HIGH-severity production finding that both
are permanently empty, a test would very likely have caught this immediately (constructing a
struct/array-typed `EffectParameter` and asserting `Elements.Count > 0` would fail against the
current implementation) — but no such test was written.

## Cross-File Observations
This is the definitive, independently-verified resolution of the sibling `effects-infra` fork's own
finding in `src/Microsoft/Xna/Framework/Graphics/EffectParameter.cpp.audit.md` — that fork
correctly identified the Matrix transpose inversion by diffing against FNA source directly; this
audit additionally proves the test suite itself would block, not merely fail to catch, a correct
fix.

## Missing or Weak Tests
- A test using `SetValueTransposeRawLayoutDiffersFromSetValue`'s exact same `MakeAsymmetric()`
  input, but with expected values corrected to match real FNA's actual column-major/row-major
  convention, is needed once the production code is fixed (the existing test would need its
  expected values swapped, not merely re-run).
- Any test at all for `Elements`/`StructureMembers`.

## Positive Findings
`MakeAsymmetric()`'s choice of a fully-asymmetric 4×4 test matrix (all 16 distinct elements) is
methodologically excellent — it is exactly the right kind of input to distinguish row-major from
column-major storage, and would have caught this bug immediately had the expected values been
independently derived from real FNA source rather than from the current (wrong) implementation's
own output.

## Final Assessment
One HIGH finding (test suite bakes in the inverse of FNA's real Matrix storage convention,
independently confirmed against FNA source) and one MEDIUM finding (zero coverage for
`Elements`/`StructureMembers`).
