# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/colored3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/colored3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: BasicEffect vertex stage, VertexPositionColor (untextured, unlit)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Transforms position by MVP, mixes vertex color with DiffuseColor when VertexColorEnabled.

## Executive Verdict

**Needs attention — correct core logic; shares the backend-wide fog absence.**

## Checklist Results

### API / XNA parity
Correct MVP transform and VertexColorEnabled gating.

### Systematic FNA parity gaps
**HIGH (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): no fog implementation at all** — confirmed no `fogFactor`/
`FogColor`/`fogEnabled`/`fogStart`/`fogEnd` identifier anywhere in this file (nor anywhere else in this
backend's 23 shader files or its C++ implementation). Not a wrong formula — a total absence. This is the
single most significant finding for this backend.

### Architecture
The file's own comment explicitly and honestly discloses the fog omission as deliberately deferred ("same as this codebase's WebGPU backend's own initial 3D vertical slice") — confirmed accurate: WebGPU's own initial 3D slice similarly deferred fog before later adding it (per this audit's earlier WebGPU review). Also confirms (via its own comment) that SDL_gpu's Vulkan driver already presents a D3D/OpenGL-style Y-up clip space, so no Vulkan-specific Y-flip correction is needed here — consistent with the equivalent, independently-verified claim in D3DCommon's own shaders.

## Detailed Findings

**F1 (HIGH):** no fog implementation — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

Shares the fog-absence finding with all 22 other SdlGpu shader files.

## Missing or Weak Tests

No dedicated fog test found on this backend (consistent with fog being entirely unimplemented).

## Positive Findings

Correct, honest disclosure of the fog deferral rather than a silent omission.

## Final Assessment

One HIGH finding (backend-wide fog absence, not unique to this file); otherwise correct.
