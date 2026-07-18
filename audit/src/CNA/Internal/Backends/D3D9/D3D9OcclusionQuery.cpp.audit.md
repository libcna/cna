# Audit: src/CNA/Internal/Backends/D3D9/D3D9OcclusionQuery.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9OcclusionQuery.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements D3D9OcclusionQueryBackend via IDirect3DQuery9::Issue(D3DISSUE_BEGIN/END) and GetData().
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements D3D9OcclusionQueryBackend via IDirect3DQuery9::Issue(D3DISSUE_BEGIN/END) and GetData().

## Executive Verdict

Healthy — a genuine positive structural contrast with D3D12's own confirmed bug.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**Structurally immune to D3D12's confirmed multi-draw occlusion-query bug** (each draw between one Begin()/End() overwriting the previous draw's sample count on the same query-heap slot): D3D9's occlusion query is immediate, not deferred/recorded — one real `Issue(D3DISSUE_BEGIN)`/`Issue(D3DISSUE_END)` pair wraps however many draws happen between them, and D3D9 hardware/driver semantics naturally accumulate ALL of their passing-pixel counts into that single query object, matching XNA's real `OcclusionQuery` semantics with no extra bookkeeping needed. `IsComplete()`'s `GetData(nullptr, 0, 0) == S_OK` is the standard, correct D3D9 idiom for non-blocking completion polling.

## Detailed Findings

**Structurally immune to D3D12's confirmed multi-draw occlusion-query bug** (each draw between one Begin()/End() overwriting the previous draw's sample count on the same query-heap slot): D3D9's occlusion query is immediate, not deferred/recorded — one real `Issue(D3DISSUE_BEGIN)`/`Issue(D3DISSUE_END)` pair wraps however many draws happen between them, and D3D9 hardware/driver semantics naturally accumulate ALL of their passing-pixel counts into that single query object, matching XNA's real `OcclusionQuery` semantics with no extra bookkeeping needed. `IsComplete()`'s `GetData(nullptr, 0, 0) == S_OK` is the standard, correct D3D9 idiom for non-blocking completion polling.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

Correctly and simply implements multi-draw occlusion accumulation by virtue of D3D9's immediate (non-deferred) rendering model — a structural, not merely incidental, immunity to D3D12's own confirmed bug in this exact area.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
