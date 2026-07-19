# Audit: tests/Microsoft/Xna/Framework/Graphics/SkinnedEffectTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/SkinnedEffectTests.cpp` (464 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `SkinnedEffect.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exhaustive default-value coverage for every `SkinnedEffect` property against FNA's
`SkinnedEffect.cs`, including matrices, material colors, lighting (hard-locked
`LightingEnabled=true`), fog, texture ownership, and the skinning-specific surface
(`MaxBones=72`, `WeightsPerVertex`, `GetBoneTransforms`/`SetBoneTransforms` bounds checks), plus a
regression guard for a real, previously-found-and-fixed `Clone()` bug.

## Executive Verdict
Exceptionally thorough. `AmbientLightColor`/`EmissiveColor`/`DiffuseColor` defaults (`Zero`/`Zero`/
`Vector3.One` respectively) correctly match FNA's real constructor values, and
`EnableDefaultLightingSetsAmbientLightColor` asserts the exact 3-light-rig ambient constant
(`0.05333332, 0.09882354, 0.1819608`, matching the `BasicEffect`/`AlphaTestEffect`/
`EnvironmentMapEffect`/`PbrEffect` pattern already confirmed elsewhere in this shard) to `1e-6f`
precision — no divergence found in this file for either `AmbientColor` or `EmissiveColor` handling.
`CloneCopiesAllProperties`'s own comment explicitly documents Task 401's real, previously-fixed bug
(`Clone()` never actually preserved `SpecularColor`/`SpecularPower` — the copy constructor updated a
dead cache field instead of the backing `EffectParameter`), and this test is the dedicated
regression guard for that fix.

## Checklist Results
- `GetBoneTransformsReturnsIndependentCopy` correctly tests Task 404's finding: FNA's
  `GetBoneTransforms(count)` allocates a fresh array each call rather than aliasing internal
  storage — mutating the returned vector must not affect the effect's actual bone state.
- `SetBoneTransformsThrowsOnEmpty`/`...ThrowsWhenExceedingMaxBones`/`WeightsPerVertexThrowsOnInvalidValue`
  correctly test `std::invalid_argument`/`std::out_of_range` boundary validation.

## Detailed Findings
None.

## Cross-File Observations
Corroborates (via a clean, bug-free result) the resolved cross-cutting question about
`AmbientColor`/`EmissiveColor` handling in stock lighting effects — this file finds no divergence
for `SkinnedEffect` specifically, consistent with the already-confirmed-correct 3-light-rig ambient
constant pattern shared across `BasicEffect`/`AlphaTestEffect`/`EnvironmentMapEffect`/`PbrEffect`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The dedicated `CloneCopiesAllProperties` regression guard for Task 401's real
`Clone()`-drops-Specular bug is exactly the kind of test this audit values most — a real bug found
during test-writing, fixed, and locked in against recurrence.

## Final Assessment
No findings; corroborates the resolved `AmbientColor`/`EmissiveColor` cross-cutting question as
correct for `SkinnedEffect`.
