# Audit: include/CNA/Internal/Backends/D3D11/D3D11OcclusionQuery.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11OcclusionQuery.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (37 lines)
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11OcclusionQuery.cpp` (same shard)
- XNA/FNA relevance: implements `Microsoft::Xna::Framework::Graphics::OcclusionQuery`'s backend contract
  (`IOcclusionQueryBackend`)
- Graphics backend relevance: D3D11-specific `ID3D11Query(D3D11_QUERY_OCCLUSION)` wrapper
- FNA reference: FNA's `OcclusionQuery` (behavioral reference — exact-count vs. boolean-visibility precision)
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Declares `D3D11OcclusionQueryBackend`, a thin RAII wrapper around a single `ID3D11Query` of type
`D3D11_QUERY_OCCLUSION`.

## Executive Verdict

**Healthy.** Correct, minimal interface; a genuinely useful comparison note against EasyGL's own occlusion-query
precision.

## Checklist Results

### API / XNA / FNA parity
Correctly documents that D3D11 can report an *exact* visible-pixel count (`D3D11_QUERY_OCCLUSION`), unlike EasyGL's
GLES3 `GL_ANY_SAMPLES_PASSED` (boolean-only) — this is accurate: FNA's `OcclusionQuery.PixelCount` is documented to
return an approximate/exact count depending on platform, and D3D11 genuinely can be more precise here, matching
this file's own claim.
`PixelCount()`'s explicit `UINT64`-to-`int` clamp to `INT32_MAX` (rather than an unchecked/silent truncation) is
correctly documented and matches the base interface's `int` return type constraint.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None.

## Cross-File Observations

See the `.cpp` report for the implementation-level verification.

## Missing or Weak Tests

No dedicated D3D11 occlusion-query test found in this audit so far (this shard's own `examples-tests-d3d11` batch
not yet audited).

## Positive Findings

Honest, accurate comparison against EasyGL's own precision limitation rather than an unqualified claim.

## Final Assessment

No issues found.
