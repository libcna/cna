# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/pbr3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/pbr3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: NOXNA — PbrEffect (metallic-roughness PBR), unskinned
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Full metallic-roughness PBR shading: GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility, Schlick Fresnel (glTF 2.0's reference BRDF), per-pixel normal mapping, ambient/emissive/occlusion.

## Executive Verdict

**Healthy — correct, well-implemented PBR BRDF with a genuine, explicitly-justified robustness improvement.**

## Checklist Results

### API / XNA parity
N/A directly (NOXNA) — internal correctness against the glTF 2.0 reference BRDF is what matters, and every term (D/G/F, the `k=(roughness+1)^2/8` direct-lighting remapping) is a faithful, term-for-term match to the same formula already independently verified correct in `D3DCommon/shaders/pbr3d.frag.hlsl`.

### Logic
`emissive` (line 114) added **unscaled** to `ambient + Lo` (line 116) — correct.

### Robustness
`safeNormalize()`'s NaN-guard is explicitly documented as a deliberate improvement over `EasyGLGraphicsBackend`'s own `PbrLight()` callers, which lack this guard (an OpenGL-driver-specific tolerance this Vulkan-backed environment does not share) — a genuine, well-reasoned engineering decision, not an unexplained deviation.

## Detailed Findings

None.

## Cross-File Observations

Identical BRDF formula to `D3DCommon/shaders/pbr3d.frag.hlsl`, independently cross-verified consistent.

## Missing or Weak Tests

No dedicated test found asserting the exact BRDF output values on this backend (though `sdlgpu_pbreffect_test.cpp`, already audited, independently re-derived and confirmed 3 quads' expected BRDF outputs by hand).

## Positive Findings

Faithful glTF 2.0 BRDF implementation with an explicitly-justified robustness improvement over the project's own reference implementation.

## Final Assessment

No defects found.
