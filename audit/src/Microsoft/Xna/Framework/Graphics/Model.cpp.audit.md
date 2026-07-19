# Audit: src/Microsoft/Xna/Framework/Graphics/Model.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/Model.cpp`
- Audit status: AUDITED (full read, 136 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Model.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors and `CopyAbsoluteBoneTransformsTo`/`CopyBoneTransformsFrom`/
`CopyBoneTransformsTo`/`Draw`.

## Executive Verdict
Behaviorally correct against FNA (`CopyAbsoluteBoneTransformsTo`'s parent-composition loop and
`Draw`'s per-mesh/per-effect world-matrix assignment both match FNA's real logic line-for-line), but
every length-validation throw in this file uses a raw `std::out_of_range`/`std::runtime_error`
instead of the `System::ArgumentOutOfRangeException`/`System::ArgumentNullException`/
`System::InvalidOperationException` types FNA's real `Model.cs` documents and this project's own
established exception-type convention calls for.

## Checklist Results
- `CopyAbsoluteBoneTransformsTo`/`CopyBoneTransformsFrom`/`CopyBoneTransformsTo`: each correctly
  checks `dest.size() < bones_.getCountProperty()` before indexing (matching FNA's real
  `destinationBoneTransforms.Length < Bones.Count` check) — the size-check *logic* is right, only
  the exception *type* diverges from FNA (see Detailed Findings). FNA's own `ArgumentNullException`
  check for a null array has no C++ equivalent need, since these parameters are `std::vector&`
  references, not nullable pointers — an accepted C++ divergence per this project's own
  documented convention (null guards omitted for C++ references).
- `Draw()`'s per-effect loop correctly resolves `boneIdx` via a null-safe fallback
  (`mesh->getParentBoneProperty() ? ... : 0`) that FNA's real `Draw()` does NOT have (FNA
  dereferences `mesh.ParentBone.Index` unconditionally) — see Positive Findings.
- The 4-argument constructor's `rootBoneIndex`/`meshParentBones` validation (lines 36-44) both
  throw raw `std::out_of_range` rather than `System::ArgumentOutOfRangeException`.

## Detailed Findings

### MEDIUM — Five throw sites in this file use raw `std::` exceptions instead of this project's
own `System::` exception types, diverging from FNA's real, documented exception contract
FNA's real `Model.cs` throws `ArgumentOutOfRangeException` for `CopyAbsoluteBoneTransformsTo`/
`CopyBoneTransformsFrom`/`CopyBoneTransformsTo`'s length checks (lines 62-63, 85-86, 95-96 in this
file, each `throw std::out_of_range(...)`), and `InvalidOperationException` for `Draw()`'s "effect
doesn't implement IEffectMatrices" case (line 123, `throw std::runtime_error(...)`). The 4-argument
constructor's own two validation throws (lines 37, 44) are likewise raw `std::out_of_range` with no
direct FNA equivalent to compare against (this constructor overload is a `NOXNA` addition), but
should still follow the project's own established `System::ArgumentOutOfRangeException` convention
for consistency. This is a real behavioral divergence, not just a style nit: a caller written to
catch `System::InvalidOperationException`/`System::ArgumentOutOfRangeException` (the types this
project's own sharp-runtime exception hierarchy provides, and that FNA's real API contract
documents) will NOT catch a raw `std::runtime_error`/`std::out_of_range` thrown here — the two
exception hierarchies are unrelated in C++, unlike .NET where every exception derives from
`System.Exception`.

**Failure scenario**: game code ported from a real XNA title that catches
`ArgumentOutOfRangeException` around a `Model.CopyBoneTransformsTo` call (a real, documented XNA
usage pattern for bounds-mismatched destination arrays) would have that catch silently fail to
catch this port's actual `std::out_of_range`, propagating past the intended handler.

**Suggested fix** (report-only, no source changes made per this audit's scope): replace all five
throw sites with `System::ArgumentOutOfRangeException`/`System::InvalidOperationException`
respectively, matching this project's own established convention (seen correctly applied in, e.g.,
`AnimationPlayer.cpp`'s equivalent validation in the very same shard).

## Cross-File Observations
This is the same recurring cross-cutting pattern already documented across multiple prior shards in
this audit (raw `std::` exception instead of the project's own `System::` exception type) — see
`AUDIT_CROSS_CUTTING_FINDINGS.md`'s general findings section. Here it recurs five times in one
file, all in FNA-parity-relevant exception-type positions rather than purely internal code.

## Missing or Weak Tests
Not independently located in this pass; a test asserting `System::ArgumentOutOfRangeException`
(not `std::out_of_range`) would have caught this.

## Positive Findings
`Draw()`'s null-safe `boneIdx` fallback (`mesh->getParentBoneProperty() ? ... : 0`) is a genuine,
real safety improvement over FNA's own `Draw()`, which unconditionally dereferences
`mesh.ParentBone.Index` and would NPE for any mesh with a null `ParentBone` — a real, reachable
state in this port specifically because its own 4-argument `Model` constructor explicitly allows
leaving every mesh's `ParentBone` null (an empty `meshParentBones` argument). This port's `Draw()`
correctly anticipates and safely handles a state its own constructor can produce that FNA's
constructor never could.

## Final Assessment
One MEDIUM finding: five raw-`std::`-exception throw sites diverging from FNA's documented
exception contract and this project's own established convention.
