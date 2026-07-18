# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/skinned_colored3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned_colored3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SkinnedEffect fragment stage, VertexColorEnabled + per-pixel-lit path (stride-56 variant)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SkinnedEffect.fx / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

`skinned3d.frag.hlsl`'s sibling reading a per-vertex Color varying, gated by `VertexColorEnabled`, ported from `EasyGLGraphicsBackend::EnsureSkinnedProgram()`'s fragment stage. Its own comment documents a real historical EasyGL bug (vertex color multiplied into diffuse only, before specular was added, letting an unmodulated specular leak through) and confirms this port avoids it.

## Executive Verdict

**Needs attention — correct vertex-color-modulation-after-specular discipline (verified); lacks an EmissiveColor slot entirely.**

## Checklist Results

### API / XNA parity
**Independently verified correct**: vertex color modulates the whole combined diffuse+specular output (`outColor.rgb *= vc.rgb`, line 75), applied *after* the specular add (line 74) — exactly matching the historical-bug-avoidance this file's own comment describes, and consistent with EasyGL's own already-fixed convention.

### Systematic FNA parity gaps
**MEDIUM — no `EmissiveColor` cbuffer field at all**, unlike its unskinned sibling
`lit_textured3d.frag.hlsl`/`lit_textured3d.vert.hlsl`, which carries an explicit `EmissiveColorPad`
field consumed in the lit formula. `AmbientColor` IS correctly present and consumed (`litRGB = (AmbientColor + lightSum) * DiffuseColor.rgb, line 62`) — only the
`EmissiveColor` half of `SkinnedEffect`'s lighting parameters is dropped. This is a narrower, D3D11/D3D12-specific
variant of the already-confirmed Vulkan-specific defect ("`SkinnedEffect::FillGpuDrawParams()` never sets
`ambientColor`, and Vulkan's skinned shaders never consume `emissiveColor`") — here Ambient transfers correctly but
Emissive does not. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Detailed Findings

**F1 (MEDIUM):** no `EmissiveColor` cbuffer field.

**F2:** consumes the already-buggy `Normal` varying from `skinned_colored3d.vert.hlsl`.

## Cross-File Observations

Its own comment's claim about the historical EasyGL bug it avoids was independently checked against the actual vertex-color-modulation ordering here and confirmed accurate — a genuinely useful, verifiable provenance comment rather than an unverifiable claim.

## Missing or Weak Tests

No dedicated D3D11/D3D12 `SkinnedEffect` VertexColorEnabled+specular-ordering test found.

## Positive Findings

Correctly avoids a real, previously-fixed-elsewhere ordering bug (vertex color applied after specular, not before) — a positive finding independently re-verified, not just repeated from the comment.

## Final Assessment

One confirmed MEDIUM finding (missing EmissiveColor); inherits the vertex-shader normal-transform and fog-formula bugs. The specular-ordering discipline this file's own comment claims is independently confirmed correct.
