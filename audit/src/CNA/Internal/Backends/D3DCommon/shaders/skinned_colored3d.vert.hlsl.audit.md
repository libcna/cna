# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/skinned_colored3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned_colored3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SkinnedEffect vertex stage, VertexColorEnabled + per-pixel-lit path (stride-56 variant)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SkinnedEffect.fx / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

`skinned3d.vert.hlsl`'s sibling for the stride-56 (VertexPositionNormalTextureSkinned + Color) vertex layout, adding per-vertex Color pass-through. Ported from `EasyGLGraphicsBackend::EnsureSkinnedProgram()`'s vertex stage.

## Executive Verdict

**Needs attention — shares both the skinned-normal-transform bug and the systemic fog bug.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH — shares the identical complete-omission normal-transform bug** as `skinned3d.vert.hlsl`. Line 73: `output.Normal = normalize(mul(input.Normal, (float3x3)skinMat));` — no `World` composed in.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 78: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (HIGH):** complete omission of world-space normal-matrix contribution, line 73.
**F2 (MEDIUM):** mirrored fog formula, line 78.

## Cross-File Observations

Identical bug shape and root cause to `skinned3d.vert.hlsl`/`skinned3d_vertexlit.vert.hlsl`/`skinned_colored3d_vertexlit.vert.hlsl` — 4 of 4 non-PBR D3DCommon skinned vertex shaders share this exact defect, with zero exceptions (see `AUDIT_CROSS_CUTTING_FINDINGS.md`).

## Missing or Weak Tests

No dedicated D3D11/D3D12 rotated-World `SkinnedEffect` VertexColorEnabled test found.

## Positive Findings

Correct per-vertex Color pass-through (`output.Color = input.Color`, line 76), consistent with the sibling `skinned_colored3d.frag.hlsl`'s own correct `VertexColorEnabled` gating.

## Final Assessment

Two confirmed findings: HIGH (missing world-space normal transform) and MEDIUM (shared fog-formula bug).
