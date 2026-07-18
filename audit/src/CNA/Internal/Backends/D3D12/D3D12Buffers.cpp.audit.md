# Audit: src/CNA/Internal/Backends/D3D12/D3D12Buffers.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D12/D3D12Buffers.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: Implements the 2 buffer backends
- Graphics backend relevance: D3D12-specific
- FNA reference: FNA's own D3D device conventions (behavioral reference)
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Implements `EnsureCapacity()`/`UploadAndCopy()` for both buffer types via DEFAULT-heap+UPLOAD-heap-staging, and the public `SetData*` overloads.

## Executive Verdict

**Mostly healthy — confirms the F1 performance finding at the implementation level; otherwise correct.**

## Checklist Results

### Behavioral correctness / Logic
Every `EnsureCapacity()` reallocation correctly re-registers the fresh buffer with `D3D12ResourceStateTracker::TrackResource()` (line 111/188), with an explicit, accurate comment confirming the theoretical stale-tracked-state-after-address-reuse risk (already investigated in the `D3D12ResourceStateTracker` report) is a non-issue here since the old `ComPtr` has already released its reference by the time the new one is tracked. `D3D12IndexBufferBackend`'s 16-bit/32-bit mismatch guard mirrors D3D11's own defensive check.

### Systematic FNA parity gaps (performance)
**F1 (MEDIUM, confirmed):** `SetDataWithOptions()`'s `options` parameter is genuinely unused (line 141) — every call unconditionally does the full `CreateAndFillUploadBuffer` → `CopyBufferRegion` → synchronous submit-and-wait sequence.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (MEDIUM):** confirmed unused `SetDataOptions` parameter, always-synchronous upload.

## Cross-File Observations

See paired header's report; consistent with `D3D12ResourceStateTracker`'s own verified-safe TrackResource-on-every-creation discipline.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Correct, defensive 16-bit/32-bit mismatch guard; correct resource-state-tracker integration on every reallocation.

## Final Assessment

One MEDIUM performance-only finding; otherwise correct.
