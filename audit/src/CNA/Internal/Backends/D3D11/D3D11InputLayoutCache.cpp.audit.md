# Audit: src/CNA/Internal/Backends/D3D11/D3D11InputLayoutCache.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D11/D3D11InputLayoutCache.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ implementation (40 lines)
- Related header: `include/CNA/Internal/Backends/D3D11/D3D11InputLayoutCache.hpp` (same shard)
- XNA/FNA relevance: N/A directly
- Graphics backend relevance: D3D11-only
- FNA reference: N/A
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Implements `GetOrCreate()`: looks up `D3DVertexFormatHelper::InputElementsForStride()` and
`D3DShaderCache::GetVertexShaderBytecode()`, then calls `CreateInputLayout()`.

## Executive Verdict

**Mostly healthy — correct, with one LOW-severity design characteristic worth flagging.**

## Checklist Results

### Behavioral correctness / Logic
**F1 (LOW):** on a bad stride, missing bytecode, or a failed `CreateInputLayout()` call, this function caches a
**null** `ComPtr` under that `(variant, stride)` key (lines 22-24, 31-33, and the fall-through after line 36 all
`cache_.emplace(key, layout)` regardless of whether `layout` ended up populated). A subsequent call with the exact
same key returns this cached null immediately rather than re-attempting creation. For the "bad stride"/"missing
bytecode" cases this is harmless (those inputs can't ever succeed). For a `CreateInputLayout()` failure specifically,
this means a **transient** failure (e.g. a momentary device/driver hiccup) permanently poisons that cache entry
for the process's remaining lifetime — no retry ever happens again for that `(variant, stride)` pair. Low severity
in practice (D3D11 `CreateInputLayout()` failures are near-always deterministic/input-driven, not transient), but
worth noting as a design characteristic a device-lost-recovery path (`Clear()`, not yet fully wired per DX-27)
would need to account for.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (LOW):** permanently-cached null on any `CreateInputLayout()` failure, not just genuinely-invalid inputs.

## Cross-File Observations

Correctly builds on `D3DVertexFormatHelper::InputElementsForStride()` and `D3DShaderCache::GetVertexShaderBytecode()`,
both already independently verified correct in the `backend-d3dcommon` shard's own audit.

## Missing or Weak Tests

No dedicated test found exercising a `CreateInputLayout()` failure path or the cache's behavior across a
simulated device-lost/recreate cycle.

## Positive Findings

Clean, minimal, correctly-layered implementation with no redundant logic.

## Final Assessment

One LOW-severity design characteristic (permanent failure caching); otherwise correct.
