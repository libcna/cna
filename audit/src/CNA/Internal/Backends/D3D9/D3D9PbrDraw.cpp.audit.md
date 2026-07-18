# Audit: src/CNA/Internal/Backends/D3D9/D3D9PbrDraw.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9PbrDraw.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation, 292 lines
- XNA/FNA relevance: N/A (CNA-original NOXNA PbrEffect/SkinnedPbrEffect draw dispatch, no Microsoft Stock
  Effect equivalent)
- Graphics backend relevance: real `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch for `Pbr3D.hlsl`/
  `PbrSkinned3D.hlsl`
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Constant-uploads and draws for CNA's own PBR/SkinnedPbr custom shaders, using
`D3D9ConstantUpload.cpp`'s `TryUpload*ShaderConstantEXT()` helpers against `D3D9CnaShaderRegisters.hpp`'s
own `D3DDisassemble()`-verified register tables.

## Executive Verdict

**Needs attention — confirms the object-space-only fog defect's C++-side upload; confirms correct emissive/
ambient forwarding matching `Pbr3D.hlsl`/`PbrSkinned3D.hlsl`'s own shader-side consumption.**

## Checklist Results

### Fog upload
Uploads `params.fogStart`/`params.fogEnd`/`params.fogEnabled` directly into the `FogParams` register the
shaders themselves then combine with raw local-space `Position.z` — consistent with, and the C++-side half of,
the already-recorded object-space-only fog defect (the C++ side correctly forwards the values it's given; the
defect is the shader's own choice of raw local Z rather than a World/View-transformed value, already recorded
in `AUDIT_CROSS_CUTTING_FINDINGS.md`).

### Ambient/Emissive/Metallic-Roughness upload
Uploads `AmbientColor`/`EmissiveColor`/`MetallicRoughnessFactor` directly matching `Pbr3D.hlsl`/
`PbrSkinned3D.hlsl`'s own declared register layout — consistent with those shaders' own confirmed-correct
unscaled additive emissive combination (`ambient + Lo + emissive`, not re-multiplied).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found; follows the same per-file `ResolveD3D9TextureEXT()`/`FormatHr()` local-duplication convention
already established and explained in `D3D9SkinnedVertexColorDraw.cpp`'s own header comment.

## Detailed Findings

None new — corroborates already-recorded findings from the shader-side reports.

## Cross-File Observations

Register-table consumption cross-checked directly against `Pbr3D.hlsl`/`PbrSkinned3D.hlsl`'s own declared
register layout during this shard's direct shader reading — no mismatch found.

## Missing or Weak Tests

None beyond what's already recorded cross-cuttingly for the object-space-only fog defect.

## Positive Findings

Consistent, correct constant-upload matching the shaders' own confirmed-correct emissive-combination logic.

## Final Assessment

No new defects; corroborates already-recorded cross-cutting findings from the paired shader files.
