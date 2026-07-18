# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: EnvironmentMapEffect vertex stage
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Transforms position, computes world-space normal via the correct inverse-transpose, world position.

## Executive Verdict

**Needs attention — correct normal transform (part of the unskinned control group); shares the backend-wide fog absence.**

## Checklist Results

### API / XNA parity
**Correct** `transpose(inverse(mat3(world)))` normal transform (line 38) — part of this project's control group (with `lit_textured3d.vert.glsl`/`pbr3d.vert.glsl`) proving the skinned-shader normal-transform bug is a skinning-specific regression, not a general error on this backend.

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

## Detailed Findings

**F1 (HIGH):** no fog implementation.

## Cross-File Observations

Correct normal transform, consistent with `lit_textured3d.vert.glsl`'s own established convention.

## Missing or Weak Tests

No dedicated fog test found; `sdlgpu_envmap_test.cpp` (already audited) exercises this shader's other aspects and correctly found the emissive-remultiply bug in its fragment sibling.

## Positive Findings

Correct, GLSL-native `inverse()`-based normal-transform, no CPU-side precomputation workaround needed (unlike WebGPU's WGSL-forced equivalent).

## Final Assessment

One HIGH finding (backend-wide fog absence); the normal transform this file owns is correct.
