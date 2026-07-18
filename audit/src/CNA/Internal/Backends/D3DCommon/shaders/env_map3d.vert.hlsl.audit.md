# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/env_map3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/env_map3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: EnvironmentMapEffect vertex stage (world-space normal + eye direction for reflection)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: EnvironmentMapEffect.fx / Lighting.fxh (fog only)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Transforms position, computes the world-space normal via `InverseTranspose3x3(World)`, world-space eye direction, and a fog factor. Explicitly "ported line-by-line from Vulkan".

## Executive Verdict

**Needs attention (shares the systemic fog bug), but two genuinely positive, independently-verified findings.**

## Checklist Results

### API / XNA parity
Normal transform correctly uses `InverseTranspose3x3((float3x3)World)` (line 61), matching `lit_textured3d.vert.hlsl`'s and `pbr3d.vert.hlsl`'s established correct convention — unlike every *skinned* shader in this directory, which gets this wrong (see `AUDIT_CROSS_CUTTING_FINDINGS.md`).

### Architecture
**Positive finding, independently verified**: unlike Vulkan's own `env_map3d.vert.glsl` (confirmed elsewhere in this audit to be missing a required Y-flip, causing `EnvironmentMapEffect` to render vertically mirrored on Vulkan), this D3D port correctly and deliberately omits any Y-flip — verified this isn't an oversight by cross-checking `colored3d.vert.hlsl`'s explicit documentation of why D3D's own clip space doesn't need Vulkan's corrective flip. **D3D11/D3D12 do NOT share Vulkan's EnvironmentMapEffect vertical-mirroring bug.**

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 67: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 67 (this file's own comment claims it "matches the established Task 888 formula" — true only in the sense that Task 888 itself established the wrong formula for this backend family; it does not match FNA).

## Cross-File Observations

Correct normal-transform convention makes this file part of the control group proving the skinned-shader normal-transform bug is a skinning-specific oversight, not a general math error — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated D3D11/D3D12 `EnvironmentMapEffect` test found; the (correct) absence of vertical mirroring on this backend is currently unverified by any registered CTest.

## Positive Findings

Two independently-verified correct, deliberate backend-specific deviations from the Vulkan source (normal transform already correct; Y-flip correctly omitted) in a file whose header comment could easily have implied a blind copy.

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug); genuinely correct on the two axes (normal transform, Y-flip) where its Vulkan/skinned siblings are not.
