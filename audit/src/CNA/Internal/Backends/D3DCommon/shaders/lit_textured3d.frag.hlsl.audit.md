# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: BasicEffect fragment stage, per-pixel-lit path (PreferPerPixelLighting == true)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: BasicEffect.fx (lit branch) / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Full per-pixel Blinn-Phong lighting: ambient+diffuse+specular with correct light-facing gating, emissive added unscaled after the diffuse*tint multiply, specular added after the texture multiply scaled by the resulting alpha (FNA's `AddSpecular` macro convention), then fog mix.

## Executive Verdict

**Healthy — correct, FNA-accurate lighting formula.**

## Checklist Results

### API / XNA parity
**Independently verified correct against FNA's `Lighting.fxh ComputeLights()`**: ambient+diffuse summed first (line 67-68), then multiplied by `Tint` (line 79) with `EmissiveColorPad` added **unscaled** afterward (matching FNA's `result.Diffuse = sum*DiffuseColor + EmissiveColor` exactly — explicitly commented as such at line 77-78). Specular is added after the texture*diffuse multiply, scaled by the resulting alpha (line 83), matching FNA's `AddSpecular` macro, not the texture directly. This is the correct emissive convention `env_map3d.frag.hlsl` (in the same directory) gets wrong — direct proof this author knew the right approach.

### Logic
Half-vector Blinn-Phong specular (lines 72-76) correctly gated by the same `zeroL` "does this light face the surface" term used for diffuse, applied once to the summed per-light contribution as FNA does, not per-light. Fog mix correct in isolation.

## Detailed Findings

None.

## Cross-File Observations

Its correct emissive-unscaled convention is the control-group evidence proving `env_map3d.frag.hlsl`'s emissive-remultiply bug is an isolated `EnvironmentMapEffect`-specific mistake, not a directory-wide misunderstanding — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated D3D11/D3D12 per-pixel-lit `BasicEffect` specular/emissive test found.

## Positive Findings

Textbook-correct, well-commented re-implementation of FNA's `Lighting.fxh` per-pixel path, explicitly citing the FNA convention it matches at the exact line that matters (emissive-unscaled).

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
