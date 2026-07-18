# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d_vertexlit.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d_vertexlit.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: BasicEffect fragment stage, per-vertex-lit path (XNA's real default lighting mode)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: BasicEffect.fx (per-vertex branch) / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Consumes the already-computed `LitRGB`/`SpecularRGB` varyings from `lit_textured3d_vertexlit.vert.hlsl` instead of recomputing Blinn-Phong per pixel; texture sample and fog mix are otherwise unchanged from the per-pixel sibling.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
Correctly forwards vertex-computed lighting rather than double-computing it; specular-after-texture-multiply-scaled-by-alpha discipline (line 54) matches `lit_textured3d.frag.hlsl`'s own convention exactly.

### Logic
Fog mix correct in isolation.

## Detailed Findings

None.

## Cross-File Observations

Correctly mirrors `lit_textured3d.frag.hlsl`'s own lit branch, as its own comment states and this audit independently verified.

## Missing or Weak Tests

No dedicated test found for XNA's actual default (per-vertex) BasicEffect lighting path on this backend.

## Positive Findings

Clean division of labor between vertex and fragment stage, with the fragment shader correctly staying "dumb" (no lighting recomputation).

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
