# Audit: tests/Microsoft/Xna/Framework/Graphics/ViewportTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/ViewportTests.cpp` (354 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Viewport.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `Viewport`'s constructors, setters, `AspectRatio`, `Bounds`/`TitleSafeArea`, and — the
most substantial section — `Project`/`Unproject` with both identity and genuinely non-identity
(perspective + look-at) matrices, plus `MinDepth`/`MaxDepth` edge cases (Task 344: inverted
Min>Max, and Min==Max division-by-zero propagating IEEE-754 NaN, matching FNA's own unguarded
behavior exactly).

## Executive Verdict
Exceptionally rigorous. The file's own comment explicitly identifies and fixes a real
methodological gap: tests using only identity world/view/projection matrices can never exercise
the perspective-divide branch (`a` is always exactly 1.0 with identity matrices, so the divide is
always trivially skipped) — so `ProjectWithNonIdentityPerspectiveMatrix` and its siblings use a
real `CreatePerspectiveFieldOfView` projection with hand-derived expected values (worked out
step-by-step in the comment: `xScale=yScale=1`, `M33=-100/99`, etc.), explicitly noting what the
WRONG result would be if the divide were skipped (800, 0, ~4.04) to prove the test point is
genuinely discriminating — not just coincidentally passing either way. This is exactly the same
methodological rigor already praised in `ModelTests.cpp`'s multiply-order test and
`SkinnedModelEXTTests.cpp`'s rotation-pivot test — a recurring positive pattern across this shard.
`UnprojectWithEqualMinMaxDepthProducesNonFiniteResult` correctly tests that CNA does NOT add
"helpful" clamping/guards against a genuine FNA-inherited division-by-zero, matching FNA's own real,
unguarded C# behavior exactly (verified: FNA has no such guard).

## Checklist Results
- `ProjectWithMinDepthGreaterThanMaxDepthProducesInvertedZWithoutThrowing`'s comment correctly
  explains why a "protective" implementation that swapped or clamped Min/Max would be a deviation
  from FNA, not a fix — and the test is designed to fail if such a well-intentioned regression were
  introduced.

## Detailed Findings
None.

## Cross-File Observations
This file, `ModelTests.cpp`, and `SkinnedModelEXTTests.cpp` together form a strong, recurring
methodological pattern in this shard: tests that explicitly derive and assert what a plausible-but-
wrong implementation would produce, to prove the chosen test case is genuinely discriminating
rather than coincidentally passing under multiple candidate implementations. Worth citing as a
shard-wide positive pattern in any summary of this audit's findings.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
One of the most rigorous test files in this entire shard — explicit non-identity-matrix coverage
with hand-derived, discriminating expected values, and correct-by-design fidelity to FNA's
unguarded edge-case behavior (NaN propagation) rather than "improving" on it.

## Final Assessment
No findings; strong positive example of discriminating test design, consistent with the same
pattern already praised in `ModelTests.cpp`/`SkinnedModelEXTTests.cpp`.
