# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: GLSL shader source (SDL_gpu SPIR-V binding convention: vertex textures/storage set 0, vertex UBOs
  set 1, fragment textures/storage set 2, fragment UBOs set 3)
- XNA/FNA relevance: EnvironmentMapEffect fragment stage
- Graphics backend relevance: compiled to SPIR-V by `compile_shaders.py`, consumed by `SdlGpuGraphicsBackend`
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Computes 3-directional-light diffuse lighting, samples the base texture and env cube map, Fresnel-weighted blend, fog mix.

## Executive Verdict

**Needs attention — confirms the already-recorded cross-cutting EmissiveColor-remultiply bug at the source level.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed (see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** line 53, `vec3 litRGB = (pc.emissiveAmount.xyz + lightSum) * fragTint.rgb;` — re-multiplies the already-combined emissive term by the diffuse tint, instead of FNA's real convention of adding EmissiveColor unscaled after the lightSum*DiffuseColor multiply (exactly as `lit_textured3d.frag.glsl`'s own correct equivalent line does). The 5th confirmed backend-group for this cross-cutting defect (after Bgfx, WebGPU, Vulkan, D3D11+D3D12 — SdlGpu was already recorded as sharing this via the `examples-tests-sdlgpu`/`sdlgpu_smoke_test.cpp` audit; this is the direct source confirmation). Masked by every current test's use of default DiffuseColor=(1,1,1,1), which makes the re-multiply a no-op.

### Logic
Everything else — the Fresnel/flat blend-factor selection, the `combinedAlpha`-scaled envmap sample (matching FNA's real PSEnvMap/PSEnvMapSpecular convention), the `safeNormalize()` NaN guard for a disabled DirectionalLight — is independently verified correct.

## Detailed Findings

**F1 (HIGH):** EmissiveColor re-multiplied by DiffuseColor, line 53 — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

Directly comparable to `lit_textured3d.frag.glsl`'s own correct emissive-unscaled line — proving this backend's author knew the correct convention and only got `EnvironmentMapEffect` specifically wrong, consistent with the pattern already observed in every other backend sharing this defect.

## Missing or Weak Tests

No test in this codebase (any backend) varies DiffuseColor away from its default white for `EnvironmentMapEffect` — a systemic gap, not specific to this file.

## Positive Findings

`safeNormalize()`'s NaN-guard for a disabled DirectionalLight (Direction=(0,0,0)) is a genuine robustness improvement not present in this project's HLSL/WGSL equivalents.

## Final Assessment

One HIGH finding (confirmed cross-cutting EmissiveColor-remultiply bug); otherwise correct, with a genuine robustness improvement (safeNormalize).
