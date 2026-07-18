# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/pbr_skinned3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/pbr_skinned3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: NOXNA — SkinnedPbrEffect (metallic-roughness PBR + bone skinning), a CNA extension
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: N/A
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Identical BRDF math to `pbr3d.frag.hlsl`; not itself skinning-aware (skinning only affects the vertex stage, already resolved by the time this pixel shader runs).

## Executive Verdict

**Healthy — correct, and correctly carries EmissiveColor unlike its non-PBR SkinnedEffect siblings.**

## Checklist Results

### API / XNA parity
N/A (non-XNA extension).

### Logic
Identical, independently-verified-correct BRDF to `pbr3d.frag.hlsl`. **Positive finding**: unlike `skinned3d.frag.hlsl`/`skinned3d_vertexlit.frag.hlsl`/`skinned_colored3d.frag.hlsl`/`skinned_colored3d_vertexlit.frag.hlsl` (which lack any `EmissiveColor` cbuffer field at all — see `AUDIT_CROSS_CUTTING_FINDINGS.md`), this file's `PerDraw` cbuffer correctly carries `EmissiveRoughness.xyz` through unchanged from its unskinned `pbr3d.frag.hlsl` sibling, because `SkinnedPbrEffect`'s parameter cbuffer was copied wholesale from `PbrEffect`'s rather than rebuilt from `SkinnedEffect`'s (which is where the missing-EmissiveColor omission actually originates).

## Detailed Findings

None.

## Cross-File Observations

Its own comment ("PbrLights lives at register(b2) here instead of (b1)") correctly documents a register-numbering difference from `pbr3d.frag.hlsl`, independently verified accurate.

## Missing or Weak Tests

No dedicated D3D11/D3D12 `SkinnedPbrEffect` BRDF/emissive test found.

## Positive Findings

Correctly avoids the missing-`EmissiveColor` defect its non-PBR `SkinnedEffect` siblings share, purely as a side effect of which unskinned sibling its cbuffer layout was copied from — worth noting as an accidental rather than deliberate correctness, but correct nonetheless.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug and the vertex-shader raw-World normal-transform bug (both in `pbr_skinned3d.vert.hlsl`) as a pass-through consumer only.
