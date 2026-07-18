# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/alpha_test_colored3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/alpha_test_colored3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: AlphaTestEffect fragment stage, VertexColorEnabled variant
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: AlphaTestEffect.fx
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Identical alpha-test logic to `alpha_test3d.frag.hlsl`; only ever reads the already-combined Tint the vertex shader computed, not vertex color directly, so needs no changes for the VertexColorEnabled variant beyond a byte-identical PerDraw layout.

## Executive Verdict

**Healthy — correct, and correctly documents why it needs no changes from its sibling.**

## Checklist Results

### API / XNA parity
Identical alpha-test semantics to `alpha_test3d.frag.hlsl`, correctly re-verified.

### Logic
Fog mix (line 58) is correct in isolation; the mirrored-formula defect lives in the vertex stage.

## Detailed Findings

None in this file specifically — see `alpha_test_colored3d.vert.hlsl.audit.md`.

## Cross-File Observations

Its own header comment's claim that no changes are needed here beyond the PerDraw layout is accurate — independently confirmed the cbuffer layouts of both files are indeed byte-identical.

## Missing or Weak Tests

No dedicated test found for this specific file.

## Positive Findings

Accurate, verified self-documentation of exactly why this file is a near-duplicate of its sibling rather than an unexplained one.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
