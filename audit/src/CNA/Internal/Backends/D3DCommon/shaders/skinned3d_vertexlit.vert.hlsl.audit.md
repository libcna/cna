# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d_vertexlit.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d_vertexlit.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SkinnedEffect vertex stage, per-vertex-lit path (PreferPerPixelLighting == false, XNA's real default)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SkinnedEffect.fx / Lighting.fxh (per-vertex branch)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Per-vertex-lit sibling of `skinned3d.vert.hlsl`: computes full Blinn-Phong lighting in the vertex stage using a locally-computed normal `N`, for XNA's actual default (per-vertex) SkinnedEffect lighting mode.

## Executive Verdict

**Needs attention — shares both the skinned-normal-transform bug and the systemic fog bug.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH — shares the identical complete-omission normal-transform bug.** Line 75: `float3 N = normalize(mul(input.Normal, (float3x3)skinMat));` — same as `skinned3d.vert.hlsl`, just assigned to a local variable (`N`) instead of an output field, since lighting is computed here rather than in the fragment shader; `World` still never enters the calculation. Since this is XNA's actual *default* SkinnedEffect lighting mode (`PreferPerPixelLighting=false`), this variant of the bug is arguably the more practically-impactful of the two.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 96: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (HIGH):** complete omission of world-space normal-matrix contribution (local variable `N`), line 75.
**F2 (MEDIUM):** mirrored fog formula, line 96.
**F3 (MEDIUM, lighting formula itself independently verified correct):** ambient+diffuse+specular+emissive combination (lines 82-92) correctly matches FNA's `Lighting.fxh` convention — the *only* wrong input is the unrotated normal `N` from F1, not the lighting math structure itself.

## Cross-File Observations

Directly comparable to `skinned3d.vert.hlsl` (per-pixel sibling, identical bug) and `lit_textured3d_vertexlit.vert.hlsl` (unskinned per-vertex-lit sibling, correct normal transform) — this 3-way comparison is the cleanest evidence in the audit that the defect is skinning-specific.

## Missing or Weak Tests

No dedicated D3D11/D3D12 per-vertex-lit `SkinnedEffect` rotated-World test found.

## Positive Findings

Correctly ports FNA's real default lighting mode (per-vertex, not per-pixel) as its own dedicated shader variant, matching the project's own stated Task 1106/1107 XNA-parity goal.

## Final Assessment

Two confirmed findings: HIGH (missing world-space normal transform, same root cause as `skinned3d.vert.hlsl`) and MEDIUM (shared fog-formula bug).
