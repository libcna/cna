# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/pbr_skinned3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/pbr_skinned3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: NOXNA — SkinnedPbrEffect (metallic-roughness PBR + bone skinning), a CNA extension
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: N/A — no direct FNA/XNA equivalent
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

PBR + skinning combo: bone-skins Position/Normal/Tangent (up to 72 bones, 1/2/4 weights per vertex, matching FNA's real `Skin()` weight-count gating) before the World transform, then follows `pbr3d.vert.hlsl`'s own transform/fog pattern.

## Executive Verdict

**Needs attention — shares the systemic fog bug AND the skinned-normal-transform bug (self-documented).**

## Checklist Results

### Systematic FNA parity gaps
**HIGH — self-documented deviation from its own correct unskinned sibling.** Line 74-75: `float3x3 skinNormalMat = (float3x3)skinMat; output.Normal = normalize(mul(mul(input.Normal, skinNormalMat), (float3x3)World));` — uses the **raw** `World` matrix for the outer transform, not the inverse-transpose `pbr3d.vert.hlsl`'s own unskinned sibling correctly uses. The file's own comment self-documents this exactly: "plain World (NOT the inverse-transpose pbr3d.vert.hlsl's unskinned sibling uses), the same simplification skinned3d.vert.hlsl's own normal transform already makes" — i.e. the author knew the correct convention (visible one file away, in the very same directory) and used the wrong one anyway for the skinned variant, framing it as an established simplification rather than re-deriving correctness. Under non-uniform-scale World transforms this produces visibly wrong lighting on skinned PBR models. This is the 5th of 5 D3DCommon skinned vertex shaders confirmed to share this defect family (the other 4 omit World entirely; this one uses raw World) — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 82: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (HIGH):** raw-World-instead-of-inverse-transpose normal transform, lines 74-75.
**F2 (MEDIUM):** mirrored fog formula, line 82.

## Cross-File Observations

Directly comparable to `pbr3d.vert.hlsl` (gets this right) and to `skinned3d.vert.hlsl` (shares the complete-omission variant of the same underlying mistake) — this file's own comment ties both comparisons together explicitly, making it the clearest single piece of evidence in this audit that the defect is a known, accepted shortcut rather than an unnoticed bug.

## Missing or Weak Tests

No dedicated D3D11/D3D12 `SkinnedPbrEffect` non-uniform-scale lighting test found — masked by every current test's use of `World=Identity`, per this audit's established pattern for this whole bug family.

## Positive Findings

Correctly ports FNA's bone-weight-count gating (`weightsPerVertex >= 2.0`/`>= 4.0`) exactly.

## Final Assessment

Two confirmed findings: HIGH (self-documented raw-World normal transform) and MEDIUM (shared fog-formula bug).
