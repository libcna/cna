# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/textured3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/textured3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: BasicEffect fragment stage, VertexPositionTexture/VertexPositionColorTexture (unlit, textured)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Samples the texture (or substitutes opaque white when TextureEnabled is false), multiplies by fragTint.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
`textureEnabled` ternary substitution correctly matches FNA's "no texture bound" behavior.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Correctly shared by both `textured3d.vert.glsl` (stride 20) and `colored_textured3d.vert.glsl` (stride 24) — verified both produce a compatible `fragUV`/`fragTint` interface.

## Missing or Weak Tests

No dedicated test found for this specific file.

## Positive Findings

Correct, minimal, honestly-documented sharing between two vertex-shader variants.

## Final Assessment

No defects found.
