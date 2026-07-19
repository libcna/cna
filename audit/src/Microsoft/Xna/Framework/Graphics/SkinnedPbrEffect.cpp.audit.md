# Audit: src/Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.cpp`
- Audit status: AUDITED (full read, 419 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: NOXNA extension, no FNA equivalent — reviewed for internal consistency
- Main related tests: not independently located in this pass

## Purpose
Implements `SkinnedPbrEffect`'s constructors, `Clone()`, `CacheEffectParameters()`, `OnApply()`,
`FillGpuDrawParams()`, and bone-transform accessors.

## Executive Verdict
Correct and internally consistent with `PbrEffect`/`SkinnedEffect`'s established conventions. Two
MEDIUM findings, both exception-type misses mirroring already-confirmed patterns in sibling files.

## Checklist Results
- `EnableDefaultLighting()` reuses the identical light rig as `PbrEffect`/`BasicEffect`/
  `SkinnedEffect`.
- Bone-palette upload in `FillGpuDrawParams()` (lines 399-403) correctly mirrors
  `SkinnedEffect::FillGpuDrawParams()`'s own bone-upload loop, including `weightsPerVertex`
  forwarding.
- `p.ambientColor`/`p.emissiveColor` are both populated directly with `ambientLightColor_`/
  `emissiveFactor_` (lines 362-368) — unlike `SkinnedEffect`'s pre-folded-into-emissive
  convention, this is intentional and correct: PBR's BRDF genuinely wants ambient and emissive as
  independent inputs (ambient contributes to the diffuse lighting term via the BRDF; emissive is
  additive and separate) — this is NOT the same "ambient must be pre-folded" convention the
  non-PBR stock effects share, since the PBR shader path (per this file's own class-comment
  description of the real glTF BRDF) has a genuinely different lighting model that can consume
  both terms separately.

## Detailed Findings

### MEDIUM — `setLightingEnabledProperty(false)` throws `std::runtime_error` instead of
`System::NotSupportedException`
Line 152-155, identical pattern to `SkinnedEffect`/`EnvironmentMapEffect`/`PbrEffect` (see each
file's own report).

### MEDIUM — three more raw-`std::`-exception validation throws (mirrors `SkinnedEffect`'s
identical gap)
- `setWeightsPerVertexProperty()` (line 264): `throw std::out_of_range(...)`. Should be
  `System::ArgumentOutOfRangeException`.
- `SetBoneTransforms()` (lines 271, 273, two throw sites): `throw std::invalid_argument(...)` for
  both the empty-array and exceeds-`MaxBones` cases. Should be
  `System::ArgumentNullException`/`System::ArgumentException` respectively (mirroring
  `SkinnedEffect`'s own two distinct FNA-precedent exception types, even though this specific class
  has no FNA precedent of its own — internal consistency with its own sibling `SkinnedEffect`
  still applies).
- `GetBoneTransforms(int)` (line 281): `throw std::out_of_range(...)`. Should be
  `System::ArgumentOutOfRangeException`.

## Cross-File Observations
This file's bone-management code (`SetBoneTransforms`/`GetBoneTransforms`/
`setWeightsPerVertexProperty`) is close enough to `SkinnedEffect`'s own that the exception-type
gap appears to have been copied along with the logic — both files share the identical mistake in
the identical shape, consistent with one being adapted from the other.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`FillGpuDrawParams()`'s direct (non-pre-folded) `ambientColor`/`emissiveColor` population is
correctly differentiated from the non-PBR stock effects' convention, reflecting a genuine
difference in the underlying lighting model rather than an inconsistency.

## Final Assessment
Two MEDIUM findings, both exception-type misses (`setLightingEnabledProperty` and the
bone-management trio), both mirroring already-confirmed patterns in sibling files.
