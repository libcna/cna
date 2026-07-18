# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/pbr3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/pbr3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: NOXNA — PbrEffect (metallic-roughness PBR), a CNA extension beyond stock XNA/FNA
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: N/A — no direct FNA/XNA equivalent (glTF 2.0-derived PBR pipeline, unskinned)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Transforms position, computes world-space Normal via `InverseTranspose3x3(World)` and Tangent via plain World (correctly, per its own documented convention), world position, and fog factor. HLSL port of `EasyGLGraphicsBackend::EnsurePbrProgram()`'s vertex stage.

## Executive Verdict

**Healthy — correct normal transform, correctly-documented tangent convention; shares the systemic fog bug.**

## Checklist Results

### API / XNA parity
N/A (non-XNA extension) — internal consistency with its own `pbr3d.frag.hlsl` and EasyGL reference implementation is what matters here, and both are verified consistent.

### Logic
**Correct** `InverseTranspose3x3(World)` normal transform (line 64-65). Tangent transform deliberately uses plain `(float3x3)World` rather than the inverse-transpose (line 71) — its own comment explains this is correct for uniform-scale World transforms and matches the EasyGL reference exactly, a documented, reasonable simplification shared by most real-time engines rather than an oversight (verified this reasoning is sound: tangent, as a surface-following direction rather than a surface-normal, is not subject to the same non-uniform-scale distortion a normal is).

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 81: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 81 (comment explicitly says this file "follows the D3D11 backend's existing convention, same 'don't invent a new one' discipline" — an honest acknowledgment that consistency was prioritized over re-deriving from FNA, which is exactly how this bug propagated).

## Cross-File Observations

Part of the control group (with `lit_textured3d.vert.hlsl`/`lit_textured3d_vertexlit.vert.hlsl`) proving the skinned-shader normal-transform omission in `pbr_skinned3d.vert.hlsl` is a skinning-specific regression, not a general error — this file gets the *unskinned* case right.

## Missing or Weak Tests

No dedicated D3D11/D3D12 PbrEffect fog test found.

## Positive Findings

Well-reasoned, correctly-documented tangent-transform simplification; correct normal transform.

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug), otherwise correct.
