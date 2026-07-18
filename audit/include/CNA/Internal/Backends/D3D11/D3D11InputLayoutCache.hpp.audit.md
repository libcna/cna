# Audit: include/CNA/Internal/Backends/D3D11/D3D11InputLayoutCache.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11InputLayoutCache.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (50 lines)
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11InputLayoutCache.cpp` (same shard)
- XNA/FNA relevance: N/A directly (internal GPU pipeline-object plumbing)
- Graphics backend relevance: `ID3D11InputLayout` caching, D3D11-only (D3D12 folds this into its own PSO, per
  this header's own accurate architectural note)
- FNA reference: N/A
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Declares `D3D11InputLayoutCache::GetOrCreate(device, variant, strideInBytes)`, keyed by `(D3DShaderVariant,
stride)`, plus a `Clear()` for future device-lost recovery.

## Executive Verdict

**Healthy.** Correct, minimal design; one low-severity design characteristic worth noting (permanently-cached
null on a failed creation).

## Checklist Results

### Architecture
Correctly explains why this cache lives in `D3D11`, not `D3DCommon` (unlike `D3DVertexFormatHelper`/
`D3DShaderCache`) — `ID3D11InputLayout` is a D3D11-only COM object with no D3D12 equivalent, matching design
decision 4's "D3DCommon only holds what's genuinely shared" rule, independently verified consistent with
`D3DVertexFormatHelper.hpp`'s own D3D11/D3D12-parallel-array design in the shared `backend-d3dcommon` shard.
`Clear()`'s doc comment correctly anticipates a not-yet-implemented device-lost recovery path (DX-27) rather than
being unexplained dead code.

### Memory/resource lifetime
Documented "thread-unsafe by design" matches this project's established single-threaded `GraphicsDevice` usage
convention, consistent with every other per-device D3D11 cache in this shard.

### C++ correctness / Performance / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None in this file — see the `.cpp` report for one low-severity observation about the failure-caching behavior.

## Cross-File Observations

See `.cpp` report.

## Missing or Weak Tests

No dedicated test found for the device-lost `Clear()` path (unsurprising, since DX-27's full recovery isn't
implemented yet per this file's own comment).

## Positive Findings

Clear, accurate architectural rationale for why this specific cache (unlike its siblings) is D3D11-only.

## Final Assessment

No issues found.
