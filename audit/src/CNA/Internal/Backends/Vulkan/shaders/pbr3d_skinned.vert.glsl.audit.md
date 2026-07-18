# Audit: src/CNA/Internal/Backends/Vulkan/shaders/pbr3d_skinned.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/pbr3d_skinned.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: SkinnedPbrEffect vertex shader — stride 68 (VertexPositionNormalTangentTextureSkinned), PBR BRDF + bone-palette skinning.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

SkinnedPbrEffect vertex shader — stride 68 (VertexPositionNormalTangentTextureSkinned), PBR BRDF + bone-palette skinning.

## Executive Verdict

Needs attention — NEW instance of the missing-Y-flip bug, confirmed via direct source read, with a factually FALSE justifying comment (more severe than pbr3d.vert.glsl's own incomplete-but-not-false one).

## Checklist Results

### Behavioral correctness / FNA parity
**Confirmed: lacks the Y-flip**, same as `pbr3d.vert.glsl`. **This file's own comment is demonstrably false, not just incomplete**: "Task 899-family precedent: skinned3d.vert.glsl never Y-flips... this shader is a direct extension of that exact skinning transform, so it mirrors that convention exactly." Directly contradicted by `skinned3d.vert.glsl` itself (verified by direct read): line 59, `gl_Position.y = -gl_Position.y; // Vulkan NDC Y is inverted vs OpenGL (matches textured3d.vert.glsl)` — `skinned3d.vert.glsl` DOES flip, with its own comment confirming it's deliberate. Whoever wrote this comment either misremembered or never checked `skinned3d.vert.glsl`'s actual content before citing it as precedent. This is more dangerous than a silent omission: a future maintainer reading the comment would conclude the current behavior is intentional and verified, when it is neither. Also uses `mat3(uWorld)` (not inverse-transpose) for the normal transform post-skinning — explicitly acknowledged in-comment as preserving `EnsurePbrSkinnedProgram()`'s own EasyGL behavior rather than 'correcting' it, per this port's behavior-fidelity requirement (a documented, deliberate fidelity choice, not a new defect).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**Confirmed: lacks the Y-flip**, same as `pbr3d.vert.glsl`. **This file's own comment is demonstrably false, not just incomplete**: "Task 899-family precedent: skinned3d.vert.glsl never Y-flips... this shader is a direct extension of that exact skinning transform, so it mirrors that convention exactly." Directly contradicted by `skinned3d.vert.glsl` itself (verified by direct read): line 59, `gl_Position.y = -gl_Position.y; // Vulkan NDC Y is inverted vs OpenGL (matches textured3d.vert.glsl)` — `skinned3d.vert.glsl` DOES flip, with its own comment confirming it's deliberate. Whoever wrote this comment either misremembered or never checked `skinned3d.vert.glsl`'s actual content before citing it as precedent. This is more dangerous than a silent omission: a future maintainer reading the comment would conclude the current behavior is intentional and verified, when it is neither. Also uses `mat3(uWorld)` (not inverse-transpose) for the normal transform post-skinning — explicitly acknowledged in-comment as preserving `EnsurePbrSkinnedProgram()`'s own EasyGL behavior rather than 'correcting' it, per this port's behavior-fidelity requirement (a documented, deliberate fidelity choice, not a new defect).

## Cross-File Observations

See `pbr3d.vert.glsl` (same bug, weaker justification) and `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full 4-shader-family writeup.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Correct, consistent with the shared shader family.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
