# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/dual_texture3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/dual_texture3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: DualTextureEffect fragment stage
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: DualTextureEffect.fx
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Samples both textures, doubles the first sample's RGB (the standard DualTextureEffect lightmap-multiply-by-2 convention), multiplies both samples together with Tint, mixes toward FogColor.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
`tex1.rgb *= 2.0` (line 40) correctly matches FNA's `DualTextureEffect`/`DualTexture.fx` convention (the second texture acts as a 2x lightmap multiplier), independently verified against the known DualTextureEffect formula already confirmed correct elsewhere in this audit (`dualtextureeffect_vertexcolor_test.cpp`'s report).

### Logic
Fog mix correct in isolation.

## Detailed Findings

None in this file — see `dual_texture3d.vert.hlsl.audit.md` for the upstream fog-formula defect.

## Cross-File Observations

Formula matches the same DualTextureEffect convention independently confirmed correct across EasyGL/Vulkan/Bgfx elsewhere in this audit — no D3D-specific divergence.

## Missing or Weak Tests

No dedicated D3D11/D3D12-specific `DualTextureEffect` test found (the cross-backend `dualtextureeffect_vertexcolor_test.cpp` registers on EasyGL/Vulkan/Bgfx, not D3D11/D3D12).

## Positive Findings

Correct, FNA-accurate lightmap-multiply convention.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
