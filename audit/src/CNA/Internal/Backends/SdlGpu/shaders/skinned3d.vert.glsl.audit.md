# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/skinned3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/skinned3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: SkinnedEffect vertex stage (VertexPositionNormalTextureSkinned)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Bone-skins Position/Normal (matching FNA's real weight-count gating), transforms by MVP. Fragment stage IS lit_textured3d.frag.glsl, reused unchanged.

## Executive Verdict

**Needs attention — shares the confirmed skinned-normal-transform bug (complete omission); positive finding on EmissiveColor forwarding.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed (see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** line 75, `fragNormal = normalize(mat3(skinMat) * inNormal);` — the normal is transformed by the bone-skin matrix alone; `world` never enters the calculation at all (compare `lit_textured3d.vert.glsl`'s correct `transpose(inverse(mat3(world)))`, in the same directory). The file's own comment self-documents this as "mirrors VulkanGraphicsBackend's own skinned3d.vert.glsl exactly... an established simplification already shared by every backend implementing SkinnedEffect" — direct, explicit acknowledgment of the cross-backend porting chain this audit has traced elsewhere (Vulkan -> SdlGpu, alongside the separately-confirmed EasyGL -> WebGPU and Vulkan -> D3DCommon chains).

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

### Positive: EmissiveColor forwarding
**Confirmed (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): this shader's fragment stage IS `lit_textured3d.frag.glsl`, reused unchanged** (confirmed via this file's own comment, cross-checked against the C++ `FillSkinnedLightUniforms()`/`FillLitLightUniforms()` call chain) — meaning `EmissiveColor` IS correctly forwarded for `SkinnedEffect` on this backend, unlike D3D11/D3D12 (whose separate cbuffer struct drops it) and Vulkan (which drops both Ambient and Emissive).

## Detailed Findings

**F1 (HIGH):** complete omission of world-space normal-matrix contribution, line 75.
**F2 (HIGH):** no fog implementation.

## Cross-File Observations

Explicit, self-documented confirmation of the Vulkan->SdlGpu porting chain for F1; the correct fragment-shader-reuse architecture is what makes the positive EmissiveColor-forwarding finding true.

## Missing or Weak Tests

No dedicated rotated-World `SkinnedEffect` lighting test found (masked by `World=Identity` in every current test, per the established pattern).

## Positive Findings

Correctly ports FNA's bone-weight-count gating exactly; the reused-fragment-shader architecture choice structurally prevents the EmissiveColor-dropping bug other backends' separate-struct designs hit.

## Final Assessment

Two confirmed HIGH findings (F1 normal-transform, F2 fog absence); one genuine positive finding (EmissiveColor correctly forwarded, unlike 3 other backends).
