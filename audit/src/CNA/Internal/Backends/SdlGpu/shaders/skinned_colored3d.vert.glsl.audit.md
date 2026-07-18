# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/skinned_colored3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/skinned_colored3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: SkinnedEffect vertex stage, stride 56 (VertexColorEnabled variant)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Bone-skins Position/Normal, forwards per-vertex Color unlit (multiplied into the final output in the fragment shader).

## Executive Verdict

**Needs attention — shares the confirmed skinned-normal-transform bug (complete omission).**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed:** line 66, `fragNormal = normalize(mat3(skinMat) * inNormal);` — identical complete-omission pattern to `skinned3d.vert.glsl`, explicitly self-documented as matching that file "exactly."

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

## Detailed Findings

**F1 (HIGH):** complete omission of world-space normal-matrix contribution, line 66.
**F2 (HIGH):** no fog implementation.

## Cross-File Observations

3rd of 3 non-PBR SdlGpu skinned vertex shaders confirmed to share the exact same complete-omission pattern, with zero exceptions.

## Missing or Weak Tests

No dedicated rotated-World test found.

## Positive Findings

Correct per-vertex Color pass-through (forwarded unlit, matching the established "multiply after specular" discipline documented in the paired fragment shader).

## Final Assessment

Two confirmed HIGH findings (F1 normal-transform, F2 fog absence).
