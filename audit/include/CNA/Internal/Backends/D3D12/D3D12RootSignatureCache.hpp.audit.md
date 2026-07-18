# Audit: include/CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: N/A directly (binding-layout declaration for a D3D12 PSO)
- Graphics backend relevance: D3D12-specific
- FNA reference: N/A
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Declares `D3D12RootSignatureCache::GetOrCreate()`, keyed by `(numCbvs, numSrvs, numSamplers)` binding-shape tuples.

## Executive Verdict

**Needs attention — excellent engineering documentation overall, but the class-level comment about static samplers is now stale (a documentation-rot finding, not a functional bug).**

## Checklist Results

### API / FNA parity
**F1 (LOW-MEDIUM, documentation rot):** the class-level doc comment (lines 20-27) claims every root signature uses `D3D12_STATIC_SAMPLER_DESC` static samplers, framing dynamic per-draw `SamplerState` support as explicit future follow-up work — **stale**: the actual `.cpp` implementation (its own "DX-119" comment) was later upgraded to real dynamic sampler descriptor tables. See `AUDIT_CROSS_CUTTING_FINDINGS.md`. Not a functional defect — the current code is correct — purely a not-revisited header comment.

### Architecture
The N-separate-single-descriptor-tables design (rather than one shared multi-descriptor table) is exceptionally well-documented, including a genuine, honestly-caveated empirical finding (DX-111: a multi-descriptor table sampled as all-zero under this project's Wine+vkd3d-proton dev loop despite independently-verified-correct CPU-side descriptor writes) with an explicit note that this might be a dev-loop-specific limitation rather than a universal D3D12 rule, deferring final verification to a real-Windows pass (DX-90/DX-114) — a model example of honest, falsifiable engineering documentation.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

**F1 (LOW-MEDIUM):** stale class-level comment describing an already-superseded static-sampler design — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

Directly triggered discovery of the resolved documentation-rot finding by cross-checking the `.cpp`'s own "DX-119" comment against this header's older class-level claim.

## Missing or Weak Tests

No dedicated test found for the N-separate-descriptor-tables workaround specifically (as opposed to general textured-draw coverage, which would exercise it implicitly).

## Positive Findings

The DX-111 empirical-finding documentation (dev-loop-specific limitation, honestly caveated as possibly not universal) is one of the most rigorous pieces of engineering writing found in this entire audit.

## Final Assessment

One LOW-MEDIUM documentation-rot finding (stale, not functionally wrong); otherwise excellent.
