# Audit: src/Microsoft/Xna/Framework/CurveKey.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/CurveKey.cpp`
- Audit status: AUDITED (full read, 133 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `CurveKey`, with one well-documented, correctly-scoped NaN-ordering
  deviation
- Main related tests: not independently located in this pass

## Purpose
Implements `CurveKey`'s constructors, `Clone`/`CompareTo`/`Equals`/`GetHashCode`, and equality operators.

## Executive Verdict
Healthy.

## Checklist Results

### `GetHashCode()`: XOR-combined, unaffected by the sibling addition-based UB pattern
`FloatHash(position) ^ FloatHash(value) ^ FloatHash(tangentIn) ^ FloatHash(tangentOut) ^
static_cast<size_t>(continuity)` -- no overflow possible.

### `CompareTo()`: correctly documented NaN-ordering deviation
The comment explains that C#'s `float.CompareTo` treats `NaN` as less than any real value (a total-order
convention .NET establishes for floats specifically to support sorting), while this port's raw `<`/`>`
comparisons leave `NaN` unordered (comparing as "equal", returning `0`) -- an honestly disclosed, narrow
behavioral difference only observable for a `CurveKey` with a `NaN` position (a genuinely degenerate input
that would already indicate a corrupted/invalid curve).

## Detailed Findings
None.

## Cross-File Observations
N/A.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Well-scoped, correctly-analyzed intentional deviation documentation.

## Final Assessment
No issues found.
