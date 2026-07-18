# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/instanced3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/instanced3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: NOXNA — GPU instancing helper, not a stock XNA effect (XNA/FNA has no built-in instancing API)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: N/A — no direct FNA equivalent
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Builds a per-instance World matrix from 4 instanced input rows, transforms position by `world` then the shared `Vp` (view-projection), outputs a flat `DiffuseColor`.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
N/A (non-XNA extension).

### Logic
Row-vector-from-4-rows World construction (line 45) correctly preserves the same combined transform as the GLSL source's `mat4(col0,col1,col2,col3)` column construction, given this file set's established `mul(v,M)` row-vector convention (documented and independently verified against `colored3d.vert.hlsl`'s explanation).

### Systematic FNA parity gaps
No fog term at all — correct and consistent with FNA, which does not fog instanced draws — matches FNA, which has no instancing API to begin with, and matches the GLSL source's own documented lack of fog handling here.

## Detailed Findings

None.

## Cross-File Observations

Shares its per-vertex-attribute-only input design with the GLSL source; the per-instance `INSTANCEWORLD0..3` semantic convention is explicitly flagged in its own comment as needing to match whatever future `D3D11_INPUT_ELEMENT_DESC` wiring consumes it (Phase DX5, not yet written per the comment) — a real, self-disclosed forward-compatibility risk rather than a current defect.

## Missing or Weak Tests

No dedicated D3D11/D3D12 instancing test found in this audit so far (consistent with the comment noting the consuming input-layout code doesn't exist yet either).

## Positive Findings

Correctly minimal, stride-agnostic input design; the forward-looking risk about semantic-name matching is honestly disclosed in the shader's own comment rather than left implicit.

## Final Assessment

No defects found; one disclosed, not-yet-applicable forward-compatibility risk.
