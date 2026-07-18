# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/colored_textured3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/colored_textured3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: BasicEffect vertex stage, VertexPositionColorTexture (textured, VertexColorEnabled, unlit branch)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: BasicEffect.fx (textured, unlit branch) / Lighting.fxh (fog only)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Transforms position, forwards UV, mixes vertex color with DiffuseColor, computes fog factor.

## Executive Verdict

**Needs attention (shares the systemic fog bug).**

## Checklist Results

### API / XNA parity
VertexColorEnabled gating correct; forwards UV unchanged.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 54: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 54.

## Cross-File Observations

Shares the fog bug; its own header comment correctly documents that `textured3d.vert.hlsl` shares its fog constant-buffer register convention (b1), a genuinely useful cross-reference.

## Missing or Weak Tests

No dedicated D3D11/D3D12 fog test found.

## Positive Findings

Clean division of PerDraw (b0) vs FogParams (b1) constant buffers with an explicit rationale comment.

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug), otherwise correct.
