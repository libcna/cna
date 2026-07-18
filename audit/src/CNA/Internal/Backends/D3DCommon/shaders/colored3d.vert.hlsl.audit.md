# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/colored3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/colored3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: BasicEffect vertex stage, VertexPositionColor (no texture, no lighting)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: BasicEffect.fx (untextured, unlit branch) / Lighting.fxh (fog only)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Transforms position by MVP, mixes vertex color with DiffuseColor when `VertexColorEnabled`, and computes a fog factor. Its header comment is this shader family's canonical explanation of the D3D-vs-Vulkan matrix/mul() and Y-flip/depth-range conventions.

## Executive Verdict

**Mostly healthy — correct core logic and a genuinely useful positive finding, but shares the systemic fog bug.**

## Checklist Results

### API / XNA parity
Row-major cbuffer / `mul(v,M)` convention correctly matches XNA's own CPU-side Matrix layout, verified consistent with every sibling shader in this directory.

### Architecture
**Positive finding, independently verified**: this file's comment claims D3D11's clip space already matches XNA's own convention, so (unlike the Vulkan GLSL source) no `pos.y = -pos.y` flip is needed. Checked this holds for `env_map3d.vert.hlsl` too (a shader whose comment doesn't discuss the topic) — confirmed no D3DCommon 3D vertex shader flips, consistent with this being a deliberate, correctly-applied family-wide convention rather than an oversight.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 60: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 60.

## Cross-File Observations

This file's own header comment is the best-documented explanation of the Y-flip/depth-range convention in the whole directory; other files (`textured3d.vert.hlsl`) explicitly point back to it instead of repeating the explanation.

## Missing or Weak Tests

No dedicated D3D11/D3D12 fog test found.

## Positive Findings

Clear, accurate, well-reasoned header comment explaining a genuine backend difference (no Y-flip needed) rather than leaving it as an unexplained deviation from the Vulkan source — a model example of documentation done right in this audit.

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug); otherwise correct and unusually well-documented.
