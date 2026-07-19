# Audit: include/Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp`
- Audit status: AUDITED (full read, 265 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA extension, no FNA equivalent (per this file's own doc comment, real
  XNA predates both PBR and bone skinning combined) — reviewed for internal consistency
- Main related tests: not independently located in this pass

## Purpose
`PbrEffect`'s GPU-skinned sibling: the same metallic-roughness BRDF applied to a mesh with up to 72
bone transforms.

## Executive Verdict
Correct, and honestly scoped exactly like `PbrEffect` itself: the class comment explicitly
discloses that WebGPU has no skinning shader at all for any stock effect (a pre-existing gap
outside this class's own scope, not something introduced here). See the paired `.cpp` report for
a MEDIUM finding: the same `setLightingEnabledProperty(false)`/exception-type pattern already
found in `SkinnedEffect`/`EnvironmentMapEffect`/`PbrEffect`, plus two additional raw-`std::`
validation throws mirroring `SkinnedEffect`'s own already-confirmed gap.

## Checklist Results
- Doxygen coverage: complete, with the same honest scope-boundary documentation style as
  `PbrEffect`.
- `MaxBones = 72` matches `SkinnedEffect::MaxBones` (both real-XNA-precedent and this project's own
  internal consistency).
- Correctly declared as a distinct class rather than a boolean "skinned" flag on `PbrEffect` — the
  doc comment explicitly and correctly frames this as mirroring real XNA's own
  `BasicEffect`/`SkinnedEffect` precedent (separate classes per major shader variant).

## Detailed Findings
None new in this header (see `.cpp` report).

## Cross-File Observations
Shares the `setLightingEnabledProperty(false)` exception-type miss with `SkinnedEffect`/
`EnvironmentMapEffect`/`PbrEffect`, and the `SetBoneTransforms`/`GetBoneTransforms`/
`WeightsPerVertex` raw-`std::`-exception pattern with `SkinnedEffect` specifically (both classes
share near-identical bone-management code, and both share the identical exception-type gap in it).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Consistent, honest scope documentation matching `PbrEffect`'s own standard.

## Final Assessment
No new findings in this header; see the paired `.cpp` report for the MEDIUM exception-type
findings.
