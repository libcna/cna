# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/pbr3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/pbr3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: NOXNA — PbrEffect (metallic-roughness PBR), unskinned
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Transforms position, computes world-space normal via inverse-transpose, tangent via plain World (correct, documented simplification), world position.

## Executive Verdict

**Needs attention — correct normal/tangent transform (control-group member); shares the backend-wide fog absence.**

## Checklist Results

### API / XNA parity
**Correct** `transpose(inverse(mat3(world)))` normal transform and correctly-documented plain-`mat3(world)` tangent transform (matching EasyGL's own established PbrEffect simplification) — a further control-group member alongside `lit_textured3d.vert.glsl`/`env_map3d.vert.glsl`.

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

## Detailed Findings

**F1 (HIGH):** no fog implementation.

## Cross-File Observations

Correctly reuses `lit_textured3d.vert.glsl`'s PC/LitLightParams uniform layouts rather than declaring redundant ones — efficient design choice, independently verified the field semantics genuinely match (PbrEffect's Diffuse/Ambient/Light0 fields map to the exact same slots BasicEffect already uses).

## Missing or Weak Tests

No dedicated fog test found.

## Positive Findings

Efficient, correct uniform-layout reuse; correct normal/tangent transform.

## Final Assessment

One HIGH finding (backend-wide fog absence); otherwise correct.
