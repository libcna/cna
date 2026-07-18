# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/pbr_skinned3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/pbr_skinned3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: NOXNA — SkinnedPbrEffect (metallic-roughness PBR + bone skinning)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Bone-skins Position/Normal/Tangent (matching FNA's real weight-count gating), then follows pbr3d's own transform pattern.

## Executive Verdict

**Needs attention — shares the skinned-normal-transform bug's narrower (raw-World) variant.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed (see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** line 76, `fragNormal = normalize(mat3(lp.world) * (skinNormalMat * inNormal));` — uses the raw `mat3(world)` for the outer transform, NOT the inverse-transpose `pbr3d.vert.glsl`'s own unskinned sibling correctly uses. The file's own comment self-documents this exactly, explicitly cross-referencing `EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()`'s identical simplification — the same narrower bug variant already confirmed in EasyGL/D3D11/D3D12/D3DCommon's own `pbr_skinned3d` shaders. 6th confirmed backend for this specific variant.

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

## Detailed Findings

**F1 (HIGH):** raw-World-instead-of-inverse-transpose normal transform, line 76.
**F2 (HIGH):** no fog implementation.

## Cross-File Observations

Directly comparable to `pbr3d.vert.glsl` (gets this right) and `skinned3d.vert.glsl` (shares the complete-omission variant of the same underlying mistake) — the same 3-way comparison already established for D3DCommon.

## Missing or Weak Tests

No dedicated non-uniform-scale-World test found that would surface F1 (masked by every current test's use of `World=Identity`, per this audit's established pattern for this whole bug family).

## Positive Findings

Correctly ports FNA's bone-weight-count gating exactly.

## Final Assessment

Two confirmed findings: HIGH (self-documented raw-World normal transform, F1) and HIGH (fog absence, F2, shared backend-wide).
