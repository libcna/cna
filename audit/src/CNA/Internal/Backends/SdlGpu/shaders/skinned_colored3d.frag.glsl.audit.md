# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/skinned_colored3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/skinned_colored3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: SkinnedEffect fragment stage, stride 56 (VertexColorEnabled variant)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Full Blinn-Phong lighting (identical to lit_textured3d.frag.glsl's), plus a final per-vertex-color multiply applied AFTER specular is added.

## Executive Verdict

**Healthy — correct vertex-color-modulation-after-specular discipline (verified), correctly forwards EmissiveColor.**

## Checklist Results

### API / XNA parity
**Independently verified correct**: vertex color modulates the whole combined output (line 79, `color *= vertexColor`) AFTER the specular add (line 73) — the file's own comment explicitly and accurately cites the real historical bug this ordering avoids (`EasyGLGraphicsBackend`'s own vertex-color skinned shaders hit exactly this once and fixed it the same way). `emissiveColor_pad` correctly added unscaled (line 71) — this shader independently confirms the positive `EmissiveColor`-forwarding finding already established via `skinned3d.frag.glsl`'s reuse of `lit_textured3d.frag.glsl` (this file is this variant's own dedicated fragment shader, not a reuse, but shares the identical, correct lighting formula).

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Confirms the positive EmissiveColor-forwarding finding independently of `skinned3d.frag.glsl`'s fragment-shader-reuse mechanism — this variant has its own dedicated fragment shader and still gets it right.

## Missing or Weak Tests

No dedicated test found specifically for the vertex-color-after-specular ordering on this backend (though `sdlgpu_skinnedeffect_vertexcolor_test.cpp`, already audited, independently confirmed this exact ordering claim correct).

## Positive Findings

Correct, verified specular-ordering discipline with an accurate historical-bug citation; correctly forwards EmissiveColor, a second independent confirmation of the positive cross-backend finding.

## Final Assessment

No defects found.
