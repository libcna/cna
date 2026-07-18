# Audit: src/CNA/Internal/Backends/D3D9/shaders/cna/Pbr3D.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/shaders/cna/Pbr3D.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: HLSL shader source
- XNA/FNA relevance: CNA's own NOXNA PbrEffect (glTF metallic-roughness BRDF) custom shader — no Microsoft Stock Effect equivalent exists.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

CNA's own NOXNA PbrEffect (glTF metallic-roughness BRDF) custom shader — no Microsoft Stock Effect equivalent exists.

## Executive Verdict

Needs attention — confirms the object-space-only fog defect; 2 genuine positive findings.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**Object-space-only fog confirmed** (line 64-68): same pattern as `SkinnedVertexColor3D.hlsl` — raw local `vin.Position.z`, correct formula shape otherwise. **Normal transform IS correct here** (line 57: `mul(vin.Normal, NormalMatrix)`, where `NormalMatrix` is a CPU-precomputed WorldInverseTranspose register) — this file does NOT share the skinned-normal-transform bug family (expected: this is the unskinned variant). **Emissive correctly added unscaled** (line 157: `ambient + Lo + emissive`), matching the positive pattern already confirmed in Vulkan's/Bgfx's own PBR shaders — NOT the EnvironmentMapEffect-class emissive-remultiply bug. Real, functional AlphaTest support even for this non-stock PBR effect (lines 159-162) — an extra feature not seen in every other backend's PBR shader.

## Detailed Findings

**Object-space-only fog confirmed** (line 64-68): same pattern as `SkinnedVertexColor3D.hlsl` — raw local `vin.Position.z`, correct formula shape otherwise. **Normal transform IS correct here** (line 57: `mul(vin.Normal, NormalMatrix)`, where `NormalMatrix` is a CPU-precomputed WorldInverseTranspose register) — this file does NOT share the skinned-normal-transform bug family (expected: this is the unskinned variant). **Emissive correctly added unscaled** (line 157: `ambient + Lo + emissive`), matching the positive pattern already confirmed in Vulkan's/Bgfx's own PBR shaders — NOT the EnvironmentMapEffect-class emissive-remultiply bug. Real, functional AlphaTest support even for this non-stock PBR effect (lines 159-162) — an extra feature not seen in every other backend's PBR shader.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

Correct WorldInverseTranspose normal transform; correct unscaled additive emissive combination; bonus AlphaTest support for a non-stock effect.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
