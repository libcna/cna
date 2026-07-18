# Audit: src/CNA/Internal/Backends/D3D9/shaders/cna/PbrSkinned3D.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/shaders/cna/PbrSkinned3D.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: HLSL shader source
- XNA/FNA relevance: CNA's own NOXNA SkinnedPbrEffect custom shader — PBR BRDF plus bone-palette skinning, no Microsoft Stock Effect equivalent.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

CNA's own NOXNA SkinnedPbrEffect custom shader — PBR BRDF plus bone-palette skinning, no Microsoft Stock Effect equivalent.

## Executive Verdict

Needs attention — confirms the object-space-only fog defect; genuine positive findings on normal transform and emissive.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**Object-space-only fog confirmed** (line 89-93), same pattern as the other 2 CNA-custom shaders. **Skinned normal transform applies `(float3x3)World` post-skin** (line 85), same narrower variant as `SkinnedVertexColor3D.hlsl` — this file's own header comment explicitly notes this is NOT a deviation from `EnsurePbrSkinnedProgram()`'s own GLSL (that function already includes the identical extra World step), unlike the plain `EnsureSkinnedProgram()`/`SkinnedVertexColor3D.hlsl` case where it IS a deliberate improvement — a precise, self-aware distinction the comment draws correctly. **Emissive correctly added unscaled** (line 179), consistent with `Pbr3D.hlsl`.

## Detailed Findings

**Object-space-only fog confirmed** (line 89-93), same pattern as the other 2 CNA-custom shaders. **Skinned normal transform applies `(float3x3)World` post-skin** (line 85), same narrower variant as `SkinnedVertexColor3D.hlsl` — this file's own header comment explicitly notes this is NOT a deviation from `EnsurePbrSkinnedProgram()`'s own GLSL (that function already includes the identical extra World step), unlike the plain `EnsureSkinnedProgram()`/`SkinnedVertexColor3D.hlsl` case where it IS a deliberate improvement — a precise, self-aware distinction the comment draws correctly. **Emissive correctly added unscaled** (line 179), consistent with `Pbr3D.hlsl`.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

Correct emissive combination; a precise, accurate header comment distinguishing this file's normal-transform fidelity from its plain-skinned sibling's own different situation.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
