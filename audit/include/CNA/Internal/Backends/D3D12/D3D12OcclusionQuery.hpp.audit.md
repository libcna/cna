# Audit: include/CNA/Internal/Backends/D3D12/D3D12OcclusionQuery.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12OcclusionQuery.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: `OcclusionQuery` backend contract
- Graphics backend relevance: D3D12-specific
- FNA reference: FNA's own D3D device conventions (behavioral reference)
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Declares `D3D12OcclusionQueryBackend`: `ID3D12QueryHeap(D3D12_QUERY_HEAP_TYPE_OCCLUSION)` + a READBACK-heap resource, with `IsComplete()` trivially true once `End()` has run (this whole backend is synchronous).

## Executive Verdict

**Needs attention — a real, meaningful multi-draw correctness gap, referenced but not actually documented in this header.**

## Checklist Results

### API / FNA parity
**F1 (MEDIUM-HIGH, confirmed, see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** this class's own `.cpp` comment states its design is "correct for exactly one draw call between Begin()/End()" and refers the reader to "this class's own header doc comment for the multi-draw gap" — **but this header contains no such documentation anywhere** (confirmed via grep for "multi-draw"/similar — zero matches). The actual underlying gap (confirmed at the `D3D12GraphicsBackend.cpp` level): multiple draws between one `Begin()`/`End()` pair each get their own `BeginQuery`/`EndQuery` on the same query-heap slot, so later draws overwrite earlier ones' captured samples instead of accumulating — differs from XNA's real combine-across-multiple-draws semantics.

### Architecture
The synchronous-IsComplete() simplification itself is honestly and correctly justified given this backend's fully-synchronous submission model — a reasonable, disclosed choice.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (MEDIUM-HIGH):** multi-draw occlusion queries only capture the last draw, not the combined total — see `AUDIT_CROSS_CUTTING_FINDINGS.md`. Documentation cross-reference to a non-existent header section is a secondary, minor issue on top of the real gap.

## Cross-File Observations

See `D3D12GraphicsBackend.cpp`'s report for the confirming evidence (all 4 draw-recording methods independently wrap their own `BeginQuery`/`EndQuery` pair).

## Missing or Weak Tests

No dedicated test found exercising a multi-draw occlusion-query sequence on this backend.

## Positive Findings

Honest, correct synchronous-semantics simplification given the backend's fully-synchronous design.

## Final Assessment

One MEDIUM-HIGH, real (not just theoretical) correctness gap, honestly if incompletely disclosed.
