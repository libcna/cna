# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/lit_textured3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/lit_textured3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: BasicEffect fragment stage, per-pixel-lit path
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Full Blinn-Phong lighting: ambient+diffuse+specular with correct light-facing gating, emissive added unscaled, specular added after texture*diffuse scaled by resulting alpha.

## Executive Verdict

**Healthy — correct, FNA-accurate lighting formula, and the shader this backend's SkinnedEffect correctly reuses unchanged.**

## Checklist Results

### API / XNA parity
**Independently verified correct** against FNA's `Lighting.fxh ComputeLights()`: ambient+diffuse summed, multiplied by tint, with `emissiveColor_pad` added **unscaled** afterward (line 71) — correctly matching FNA's convention, the exact place `env_map3d.frag.glsl`'s sibling gets wrong. Specular correctly added after the texture*diffuse multiply, scaled by resulting alpha (FNA's `AddSpecular` convention).

### Cross-file significance
**This shader IS `skinned3d.vert.glsl`'s fragment stage, reused unchanged** (confirmed via that file's own comment) — this is the mechanism by which SdlGpu's SkinnedEffect correctly forwards `EmissiveColor` (unlike D3D11/D3D12/Vulkan) — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### Robustness
`safeNormalize()`'s NaN-guard for a disabled DirectionalLight is explicitly documented as a deliberate improvement over the OpenGL-driver-tolerant EasyGL reference (a real Vulkan-specific undefined-behavior risk this backend proactively avoids).

## Detailed Findings

None.

## Cross-File Observations

Directly responsible for SdlGpu's positive SkinnedEffect-EmissiveColor finding (reused unchanged as `skinned3d`'s fragment stage) — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated D3D11/D3D12-parity per-pixel-lighting specular/emissive test found for this backend specifically.

## Positive Findings

Textbook-correct FNA lighting formula; a real, deliberate, explicitly-justified robustness improvement over the reference implementation's own NaN-vulnerability; the architectural choice to literally reuse this shader for SkinnedEffect is what prevents a whole class of bug other backends hit.

## Final Assessment

No defects found; a genuinely well-engineered, correctly-documented file with real positive cross-backend significance.
