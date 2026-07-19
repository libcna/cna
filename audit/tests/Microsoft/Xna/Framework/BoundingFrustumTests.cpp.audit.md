# Audit: tests/Microsoft/Xna/Framework/BoundingFrustumTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/BoundingFrustumTests.cpp` (436 lines)
- Audit status: AUDITED (full read; cross-checked against
  `src/Microsoft/Xna/Framework/BoundingFrustum.cpp`'s implementation and its own already-committed
  audit report to resolve an apparent cross-file contradiction — see Cross-File Observations)
- Subsystem: `tests-xna-framework-core` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::BoundingFrustum`
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `BoundingFrustum`'s matrix property, plane accessors, corners, `Contains`/`Intersects` against
every other bounding type, equality, `GetHashCode`, and `ToString`.

## Executive Verdict
Thorough coverage overall, with one LOW-severity exception-type inconsistency relative to a sibling
file in this same shard, and correct, deliberate avoidance of a known-incomplete code path (see
Cross-File Observations for the full investigation of that path, which — initially — looked like it
might be a stale-comment finding but resolved to a non-issue on closer inspection).

## Checklist Results
- `GetCornersTooSmallVectorThrows` (line 121-126) asserts `std::out_of_range`, not this project's own
  `System::ArgumentOutOfRangeException` — the sibling `BoundingBoxTests.cpp`'s equivalent
  `GetCornersTooSmallVectorThrows` correctly asserts `System::ArgumentOutOfRangeException` for the
  structurally identical operation on `BoundingBox`. This is very likely the test faithfully
  reflecting what `BoundingFrustum::GetCorners()`'s own implementation actually throws (a production
  inconsistency, not a test-authoring error) — flagged as LOW here since it's a testing-convention
  observation, with the underlying production inconsistency belonging to whichever pass audits
  `BoundingFrustum.cpp` directly (already completed this session, `xna-framework-core` shard; worth a
  follow-up note there if not already captured).
- `IntersectsRayOriginInsideReturnZero`/`IntersectsRayOriginOutsideReturnsNullopt` (lines 339-355)
  correctly test only the two branches of `BoundingFrustum::Intersects(Ray)` that are actually
  implemented (ray origin `Contains` → 0; ray origin `Disjoint` → nullopt) — see Cross-File
  Observations for confirmation this is a deliberate, correct choice, not an oversight.
- `ContainsSameFrustumReturnContains`'s own comment (lines 218-224) is a precise, well-reasoned
  explanation of a real FNA-faithful boundary-case subtlety (two distinct objects with identical
  matrices produce `Intersects`, not `Contains`, since corners lie exactly on the boundary planes;
  `Contains` is reserved for the literal self-reference case).

## Detailed Findings
None (the exception-type note above is recorded as LOW/cross-cutting-pattern-consistent rather than a
standalone defect in this test file itself).

## Cross-File Observations
**Investigated and resolved a potential contradiction between this file and `RayTests.cpp` (same
shard):** `RayTests.cpp`'s own comment states "`Ray::Intersects(BoundingFrustum)` delegates to
`BoundingFrustum::Intersects(Ray)`, which is not yet implemented (throws `NotImplementedException`).
Test omitted until `BoundingFrustum::Intersects(Ray)` is ported." At first glance this looks
contradicted by this file's own passing `IntersectsRayOriginInsideReturnZero`/
`IntersectsRayOriginOutsideReturnsNullopt` tests, which clearly do exercise
`BoundingFrustum::Intersects(Ray)` successfully. Direct inspection of
`src/Microsoft/Xna/Framework/BoundingFrustum.cpp`'s actual implementation (and its own
already-committed, already-audited report) resolves this: `Intersects(Ray)` is genuinely a
**partial** implementation — it correctly handles the `Contains` (origin inside → distance 0) and
`Disjoint` (origin outside, ray doesn't cross in → nullopt) cases, but throws
`System::NotImplementedException` for the third, `Intersects` case (the actual general
ray-crosses-a-boundary-plane intersection math), confirmed to be a faithful reproduction of FNA's own
identical `NotImplementedException` for this exact case (`BoundingFrustum.cs` lines 453-474). Both
test files are therefore **correct and mutually consistent**: this file tests only the two working
branches, and `RayTests.cpp` correctly omits a test for the one branch that's a genuine, documented,
FNA-faithful incomplete implementation (not a stale comment after all — initially suspected, then
ruled out by checking the actual source).

## Missing or Weak Tests
No test in this file confirms `Intersects(Ray)` actually throws `NotImplementedException` for the
general (boundary-plane-crossing) case — such a test would provide regression coverage that this
known, disclosed FNA-faithful limitation doesn't silently change behavior in the future (e.g. into
returning a wrong, non-throwing answer). Low priority given the limitation is already thoroughly
documented at the production-code level.

## Positive Findings
The deliberate avoidance of testing the known-unimplemented `Intersects(Ray)` branch, combined with
`RayTests.cpp`'s parallel, consistent choice, reflects careful, cross-file-aware test authorship
rather than accidental gaps.

## Final Assessment
No new findings; one LOW cross-cutting exception-type-convention observation, consistent with a
likely production-level inconsistency already in scope for the production-code audit.
