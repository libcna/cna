# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/env_map3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/env_map3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: EnvironmentMapEffect fragment stage (reflection sampling + Fresnel + lighting)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: EnvironmentMapEffect.fx / Lighting.fxh
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Computes 3-directional-light diffuse lighting, samples the base texture and an environment cube map via the reflected view vector, blends between them by a Fresnel-or-flat `blendFactor`, and mixes toward FogColor. Explicitly "ported line-by-line from Vulkan".

## Executive Verdict

**Significant correctness risk — shares the cross-cutting EnvironmentMapEffect emissive/diffuse re-multiply bug.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH.** Line 46: `float3 litRGB = (EmissiveEm.xyz + lightSum) * DiffuseColor.rgb;` — re-multiplies the already-combined `EmissiveColor + lightSum` term by `DiffuseColor`, instead of FNA's real `Lighting.fxh` convention of adding `EmissiveColor` **unscaled** after the `lightSum*DiffuseColor` multiply (exactly as this directory's own `lit_textured3d.frag.hlsl`/`lit_textured3d_vertexlit.vert.hlsl` correctly do at their own equivalent line). This project's own `EnvironmentMapEffect.cpp` comment explicitly states the unscaled-add is required to "match FNA" — making this a directly-confirmed, self-contradicted-by-the-project's-own-C++-comment defect, not just an inference. This is the 5th backend-group in this audit confirmed to share this exact bug (after Bgfx, WebGPU, Vulkan, SdlGpu) — see `AUDIT_CROSS_CUTTING_FINDINGS.md`. Masked by every current EnvironmentMapEffect test's use of default `DiffuseColor=(1,1,1,1)`, which makes the re-multiply a no-op.

### Logic
Everything else — the Task 891 base-lerp `combinedAlpha` scaling (line 59), the Fresnel/flat blend-factor selection (line 53-55), and the fog mix (line 61) — is independently verified correct and consistent with this project's own already-fixed Task 891/395 history.

## Detailed Findings

**F1 (HIGH):** `EmissiveColor` re-multiplied by `DiffuseColor` instead of added unscaled, line 46 — 5th confirmed backend-group for this cross-cutting defect.

## Cross-File Observations

Directly comparable to `lit_textured3d.frag.hlsl`'s own correct emissive-handling line (`lit = lightSum*Tint.rgb + EmissiveColorPad.xyz`, no re-multiply) — proving the D3DCommon author knew the correct convention and only got `EnvironmentMapEffect` specifically wrong, consistent with the pattern already observed for the skinned-normal-transform bug in the same directory.

## Missing or Weak Tests

No test in this codebase (any backend) varies `DiffuseColor` away from its default white or `EmissiveColor`/`AmbientLightColor` away from black for `EnvironmentMapEffect` — a systemic testing gap across every backend sharing this bug, not specific to D3D11/D3D12.

## Positive Findings

Task 891's base-lerp alpha-scaling fix and the Fresnel blend-factor logic are both correctly implemented and consistent with this project's own documented fix history.

## Final Assessment

One confirmed HIGH finding (EmissiveColor re-multiply bug, 5th confirmed backend-group), otherwise correct.
