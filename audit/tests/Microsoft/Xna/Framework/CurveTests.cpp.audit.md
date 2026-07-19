# Audit: tests/Microsoft/Xna/Framework/CurveTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/CurveTests.cpp` (303 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-framework-core` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Curve`
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `Curve`'s `IsConstant`, `Evaluate` (single-key, exact-key-position, Hermite-midpoint, before-
first/after-last), `Clone`, all four `ComputeTangent(s)` overloads, and every `CurveLoopType`'s
`Evaluate` behavior (Cycle/CycleOffset/Oscillate, both pre- and post-loop), plus `Step` continuity.

## Executive Verdict
Excellent, mathematically rigorous coverage — each loop-type test includes a worked-out hand
calculation in its own comment (e.g. `CycleOffset`'s "cycle=1: GetCurvePosition(0.5) + 1*(2-0)")
rather than an opaque magic expected value, making the tests independently auditable against the
documented XNA algorithm rather than merely matching whatever the implementation currently outputs.

## Checklist Results
- `ComputeTangentOutOfRangeThrows`/`ComputeTangentNegativeIndexThrows` assert raw `std::out_of_range`
  rather than this project's own `System::ArgumentOutOfRangeException` — consistent with the
  cross-cutting pattern already noted in `CurveKeyCollectionTests.cpp` (same shard), very likely
  reflecting `Curve::ComputeTangent`'s own actual implementation.
- Every `CurveLoopType` value except `Constant` (tested separately via `TwoKeysNotConstant`) has a
  dedicated `Evaluate` test for both its pre-loop and post-loop role.

## Detailed Findings
None new (see Cross-File Observations for the shared cross-cutting pattern).

## Cross-File Observations
Adds a further instance to this session's already-tracked exception-type cross-cutting pattern
(alongside `CurveKeyCollectionTests.cpp`, same shard).

## Missing or Weak Tests
Not otherwise identified — coverage is comprehensive and mathematically well-reasoned.

## Positive Findings
The hand-derived expected-value comments for each loop-type test are an exemplary practice: they let
a reviewer verify the test's own correctness against the documented algorithm, not just trust that
the implementation and the test happen to agree.

## Final Assessment
No new findings; contributes a further confirmed instance to the already-tracked project-wide
exception-type cross-cutting pattern.
