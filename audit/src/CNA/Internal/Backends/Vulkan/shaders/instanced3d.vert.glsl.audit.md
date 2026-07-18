# Audit: src/CNA/Internal/Backends/Vulkan/shaders/instanced3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/instanced3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: Hardware-instanced 3D draw vertex shader (NOXNA CNA extension, not a stock XNA effect) — per-instance world matrix from a per-instance vertex buffer, applied to a shared VP.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

Hardware-instanced 3D draw vertex shader (NOXNA CNA extension, not a stock XNA effect) — per-instance world matrix from a per-instance vertex buffer, applied to a shared VP.

## Executive Verdict

Needs attention — NEW, undocumented instance of the missing-Y-flip bug, confirmed via direct source read.

## Checklist Results

### Behavioral correctness / FNA parity
**Confirmed: lacks the Y-flip, with NO comment or rationale of any kind** (unlike `pbr3d`/`pbr3d_skinned`, which at least attempt a justification). `gl_Position = pc.vp * world * vec4(aPos, 1.0);` — no follow-up flip, despite `FillInstancedPushConst()`'s `vp = view * proj` being composed identically (no baked-in flip) to every other 3D draw path's `wvp = world * view * projection`. Net effect: instanced draws render vertically mirrored relative to every other effect type. Most likely a simple oversight — the Y-flip convention may have been established after this instancing path was added, or the two code paths were never cross-checked against each other. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**Confirmed: lacks the Y-flip, with NO comment or rationale of any kind** (unlike `pbr3d`/`pbr3d_skinned`, which at least attempt a justification). `gl_Position = pc.vp * world * vec4(aPos, 1.0);` — no follow-up flip, despite `FillInstancedPushConst()`'s `vp = view * proj` being composed identically (no baked-in flip) to every other 3D draw path's `wvp = world * view * projection`. Net effect: instanced draws render vertically mirrored relative to every other effect type. Most likely a simple oversight — the Y-flip convention may have been established after this instancing path was added, or the two code paths were never cross-checked against each other. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Correct, consistent with the shared shader family.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
