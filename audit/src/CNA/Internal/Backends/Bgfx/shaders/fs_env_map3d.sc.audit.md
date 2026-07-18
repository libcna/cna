# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: EnvironmentMapEffect fragment stage
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Computes 3-directional-light diffuse lighting, samples base texture and env cube map, Fresnel-weighted blend, fog mix.

## Executive Verdict

**Needs attention — confirms the already-recorded cross-cutting EmissiveColor-remultiply bug at the source level.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed (see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** line 28, `vec3 litRGB = (u_emissiveColor.xyz + lightSum) * u_diffuseColor.xyz;` — re-multiplies the already-combined emissive term by diffuse, instead of FNA's real convention (add EmissiveColor unscaled after the lightSum*DiffuseColor multiply). This is the ORIGINAL confirmed instance of this cross-cutting defect in this audit (already found via 5 `examples-tests-bgfx` files); this pass confirms it directly at the shader source.

### Logic
Everything else — the Task 891 base-lerp `combinedAlpha` scaling, the Fresnel/flat blend-factor selection — is independently verified correct and consistent with this project's own documented fix history.

## Detailed Findings

**F1 (HIGH):** EmissiveColor re-multiplied by DiffuseColor, line 28 — the original confirmed instance of this cross-cutting defect (now also confirmed in WebGPU, Vulkan, SdlGpu, D3D11+D3D12 — 5 backend-groups total).

## Cross-File Observations

Directly comparable to `fs_lit_textured3d.sc`'s own correct emissive-unscaled line — proving this backend's author knew the correct convention and only got `EnvironmentMapEffect` specifically wrong.

## Missing or Weak Tests

No test in this codebase (any backend) varies DiffuseColor away from its default white for `EnvironmentMapEffect` — a systemic gap.

## Positive Findings

Correct Task 891 alpha-scaling and Fresnel-blend logic.

## Final Assessment

One HIGH finding (the original confirmed instance of the cross-cutting EmissiveColor-remultiply bug); otherwise correct.
