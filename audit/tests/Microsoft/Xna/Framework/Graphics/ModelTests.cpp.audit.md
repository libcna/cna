# Audit: tests/Microsoft/Xna/Framework/Graphics/ModelTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/ModelTests.cpp` (270 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Model.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `Model::CopyAbsoluteBoneTransformsTo`/`CopyBoneTransformsFrom`/`CopyBoneTransformsTo`
against a known, deliberately order-discriminating 3-bone hierarchy (root→child→grandchild using
scale+translate, not translation-only, specifically so a swapped multiply order would be caught),
plus the 4-arg (`meshParentBones`) and 5-arg (`rootBoneIndex`) constructor overloads.

## Executive Verdict
Exceptionally rigorous. `CopyAbsoluteBoneTransformsToGrandchildComposesInCorrectMultiplyOrder`
explicitly computes and asserts against BOTH the correct result AND what the incorrect
swapped-multiply-order result would have been (3,2,0) vs (3,1,0)) — a strong, self-verifying test
design that proves the chosen test point genuinely discriminates multiply order, rather than
happening to produce the same result either way. This directly parallels this fork's own analysis
methodology in resolving Item 3 (Matrix transpose convention) by hand-deriving what each concrete
convention would independently produce, and is worth citing as the correct test pattern to follow
for future round-trip-adjacent tests.

## Checklist Results
- `CopyBoneTransformsFromAcceptsALargerSourceIgnoringExtraElements`/
  `CopyBoneTransformsToAcceptsALargerDestinationIgnoringExtraElements` correctly test and document a
  real, confirmed CNA-vs-FNA deviation (found while designing these very tests, per the inline
  comment and now recorded in `CHECKLIST.md`): FNA's `CopyBoneTransformsFrom`/`To` loop by the
  caller-supplied array's own `Length`, so a larger-than-`Bones.Count` array makes FNA throw
  partway through; CNA loops by `Bones.Count` instead, safely ignoring extras — explicitly justified
  as the same class of intentional C++ safety improvement as `Model::Draw`/`ModelMesh::Draw`
  (Task 431).
- `FourArgConstructorSetsMeshParentBone`/`...EmptyMeshParentBonesLeavesParentBoneNull`/
  `...ThrowsWhenMeshParentBonesSizeMismatches` are genuine regression tests for a real, confirmed
  bug (Task 439): `ModelMesh::parentBone_` was never actually set by any public construction path,
  making `Model::Draw`'s parent-bone branch permanently dead code for every hand-built model.
- `FiveArgConstructorHonorsNonZeroRootBoneIndex`/`...ThrowsWhenRootBoneIndexOutOfRange` are genuine
  regression tests for a real, confirmed bug (Task 916, found via direct FNA source read of
  `ModelReader.cs`): FNA's real `Model` constructor never defaults `Root` to `bones[0]` — it's set
  externally from an explicit `rootBoneIndex`; CNA's constructors previously had no way to specify
  anything but `bones[0]`.

## Detailed Findings
None.

## Cross-File Observations
The self-verifying "assert against both the correct AND the wrong-order result" test pattern used
in `CopyAbsoluteBoneTransformsToGrandchildComposesInCorrectMultiplyOrder` is a strong methodological
positive — directly analogous to how this fork resolved the Item 3 Matrix-transpose ambiguity by
independently deriving what each candidate convention would produce, rather than trusting a single
value in isolation.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Two genuine, well-documented bug-discovery-during-test-writing narratives (Tasks 439, 916), both
with real regression tests added — exactly the kind of test-driven-discovery this audit values most
highly.

## Final Assessment
No findings; strong positive example of order-discriminating test design and genuine
bug-catching-during-authoring.
