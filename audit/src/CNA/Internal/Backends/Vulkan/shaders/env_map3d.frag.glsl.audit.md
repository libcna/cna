# Audit: src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: EnvironmentMapEffect fragment shader — cubemap reflection sampling, Fresnel blend, fog mix.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

EnvironmentMapEffect fragment shader — cubemap reflection sampling, Fresnel blend, fog mix.

## Executive Verdict

Needs attention — 1 already-recorded cross-cutting defect confirmed here directly.

## Checklist Results

### Behavioral correctness / FNA parity
**Confirmed: `litRGB = (emissive + lightSum) * diffuseColor` re-multiplies EmissiveColor by DiffuseColor** (line 39: `vec3 litRGB = (ep.emissive_em.xyz + lightSum) * ep.diffuseColor.rgb;`), instead of FNA's correct unscaled-add convention (`lightSum * diffuseColor + emissive`). Already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md` as the ORIGINAL confirmed instance's Vulkan counterpart (5 backend-groups now share this exact bug). The envmap/Fresnel blend math itself, and the Task 891 `combinedAlpha`-scaling-of-envSample fix, are both correct.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**Confirmed: `litRGB = (emissive + lightSum) * diffuseColor` re-multiplies EmissiveColor by DiffuseColor** (line 39: `vec3 litRGB = (ep.emissive_em.xyz + lightSum) * ep.diffuseColor.rgb;`), instead of FNA's correct unscaled-add convention (`lightSum * diffuseColor + emissive`). Already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md` as the ORIGINAL confirmed instance's Vulkan counterpart (5 backend-groups now share this exact bug). The envmap/Fresnel blend math itself, and the Task 891 `combinedAlpha`-scaling-of-envSample fix, are both correct.

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
