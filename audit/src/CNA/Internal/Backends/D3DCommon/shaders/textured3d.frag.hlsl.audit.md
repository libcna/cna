# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/textured3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/textured3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: BasicEffect fragment stage, VertexPositionTexture (unlit, textured)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: BasicEffect.fx (textured, unlit branch)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Samples the texture (or substitutes opaque white when `TextureEnabled` is false), multiplies by Tint, mixes toward FogColor.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
`TextureEnabled` ternary substitution correct, matching FNA's "no texture bound" behavior.

### Logic
Fog mix correct in isolation.

## Detailed Findings

None in this file — see `textured3d.vert.hlsl.audit.md` for the upstream fog-formula defect.

## Cross-File Observations

Identical `TextureEnabled` ternary pattern to `colored_textured3d.frag.hlsl` — verified consistent.

## Missing or Weak Tests

No dedicated test found for this specific file.

## Positive Findings

Correct, minimal implementation.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
