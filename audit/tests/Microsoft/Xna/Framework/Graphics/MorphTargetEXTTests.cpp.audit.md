# Audit: tests/Microsoft/Xna/Framework/Graphics/MorphTargetEXTTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/MorphTargetEXTTests.cpp` (279 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `MorphTargetEXT.hpp`/`.cpp` (NOXNA glTF morph-target extension, not
  part of the XNA 4.0 API)
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `BlendMorphTargetsEXT` (weight=0/1/0.5/additive-combination math, normal renormalization,
UV pass-through untouched), `SetMorphWeightsEXT` (re-upload + stored-weight update), and
`EvaluateMorphWeightsEXT` (LINEAR/STEP/CUBICSPLINE keyframe interpolation, including a genuine
Hermite-vs-lerp-discriminating test point).

## Executive Verdict
Excellent, mathematically rigorous test file for a NOXNA extension. Not directly relevant to any of
the 10 assigned cross-check items (this file tests glTF morph-target infrastructure, unrelated to
the XNA-facing production defects this fork was assigned to cross-check), but worth flagging its
methodology as a positive example.
`CubicSplineEvaluatesRealHermiteCurveNotLinear`/`CubicSplineHonorsNonZeroTangents` hand-derive the
exact expected Hermite basis values (`h00=0.84375, h01=0.15625` at s=0.25) and assert a result that
would differ from a naive lerp, proving genuine Hermite evaluation runs rather than a disguised
linear interpolation — directly analogous to `ModelTests.cpp`'s order-discriminating transform test
and to this fork's own FNA-source-derivation methodology for the Item 3 Matrix-transpose finding.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Adds to the growing list of files in this shard using genuinely discriminating test points (values
chosen so a plausible-but-wrong implementation would produce a different, distinguishable result)
rather than values that would pass under multiple candidate implementations — see also
`ModelTests.cpp`'s bidirectional multiply-order check.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Hand-derived Hermite-basis expected values proving genuine cubic-spline evaluation (not a disguised
lerp) is a strong, mathematically rigorous test design.

## Final Assessment
No findings.
