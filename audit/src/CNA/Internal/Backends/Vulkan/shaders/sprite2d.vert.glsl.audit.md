# Audit: src/CNA/Internal/Backends/Vulkan/shaders/sprite2d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/sprite2d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: SpriteBatch 2D vertex shader — pixel-space to NDC projection for 2D sprite rendering.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

SpriteBatch 2D vertex shader — pixel-space to NDC projection for 2D sprite rendering.

## Executive Verdict

Healthy — confirmed non-bug omission of the Y-flip pattern.

## Checklist Results

### Behavioral correctness / FNA parity
**Correctly lacks the 3D Y-flip convention, for a legitimate, self-contained reason** (verified, not just assumed): computes NDC directly from pixel-space (`(inPos / viewportSize) * 2.0 - 1.0`) with its own explicit "(0,0) top-left, Y-down to match XNA" comment — a fundamentally different mapping than every 3D shader's `wvp`-transform-then-flip pattern, needing no post-hoc correction. This is the one file in the "files without the Y-flip" grep sweep result confirmed to be a non-bug, contrasting with `env_map3d`/`pbr3d`/`pbr3d_skinned`/`instanced3d`'s confirmed genuine omissions.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**Correctly lacks the 3D Y-flip convention, for a legitimate, self-contained reason** (verified, not just assumed): computes NDC directly from pixel-space (`(inPos / viewportSize) * 2.0 - 1.0`) with its own explicit "(0,0) top-left, Y-down to match XNA" comment — a fundamentally different mapping than every 3D shader's `wvp`-transform-then-flip pattern, needing no post-hoc correction. This is the one file in the "files without the Y-flip" grep sweep result confirmed to be a non-bug, contrasting with `env_map3d`/`pbr3d`/`pbr3d_skinned`/`instanced3d`'s confirmed genuine omissions.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Correct, legitimate, self-contained pixel-to-NDC mapping — the one non-bug instance in the missing-Y-flip grep sweep.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
