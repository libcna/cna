# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/colored_textured3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/colored_textured3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: BasicEffect vertex stage, VertexPositionColorTexture (unlit, textured, VertexColorEnabled)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Transforms position, forwards UV, mixes vertex color with DiffuseColor.

## Executive Verdict

**Needs attention — correct core logic; shares the backend-wide fog absence.**

## Checklist Results

### API / XNA parity
Correct VertexColorEnabled gating, matching `colored3d.vert.glsl`'s established convention.

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

## Detailed Findings

**F1 (HIGH):** no fog implementation.

## Cross-File Observations

Shares the fog-absence finding with all other SdlGpu shaders.

## Missing or Weak Tests

No dedicated fog test found.

## Positive Findings

Consistent VertexColorEnabled convention with its siblings.

## Final Assessment

One HIGH finding (backend-wide fog absence); otherwise correct.
