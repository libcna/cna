# Audit: src/CNA/Internal/Backends/Vulkan/shaders/instanced3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/instanced3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: Hardware-instanced 3D draw fragment shader — flat per-instance diffuse color only (no texture, no fog).
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

Hardware-instanced 3D draw fragment shader — flat per-instance diffuse color only (no texture, no fog).

## Executive Verdict

Healthy relative to its own honestly-disclosed scope.

## Checklist Results

### Behavioral correctness / FNA parity
**No fog support at all** — but this is explicitly, candidly disclosed in the file's own header comment: Instanced3D reuses the original, unmodified single-binding `pipelineLayoutExt3D_` (shared with 2D SpriteBatch, which must stay untouched), which is structurally incompatible with the 2-binding layout fog would require. Since hardware instancing is a CNA-original (NOXNA) extension, not a stock XNA/FNA feature, the absence of fog here is a disclosed scope limitation rather than an XNA-parity defect.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**No fog support at all** — but this is explicitly, candidly disclosed in the file's own header comment: Instanced3D reuses the original, unmodified single-binding `pipelineLayoutExt3D_` (shared with 2D SpriteBatch, which must stay untouched), which is structurally incompatible with the 2-binding layout fog would require. Since hardware instancing is a CNA-original (NOXNA) extension, not a stock XNA/FNA feature, the absence of fog here is a disclosed scope limitation rather than an XNA-parity defect.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Honest, in-code disclosure of a real architectural constraint (shared pipeline layout) rather than a silent gap.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
