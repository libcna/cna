# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d_vertexlit.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d_vertexlit.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SkinnedEffect fragment stage, per-vertex-lit path (XNA's real default lighting mode)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SkinnedEffect.fx / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Consumes the already-computed `LitRGB`/`SpecularRGB`/`Alpha` varyings from `skinned3d_vertexlit.vert.hlsl` instead of recomputing lighting per pixel; texture sample and fog mix otherwise unchanged.

## Executive Verdict

**Healthy for its own scope — correctly forwards vertex-computed lighting; the EmissiveColor omission and normal-transform bug both live upstream in the vertex shader for this per-vertex-lit variant.**

## Checklist Results

### API / XNA parity
Correctly forwards `LitRGB`/`SpecularRGB`/`Alpha` rather than recomputing lighting; specular-after-texture-multiply-scaled-by-alpha discipline (line 50) matches the per-pixel sibling.

### Cross-reference
Since lighting (including the ambient+ diffuse combination and the missing-emissive gap) is fully computed in `skinned3d_vertexlit.vert.hlsl`, not here, this fragment shader itself has no independent EmissiveColor-cbuffer gap to report — see that vertex shader's report for the full lighting-input picture.

## Detailed Findings

None new in this file — see `skinned3d_vertexlit.vert.hlsl.audit.md` for where the actual lighting computation (and its missing-EmissiveColor gap) lives for this per-vertex-lit variant.

## Cross-File Observations

Correctly mirrors `skinned3d.frag.hlsl`'s texture/specular/fog handling structure.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Clean, correct division of labor — this file has nothing of its own to get wrong once the vertex stage's output is taken as given.

## Final Assessment

No defects introduced in this file; inherits the vertex-shader's normal-transform, missing-EmissiveColor, and fog-formula issues as a pass-through consumer.
