# Audit: src/CNA/Internal/Backends/D3D12/D3D12OcclusionQuery.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D12/D3D12OcclusionQuery.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: Implements the occlusion query backend
- Graphics backend relevance: D3D12-specific
- FNA reference: FNA's own D3D device conventions (behavioral reference)
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Implements the query-heap/readback-buffer construction, `Begin()`/`End()` (marking the query heap active on the owner, then resolving after the owner's draw call(s) recorded the actual `BeginQuery`/`EndQuery`), and `PixelCount()`.

## Executive Verdict

**Needs attention — confirms the multi-draw gap at the implementation level; otherwise correct and well-documented, including a genuine empirical finding.**

## Checklist Results

### API / FNA parity
**F1 (MEDIUM-HIGH, confirmed):** `Begin()`'s own comment explicitly states this design is "correct for exactly one draw call between Begin()/End()" — see the paired header's report and `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### Positive/documentation quality
The comment's account of a genuinely useful empirical finding — `BeginQuery`/`EndQuery` must be recorded within the SAME command-list submission as the draw(s) they bracket (a Vulkan/vkd3d-proton requirement), confirmed by reproducing `PixelCount()` reporting 0 for a visible full-viewport triangle before this was understood — is a valuable, concrete piece of engineering history worth preserving.

### Behavioral correctness / Logic
`PixelCount()`'s `Map`/`memcpy`/`Unmap` readback correctly uses an empty `writtenRange` (`{0, 0}`) since the CPU never writes through this mapping — a correct, minor D3D12 API nicety (telling the driver no CPU writes need to be flushed back).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (MEDIUM-HIGH):** confirmed multi-draw overwrite bug — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

See `D3D12GraphicsBackend.cpp`'s report for the 4 draw-recording call sites confirming this.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Valuable, concrete empirical-finding documentation (the same-command-list BeginQuery/EndQuery requirement); correct minor D3D12 API usage (empty written-range on a read-only mapping).

## Final Assessment

One MEDIUM-HIGH, confirmed correctness gap (F1); otherwise correct, with genuinely valuable engineering documentation.
