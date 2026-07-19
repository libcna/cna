# Audit: include/Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp`
- Audit status: AUDITED (full read, 420 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/SkinnedEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Built-in effect for rendering skinned character models with up to 72 bone transforms, lighting,
optional fog, and a diffuse texture.

## Executive Verdict
Correct property/method shape, matching FNA's `SkinnedEffect` exactly (`MaxBones = 72`,
`WeightsPerVertex` restricted to {1,2,4}, `LightingEnabled` unsettable-to-false,
`SetBoneTransforms`/`GetBoneTransforms`). See the paired `.cpp` report for a MEDIUM finding: four
distinct validation-throw call sites use raw `std::` exceptions instead of this project's own
`System::` exception types, and for confirmation this file's `FillGpuDrawParams()` correctly
pre-folds ambient into emissive rather than ever touching `GpuDrawParams::ambientColor` — direct,
positive evidence for a cross-cutting hypothesis this project's audit had previously only inferred
indirectly.

## Checklist Results
- Doxygen coverage: complete.
- `MaxBones = 72` matches FNA's `public const int MaxBones = 72;` exactly.
- `VertexColorEnabled` is correctly `NOXNA`-tagged with an honest rationale (real XNA's
  `SkinnedEffect` has no such property; added for glTF-imported meshes with a `COLOR_0` attribute).
- `SetOwnedTexture()` correctly mirrors `BasicEffect`'s own pattern and rationale.

## Detailed Findings
None new in this header (see `.cpp` report for the exception-type finding, which surfaces at the
implementation, not the declaration).

## Cross-File Observations
Confirms this project's own `AUDIT_CROSS_CUTTING_FINDINGS.md` hypothesis under "Systematic FNA
parity gaps" (the `ambientColor`/`emissiveColor` entry) is correct: see the paired `.cpp` report's
`FillGpuDrawParams()` finding for the direct confirmation from the actual C++ source, rather than
inference from backend-shader-side symptoms alone.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Full XNA API surface faithfully reproduced, including the `WeightsPerVertex`/`SetBoneTransforms`/
`GetBoneTransforms` trio's exact validation contract (documented, if not correctly typed at the
throw site — see `.cpp` report).

## Final Assessment
No new findings in this header; see the paired `.cpp` report for the MEDIUM exception-type finding.
