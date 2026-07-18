# Audit: include/CNA/Internal/Backends/D3D12/D3D12SamplerCache.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12SamplerCache.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: `SamplerState` -> D3D12 sampler descriptor caching
- Graphics backend relevance: D3D12-specific
- FNA reference: FNA's own D3D device conventions (behavioral reference)
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Declares `D3D12SamplerCache::GetOrCreate()`, returning a GPU descriptor handle (heap-resident, not a COM object) for the given XNA sampler ordinals.

## Executive Verdict

**Needs attention — correct, real dynamic sampler support (the DX-119 upgrade); shares the project-wide `AddressW` architecture gap.**

## Checklist Results

### Systematic FNA parity gaps (architecture-level, not D3D12-specific)
**F1 (MEDIUM, see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** `AddressW` set equal to `AddressV`, correctly and honestly documented as matching `D3D11SamplerCache`'s own identical, pre-existing `IGraphicsBackend`-interface limitation (no `addressW` parameter in `ApplySamplerState()`'s signature) — not something this cache can fix unilaterally.

### API / FNA parity — real, current DX-119 upgrade
This is the file that actually delivers the dynamic sampler support `D3D12RootSignatureCache.hpp`'s stale class-level comment doesn't yet reflect (see that file's own report) — real, per-slot, runtime-settable `SamplerState`, matching D3D11's own capability.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

**F1 (MEDIUM, inherited, architecture-level):** `AddressW` unenforceable, shared with every other backend.

## Cross-File Observations

This class is the concrete evidence resolving `D3D12RootSignatureCache.hpp`'s stale-comment finding.

## Missing or Weak Tests

No dedicated test found exercising `SamplerState.AddressW` specifically.

## Positive Findings

Correctly delivers real, dynamic per-draw sampler support matching D3D11, despite the initial static-sampler-only design described in an now-outdated sibling header comment.

## Final Assessment

One MEDIUM architecture-level finding (inherited, project-wide); the file's own real contribution (dynamic sampler support) is correct.
