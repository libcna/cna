# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/alpha_test3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/alpha_test3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: AlphaTestEffect vertex stage (position/UV transform, diffuse tint, fog factor)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: AlphaTestEffect.fx / Lighting.fxh (fog only)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Ported line-by-line from the Vulkan GLSL source; transforms position by MVP, forwards UV, sets `Tint = DiffuseColor`, and computes a fog factor from raw object-space Z.

## Executive Verdict

**Needs attention (shares the systemic fog bug).**

## Checklist Results

### API / XNA parity
Matrix transform, tint, and stride-agnostic UV pass-through all correct and match AlphaTestEffect's real vertex contract.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 47: `output.FogFactor = (FogEnabled > 0.5) ? saturate((FogEnd - input.Position.z) / max(FogEnd - FogStart, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source).

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 47 — see above.

## Cross-File Observations

Shares the fog bug with all 14 other fog-capable D3DCommon siblings — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated D3D11/D3D12 AlphaTestEffect fog test found in this audit so far; the bug is currently unverified by any registered CTest for this backend.

## Positive Findings

Stride-agnostic design (UV offset varies 20/24/32 depending on vertex format, handled entirely by `D3DVertexFormatHelper.hpp`'s input-layout descriptors, not by this shader) is clean and correctly documented.

## Final Assessment

One confirmed MEDIUM finding (shared fog-formula bug), otherwise correct.
