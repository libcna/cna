# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d_vertexlit.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d_vertexlit.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: BasicEffect vertex stage, per-vertex-lit path (PreferPerPixelLighting == false, XNA's real default)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: BasicEffect.fx (lit, per-vertex branch) / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Per-vertex-lit sibling of `lit_textured3d.vert.hlsl`: computes full Blinn-Phong lighting (ambient+diffuse+specular, emissive added unscaled) in the vertex stage instead of the fragment stage, for XNA's actual default (unlit-per-pixel) BasicEffect behavior.

## Executive Verdict

**Mostly healthy — correct normal transform and lighting formula; shares the systemic fog bug.**

## Checklist Results

### API / XNA parity
**Correct** `InverseTranspose3x3(World)` normal transform (line 78-79). Lighting formula (line 89-98) correctly matches FNA's `Lighting.fxh ComputeLights()`: `AmbientColor + sum(NdotL*LightDiffuse)`, specular computed via the half-vector Blinn-Phong term gated by the same `zeroL`"does this light face the surface" term as diffuse, and `EmissiveColorPad` added **unscaled** after the `lightSum*DiffuseColor` multiply (line 98) — correctly matching FNA's convention, i.e. this file does NOT share the `EnvironmentMapEffect`-family emissive-remultiply bug found elsewhere in this audit.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 101: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 101.

## Cross-File Observations

Its fragment-shader sibling `lit_textured3d_vertexlit.frag.hlsl` correctly just forwards these already-computed `LitRGB`/`SpecularRGB` values rather than recomputing lighting — verified the division of labor is consistent and doesn't double-apply anything.

## Missing or Weak Tests

No dedicated D3D11/D3D12 per-vertex-lighting-path test found — this is XNA's actual default lighting mode (`PreferPerPixelLighting=false`) yet appears to have no dedicated coverage on this backend.

## Positive Findings

Correct, FNA-accurate emissive-unscaled convention (a place where a mistake would easily mirror the `EnvironmentMapEffect` bug elsewhere in this audit, but doesn't here).

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug); the lighting formula itself is correct.
