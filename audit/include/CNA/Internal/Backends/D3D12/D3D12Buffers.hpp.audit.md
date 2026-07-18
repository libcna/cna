# Audit: include/CNA/Internal/Backends/D3D12/D3D12Buffers.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12Buffers.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: `VertexBuffer`/`IndexBuffer`/`DynamicVertexBuffer`/`DynamicIndexBuffer` backend contracts
- Graphics backend relevance: D3D12-specific
- FNA reference: FNA's own D3D device conventions (behavioral reference)
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Declares `D3D12VertexBufferBackend`/`D3D12IndexBufferBackend`: DEFAULT-heap (GPU-resident) buffers, uploaded via a fresh UPLOAD-heap staging buffer + `CopyBufferRegion` + `D3D12ResourceStateTracker`-driven transition on every `SetData()` call.

## Executive Verdict

**Needs attention — correct, but a confirmed performance regression (every write is a full synchronous stall) relative to every other backend.**

## Checklist Results

### Systematic FNA parity gaps (performance)
**F1 (MEDIUM, see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** `SetDataWithOptions()` takes `SetDataOptions /*options*/` as a literally-unused parameter (confirmed in the `.cpp`) — `Discard`/`NoOverwrite`/`None` are all treated identically, and every call performs a full synchronous GPU stall (create staging buffer, copy, submit, wait on a fence) regardless of hint. Correctness is unaffected; this is purely a performance characteristic, honestly consistent with this backend's other explicitly-synchronous-for-now design choices, but a real regression relative to every other backend's attempt (however architecturally limited) at a no-stall streaming path.

### Architecture
The DEFAULT-heap-plus-staging-buffer-plus-explicit-transition pattern is the textbook-correct D3D12 approach and correctly integrates with `D3D12ResourceStateTracker`.

### C++ correctness / Memory/resource lifetime / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (MEDIUM):** `SetDataOptions` completely ignored, always fully-synchronous — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

See `.cpp` report for the `TrackResource()`-on-every-reallocation discipline, consistent with `D3D12ResourceStateTracker`'s own audited design.

## Missing or Weak Tests

No dedicated test found measuring or asserting the per-`SetData()` synchronization cost.

## Positive Findings

Correct, complete resource-state-tracker integration on every buffer (re)allocation.

## Final Assessment

One MEDIUM performance-only finding (F1); functionally correct.
