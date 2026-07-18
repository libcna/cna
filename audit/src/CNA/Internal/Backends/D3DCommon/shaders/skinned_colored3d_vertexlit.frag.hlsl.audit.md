# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/skinned_colored3d_vertexlit.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned_colored3d_vertexlit.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SkinnedEffect fragment stage, VertexColorEnabled + per-vertex-lit path (stride-56 variant, XNA's real default lighting mode)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SkinnedEffect.fx / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

`skinned3d_vertexlit.frag.hlsl`'s sibling adding a per-vertex Color varying, consumed from `skinned_colored3d_vertexlit.vert.hlsl`. Shares the "multiply after specular add" discipline documented in `skinned_colored3d.frag.hlsl`'s own comment.

## Executive Verdict

**Healthy for its own scope — correctly forwards vertex-computed lighting and applies the correct vertex-color-modulation ordering; the EmissiveColor omission and normal-transform bug both live upstream.**

## Checklist Results

### API / XNA parity
Vertex color modulates the whole combined output after the specular add (`outColor.rgb *= vc.rgb`, line 58, after line 57's specular add) — independently verified consistent with `skinned_colored3d.frag.hlsl`'s own documented discipline.

### Cross-reference
Lighting (including the missing-emissive gap) is fully computed in `skinned_colored3d_vertexlit.vert.hlsl`, not here — see that file's report.

## Detailed Findings

None new in this file.

## Cross-File Observations

Correctly mirrors `skinned_colored3d.frag.hlsl`'s vertex-color-modulation-after-specular ordering for the per-vertex-lit variant.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Correct, consistent ordering discipline across both the per-pixel and per-vertex-lit VertexColorEnabled variants.

## Final Assessment

No defects introduced in this file; inherits the vertex-shader's normal-transform, missing-EmissiveColor, and fog-formula issues as a pass-through consumer.
