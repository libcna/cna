# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/dual_texture3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/dual_texture3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: DualTextureEffect vertex stage
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: DualTextureEffect.fx / Lighting.fxh (fog only)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Transforms position, forwards UV, sets `Tint = DiffuseColor`, computes fog factor. Its own dedicated vertex shader (rather than reusing `textured3d`'s) exists specifically to avoid a fog-constant-buffer register collision, per its own header comment.

## Executive Verdict

**Needs attention (shares the systemic fog bug).**

## Checklist Results

### API / XNA parity
Correct MVP transform and Tint assignment.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 56: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 56.

## Cross-File Observations

Shares the fog bug; the header comment's explanation of why this needed its own vertex shader (register collision with `textured3d`'s fog cbuffer) is accurate and independently verified against `textured3d.vert.hlsl`.

## Missing or Weak Tests

No dedicated D3D11/D3D12 fog test found.

## Positive Findings

Clear historical rationale in the header comment for a design choice that could otherwise look redundant.

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug), otherwise correct.
