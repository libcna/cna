# Audit: src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements D3D9EffectBackend: runtime HLSL compilation (D3DCompile, vs/ps_2_0 or _3_0 by HiDef profile) plus named-uniform upload via a parsed constant table.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements D3D9EffectBackend: runtime HLSL compilation (D3DCompile, vs/ps_2_0 or _3_0 by HiDef profile) plus named-uniform upload via a parsed constant table.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Correctly targets vs/ps_2_0 (Reach) or vs/ps_3_0 (HiDef) based on the profile flag; `UploadEXT()` correctly zero-pads scalar/vec2/vec3 uploads to a full float4 register without reading past the caller's buffer (`std::min(componentCount, 16)` clamp before the `memcpy`). `SetUniformInt()`'s choice to upload ints as converted floats through the Float4 register path (not the separate `D3DXRS_INT4` file) is explicitly documented as matching D3D11EffectBackend's own identical convention — a deliberate, consistent, cross-backend design choice for this specific mechanism's scope (SpriteBatch custom-shader tinting/parameterization), not a general-purpose uniform system.

## Detailed Findings

Correctly targets vs/ps_2_0 (Reach) or vs/ps_3_0 (HiDef) based on the profile flag; `UploadEXT()` correctly zero-pads scalar/vec2/vec3 uploads to a full float4 register without reading past the caller's buffer (`std::min(componentCount, 16)` clamp before the `memcpy`). `SetUniformInt()`'s choice to upload ints as converted floats through the Float4 register path (not the separate `D3DXRS_INT4` file) is explicitly documented as matching D3D11EffectBackend's own identical convention — a deliberate, consistent, cross-backend design choice for this specific mechanism's scope (SpriteBatch custom-shader tinting/parameterization), not a general-purpose uniform system.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
