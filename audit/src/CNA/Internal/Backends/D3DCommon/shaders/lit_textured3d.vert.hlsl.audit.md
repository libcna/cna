# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/lit_textured3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: BasicEffect vertex stage, per-pixel-lit path (PreferPerPixelLighting == true)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: BasicEffect.fx (lit, per-pixel branch) / Lighting.fxh (fog + normal transform)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Transforms position, computes the world-space normal via `InverseTranspose3x3(World)`, world position, and fog factor; lighting itself is deferred to the fragment shader.

## Executive Verdict

**Mostly healthy — correct, well-documented normal transform; shares the systemic fog bug.**

## Checklist Results

### API / XNA parity
**Correct** `InverseTranspose3x3((float3x3)World)` normal transform (line 79), with an explicitly correct rationale in its own comment (Task 898 fix: an MVP-based transform would incorrectly bake View/Projection into the normal). This is part of this directory's control group proving the skinned-shader normal-transform omission is a skinning-specific bug, not a general error — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 87: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 87.

## Cross-File Observations

Its own comment cites `env_map3d.vert.hlsl`'s "already-correct pattern" for the normal-transform choice — independently verified true. `lit_textured3d_vertexlit.vert.hlsl` and `pbr3d.vert.hlsl` reuse the identical `InverseTranspose3x3` helper verbatim.

## Missing or Weak Tests

No dedicated D3D11/D3D12 lit-BasicEffect fog test found.

## Positive Findings

Correct, well-reasoned normal-transform implementation with an explicit citation of why the naive (MVP-based) approach would be wrong — a genuinely well-engineered file apart from the shared fog defect.

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug); the file's own defining feature (correct normal transform) is verified correct.
