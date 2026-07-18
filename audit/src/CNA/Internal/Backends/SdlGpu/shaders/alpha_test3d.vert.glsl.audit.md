# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/alpha_test3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/alpha_test3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: AlphaTestEffect vertex stage, strides 20/32 (no vertex color)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Transforms position, forwards UV, sets fragTint = DiffuseColor.

## Executive Verdict

**Needs attention — correct core logic; shares the backend-wide fog absence.**

## Checklist Results

### API / XNA parity
Correctly documents stride-agnostic design (Position+UV only, works for both stride 20 and 32 vertex layouts via the pipeline's own vertex-input-state configuration).

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

## Detailed Findings

**F1 (HIGH):** no fog implementation.

## Cross-File Observations

Shares `alpha_test3d.frag.glsl` with `alpha_test_colored3d.vert.glsl`.

## Missing or Weak Tests

No dedicated fog test found.

## Positive Findings

Clear documentation of which strides this variant serves vs. its stride-24 sibling.

## Final Assessment

One HIGH finding (backend-wide fog absence); otherwise correct.
