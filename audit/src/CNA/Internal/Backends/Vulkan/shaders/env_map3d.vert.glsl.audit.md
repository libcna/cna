# Audit: src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: EnvironmentMapEffect vertex shader — world-space normal/eye-direction computation for reflection mapping.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

EnvironmentMapEffect vertex shader — world-space normal/eye-direction computation for reflection mapping.

## Executive Verdict

Needs attention — 2 already-recorded cross-cutting defects confirmed here directly.

## Checklist Results

### Behavioral correctness / FNA parity
**Confirmed: lacks the `gl_Position.y = -gl_Position.y` Y-flip** every sibling core 3D vertex shader in this backend applies (`gl_Position = pc.mvp * vec4(aPos, 1.0);` with no follow-up flip) — causes `EnvironmentMapEffect` scenes to render vertically mirrored on Vulkan. Already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, now joined by `pbr3d`/`pbr3d_skinned`/`instanced3d`'s own independently confirmed instances of the same omission (see those files' own reports). Normal transform itself IS correct here (`transpose(inverse(mat3(pc.world)))`, the proper inverse-transpose upper-left 3x3), and the fog-formula bug is present too, both consistent with already-recorded findings.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**Confirmed: lacks the `gl_Position.y = -gl_Position.y` Y-flip** every sibling core 3D vertex shader in this backend applies (`gl_Position = pc.mvp * vec4(aPos, 1.0);` with no follow-up flip) — causes `EnvironmentMapEffect` scenes to render vertically mirrored on Vulkan. Already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, now joined by `pbr3d`/`pbr3d_skinned`/`instanced3d`'s own independently confirmed instances of the same omission (see those files' own reports). Normal transform itself IS correct here (`transpose(inverse(mat3(pc.world)))`, the proper inverse-transpose upper-left 3x3), and the fog-formula bug is present too, both consistent with already-recorded findings.

## Cross-File Observations

See `pbr3d.vert.glsl`/`pbr3d_skinned.vert.glsl`/`instanced3d.vert.glsl` for the newly confirmed sibling instances of the same missing-Y-flip defect class.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Correct, consistent with the shared shader family.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
