# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/sprite2d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/sprite2d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: SpriteBatch vertex stage (2D pixel-space-to-NDC transform)
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Maps pixel-space position to NDC, forwards UV/Color.

## Executive Verdict

**Healthy — correctly implements a genuinely necessary, empirically-verified Y-flip.**

## Checklist Results

### API / XNA parity
**Independently verified correct**: the comment's claim that "SDL_gpu's Vulkan driver flips clip-space Y internally" (requiring this shader to negate Y again to cancel it out and preserve pixel-space Y-down semantics) is explicitly described as empirically confirmed ("without this negation, a texture's top row renders at the bottom of the sprite") — a genuine, tested finding, not an assumption. Notably, this is the OPPOSITE Y-flip direction from the 3D shaders in this same backend (which need NO flip, per `colored3d.vert.glsl`'s own comment) — mirrors the identical asymmetry already independently confirmed in D3DCommon's `sprite2d.vert.hlsl` vs. its own 3D shaders, for an analogous reason.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Same 2D-vs-3D Y-flip asymmetry pattern already confirmed in D3DCommon's own `sprite2d.vert.hlsl` — consistent cross-backend documentation of a genuinely necessary, non-obvious platform quirk.

## Missing or Weak Tests

No dedicated test found specifically isolating this Y-flip (though `examples-tests-sdlgpu`'s sprite tests would fail visually if it were wrong, and weren't flagged as failing in that batch's audit).

## Positive Findings

A rare example of a Y-flip claim backed by an explicit account of empirical verification (observed upside-down rendering without the fix), not just asserted.

## Final Assessment

No defects found.
