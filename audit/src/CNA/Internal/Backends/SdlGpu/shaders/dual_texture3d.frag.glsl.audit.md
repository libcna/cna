# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/dual_texture3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/dual_texture3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: DualTextureEffect fragment stage
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Samples both textures, doubles the first sample's RGB (the standard lightmap-multiply-by-2 convention), multiplies both with fragTint.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
`tex1.rgb *= 2.0` correctly matches FNA's real `DualTextureEffect` convention, independently verified consistent with this exact formula already confirmed correct across EasyGL/Vulkan/Bgfx/D3DCommon in this audit — no divergence on this backend.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Formula matches every other backend's own `DualTextureEffect` implementation exactly.

## Missing or Weak Tests

No dedicated D3D-family test found for this specific file (the cross-backend `dualtextureeffect_vertexcolor_test.cpp` registers on EasyGL/Vulkan/Bgfx, not SdlGpu).

## Positive Findings

Correct, FNA-accurate lightmap-multiply convention shared consistently across the whole project.

## Final Assessment

No defects found.
