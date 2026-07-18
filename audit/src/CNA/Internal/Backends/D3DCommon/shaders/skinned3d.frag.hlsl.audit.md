# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SkinnedEffect fragment stage, per-pixel-lit path
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SkinnedEffect.fx / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Full per-pixel Blinn-Phong lighting for skinned models: ambient+diffuse+specular using the interpolated (buggy, world-space-omitted) normal from the vertex shader, texture sample, fog mix.

## Executive Verdict

**Needs attention — correct lighting-combination math given its inputs, but lacks an EmissiveColor slot entirely.**

## Checklist Results

### API / XNA parity
Ambient+diffuse summation (`litRGB = (AmbientColor + lightSum) * DiffuseColor.rgb`, line 56) and specular-after-texture-multiply-scaled-by-alpha (line 65) are both correctly structured, matching FNA's `Lighting.fxh` convention for the *terms it has*.

### Systematic FNA parity gaps
**MEDIUM — no `EmissiveColor` cbuffer field at all**, unlike its unskinned sibling
`lit_textured3d.frag.hlsl`/`lit_textured3d.vert.hlsl`, which carries an explicit `EmissiveColorPad`
field consumed in the lit formula. `AmbientColor` IS correctly present and consumed (`litRGB = (AmbientColor + lightSum) * DiffuseColor.rgb, line 56`) — only the
`EmissiveColor` half of `SkinnedEffect`'s lighting parameters is dropped. This is a narrower, D3D11/D3D12-specific
variant of the already-confirmed Vulkan-specific defect ("`SkinnedEffect::FillGpuDrawParams()` never sets
`ambientColor`, and Vulkan's skinned shaders never consume `emissiveColor`") — here Ambient transfers correctly but
Emissive does not. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Detailed Findings

**F1 (MEDIUM):** no `EmissiveColor` cbuffer field, silently dropping `EmissiveColor` for skinned models on this backend.

**F2:** consumes the already-buggy `Normal` varying from `skinned3d.vert.hlsl` (world-space normal-matrix omission) — this file's own lighting math is not itself at fault, but its correctness depends on an already-wrong input.

## Cross-File Observations

Directly comparable to `lit_textured3d.frag.hlsl`'s `EmissiveColorPad` handling — proving the omission here is `SkinnedEffect`-specific, not a directory-wide oversight.

## Missing or Weak Tests

No dedicated D3D11/D3D12 `SkinnedEffect` EmissiveColor test found — consistent with every other backend sharing variants of this gap also lacking such coverage.

## Positive Findings

Correct ambient+diffuse+specular combination structure for the parameters it does carry.

## Final Assessment

One confirmed MEDIUM finding (missing EmissiveColor); inherits the vertex-shader normal-transform and fog-formula bugs as a pass-through consumer.
