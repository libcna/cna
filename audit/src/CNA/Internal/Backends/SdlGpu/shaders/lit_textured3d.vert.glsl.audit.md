# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/lit_textured3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/lit_textured3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: BasicEffect vertex stage, per-pixel-lit path (VertexPositionNormalTexture)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Transforms position, computes world-space normal via inverse-transpose, world position.

## Executive Verdict

**Needs attention — correct normal transform (control-group member); shares the backend-wide fog absence.**

## Checklist Results

### API / XNA parity
**Correct** `transpose(inverse(mat3(world)))` normal transform (line 48) — this file is part of the control group proving the skinned-shader normal-transform bug (found in `skinned3d.vert.glsl`/`skinned_colored3d.vert.glsl`/`pbr_skinned3d.vert.glsl`, all in this same shard) is a skinning-specific regression, not general unfamiliarity with the correct math.

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

## Detailed Findings

**F1 (HIGH):** no fog implementation.

## Cross-File Observations

Correct normal transform, consistent with `env_map3d.vert.glsl`/`pbr3d.vert.glsl`.

## Missing or Weak Tests

No dedicated fog test found.

## Positive Findings

GLSL-native `inverse()` avoids the CPU-side precomputation workaround WebGPU's WGSL needs — cleaner code, matches Vulkan's own equivalent convention.

## Final Assessment

One HIGH finding (backend-wide fog absence); the normal transform this file owns is correct.
