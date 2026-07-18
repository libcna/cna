# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/alpha_test3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/alpha_test3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: AlphaTestEffect fragment stage (alpha compare/discard)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Samples the texture, applies the AlphaFunction/ReferenceAlpha test, discards on failure.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
Alpha-test encoding (`alphaTol > 0` -> equality-with-tolerance; else -> less-than, weight-based discard) independently verified identical to the same convention already confirmed correct in EasyGL/Vulkan/WebGPU/D3DCommon's own alpha-test shaders — consistent cross-backend implementation, not independently (and possibly divergently) reinvented.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Shared, byte-for-byte-consistent alpha-test encoding across every backend already reviewed in this audit.

## Missing or Weak Tests

No dedicated test found for the discard branch specifically on this backend.

## Positive Findings

Consistent, correct alpha-test convention shared across the whole project.

## Final Assessment

No defects found.
