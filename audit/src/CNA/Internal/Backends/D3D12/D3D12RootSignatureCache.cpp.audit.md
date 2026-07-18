# Audit: src/CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: N/A directly
- Graphics backend relevance: D3D12-specific
- FNA reference: N/A
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Implements `GetOrCreate()`: builds root CBV parameters, per-texture single-descriptor SRV tables, and (per DX-119) per-sampler single-descriptor dynamic sampler tables.

## Executive Verdict

**Healthy — confirms the header's stale-documentation gap is purely a comment issue, not a functional one.**

## Checklist Results

### API / FNA parity
**Confirms real, current dynamic `SamplerState` support**: `NumStaticSamplers = 0`, `pStaticSamplers = nullptr` (lines 107-108), with real `D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE` sampler parameters populated at draw time via `D3D12SamplerCache` (per the DX-119 comment, lines 72-79) — correctly superseding the older static-sampler design the paired header's class-level comment still describes.

### Behavioral correctness / Logic
The fixed-capacity `std::vector::reserve()` calls for `srvRanges`/`samplerRanges` (lines 50, 81) before taking each element's address for `pDescriptorRanges` is correct and necessary — without the upfront reservation, a `push_back()`-triggered reallocation would invalidate every previously-taken pointer, a genuine, easy-to-get-wrong C++ pitfall this code correctly avoids (and its own comment explicitly explains why).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None — corrects the impression the paired header's stale comment might give.

## Cross-File Observations

See paired header's report for the documentation-rot finding this file's actual behavior resolves.

## Missing or Weak Tests

No dedicated test found exercising a non-default `SamplerState` (e.g. `Point`/`Wrap` vs. the old hardcoded `Linear`/`Wrap` default) specifically on D3D12.

## Positive Findings

Correct, careful avoidance of a real C++ reallocation-invalidation pitfall via upfront `reserve()`; genuinely current, working dynamic sampler support.

## Final Assessment

No issues found; confirms the paired header's documentation-rot finding is cosmetic only.
