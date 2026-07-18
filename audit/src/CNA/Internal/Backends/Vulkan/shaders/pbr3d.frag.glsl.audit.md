# Audit: src/CNA/Internal/Backends/Vulkan/shaders/pbr3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/pbr3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: PbrEffect fragment shader — glTF 2.0 metallic-roughness BRDF (GGX distribution, Smith-Schlick-GGX visibility, Schlick Fresnel), 5 texture units.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

PbrEffect fragment shader — glTF 2.0 metallic-roughness BRDF (GGX distribution, Smith-Schlick-GGX visibility, Schlick Fresnel), 5 texture units.

## Executive Verdict

Healthy — a genuine positive finding.

## Checklist Results

### Behavioral correctness / FNA parity
**Confirmed CORRECT: `outColor = vec4(ambient + Lo + emissive, alpha)`** (line 90) — emissive is added unscaled, NOT re-multiplied by albedo, unlike the EnvironmentMapEffect emissive-remultiply bug confirmed elsewhere in this same backend (`env_map3d.frag.glsl`) and in 4 other backend-groups. The full BRDF (GGX/D, Smith-Schlick-GGX/G, Schlick/F) matches the glTF 2.0 spec's reference formulas and is internally consistent with `pbr3d_skinned.frag.glsl`'s byte-identical copy. Occlusion-map application to ambient only (not to direct lighting) matches the glTF convention.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**Confirmed CORRECT: `outColor = vec4(ambient + Lo + emissive, alpha)`** (line 90) — emissive is added unscaled, NOT re-multiplied by albedo, unlike the EnvironmentMapEffect emissive-remultiply bug confirmed elsewhere in this same backend (`env_map3d.frag.glsl`) and in 4 other backend-groups. The full BRDF (GGX/D, Smith-Schlick-GGX/G, Schlick/F) matches the glTF 2.0 spec's reference formulas and is internally consistent with `pbr3d_skinned.frag.glsl`'s byte-identical copy. Occlusion-map application to ambient only (not to direct lighting) matches the glTF convention.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Correct additive emissive combination — a positive counter-example to the emissive-remultiply bug family found elsewhere in this same backend and 4 others.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
