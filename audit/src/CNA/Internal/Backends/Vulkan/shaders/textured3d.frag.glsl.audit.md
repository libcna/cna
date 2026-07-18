# Audit: src/CNA/Internal/Backends/Vulkan/shaders/textured3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/textured3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: BasicEffect stride-20 fragment shader — texture sample + tint + fog mix.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

BasicEffect stride-20 fragment shader — texture sample + tint + fog mix.

## Executive Verdict

Healthy; instantiates already-recorded cross-cutting defects where applicable (fog formula; SkinnedEffect Ambient/Emissive gap), no new file-local defects.

## Checklist Results

### Behavioral correctness / FNA parity
BasicEffect stride-20 fragment shader — texture sample + tint + fog mix. Fog mix (`mix(FogColor, color, fogFactor)`) is correct in isolation; the mirrored-formula bug (already recorded) lives in the paired vertex shader's fog-factor computation, not here.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

BasicEffect stride-20 fragment shader — texture sample + tint + fog mix. Fog mix (`mix(FogColor, color, fogFactor)`) is correct in isolation; the mirrored-formula bug (already recorded) lives in the paired vertex shader's fog-factor computation, not here.

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
