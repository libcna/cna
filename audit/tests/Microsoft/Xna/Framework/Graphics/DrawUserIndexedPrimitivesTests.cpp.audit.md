# Audit: tests/Microsoft/Xna/Framework/Graphics/DrawUserIndexedPrimitivesTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/DrawUserIndexedPrimitivesTests.cpp` (441 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `GraphicsDevice::DrawUserIndexedPrimitives` (all typed + raw-void*
  overloads)
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises the index-count formula (`PrimitiveVerts`), the missing-effect-throws guard, and the
`primitiveCount <= 0` argument guard, across all 8 typed vertex overloads, 2 `VertexDeclaration`
overloads, and the raw-`void*` overload.

## Executive Verdict
Thorough and methodically exhaustive — every one of the 10+ overloads is tested for both the
missing-effect guard and the zero/negative-count guard, including the raw-`void*` overload whose
own comment explicitly notes it "predates the typed overloads added in Task 252 and was missed by
that task's `primitiveCount` validation; fixed alongside these tests" — a real, previously-missed
gap the test author found and closed in the same task.

## Checklist Results
- Correctly uses `System::ArgumentOutOfRangeException` (the project's own convention) for the
  `primitiveCount` guard tests — a positive contrast to the raw-`std::`-exception pattern flagged
  elsewhere in this batch for `GraphicsDevice.cpp`'s *other* validation call sites (this specific
  guard evidently already uses the correct exception type).
- The missing-effect guard tests correctly use `std::runtime_error` — consistent with the sibling
  `device_core` fork's finding that this specific `GraphicsDevice.cpp` check is one of the raw-
  `std::`-exception sites (not flagged here as a new issue, since this test simply reflects current
  behavior, same reasoning as `GraphicsDeviceValidationTests.cpp`'s own audit).
- `PrimitiveVertsTest.InvalidPrimitiveType_Throws` correctly tests an out-of-range enum value
  (`static_cast<PrimitiveType>(99)`) throwing `System::InvalidOperationException` — good defensive
  coverage of an unusual input.

## Detailed Findings
None.

## Cross-File Observations
Shares the `primitiveCount <= 0` argument-guard pattern and fixture shape with the sibling
`DrawUserPrimitivesTests.cpp` (audited in this same batch) — both explicitly cross-reference each
other's task numbers (251/252/259) in their own header comments, showing coordinated test design
across the two files rather than independent, possibly-inconsistent coverage.

## Missing or Weak Tests
Pixel-readback / actual draw-call correctness tests are explicitly and honestly deferred to
integration tests (Tasks 257-258) per this file's own header comment — a disclosed scope boundary,
not a silent gap.

## Positive Findings
The raw-`void*` overload's previously-missing `primitiveCount` guard being found and fixed
specifically because this test suite's own systematic sweep included it (rather than only the
newer typed overloads) is a genuinely valuable example of thorough test-driven bug-finding.

## Final Assessment
No findings.
