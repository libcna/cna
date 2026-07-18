# Audit: src/CNA/Internal/Backends/D3D11/D3D11Buffers.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D11/D3D11Buffers.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ implementation (180 lines)
- Related header: `include/CNA/Internal/Backends/D3D11/D3D11Buffers.hpp` (same shard)
- XNA/FNA relevance: implements the vertex/index buffer backends
- Graphics backend relevance: D3D11-specific
- FNA reference: FNA's own D3D11 buffer conventions
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Implements `EnsureCapacity()`/`Upload()` for both vertex and index buffers, plus
`SetData()`/`SetDataWithOptions()`/`SetData16/32(WithOptions)`.

## Executive Verdict

**Mostly healthy — correct buffer-growth logic; shares the `NoOverwrite` architecture-level risk already recorded
against the paired header.**

## Checklist Results

### Behavioral correctness / Logic
`EnsureCapacity()` correctly grows to `max(requiredBytes, capacityBytes)` and never shrinks, avoiding needless
reallocation for repeat same-size `SetData()` calls — independently traced and confirmed correct for both the
vertex and index variants.
`D3D11IndexBufferBackend::Upload()` correctly rejects a `SetData16` call on a 32-bit buffer (and vice versa) with
a clear, named exception (`dataIsThirtyTwoBit != thirtyTwoBit_`) rather than silently misinterpreting the byte
layout — a good defensive check.
`indexCount_` is correctly derived from `byteCount / elementSize` (line 157) rather than trusting a separately-passed
count that could disagree with the actual byte count.

### Systematic FNA parity gaps
Shares the `NoOverwrite`-always-writes-from-offset-0 architecture gap already recorded against the paired header
and in `AUDIT_CROSS_CUTTING_FINDINGS.md` — `MapTypeFor()` (lines 24-29) correctly implements the *mapping* from
`SetDataOptions` to `D3D11_MAP`, but every `Upload()` call always `Map()`s the whole buffer and `memcpy`s from
byte 0, regardless of which map type was selected.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

Inherits F1 (MEDIUM, `NoOverwrite` architecture-level risk) from the paired header — see that report and
`AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

See `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found exercising the 16-bit/32-bit mismatch exception path, or a genuine multi-draw
`NoOverwrite` streaming sequence.

## Positive Findings

Correct, defensive 16-bit/32-bit mismatch guard with a clear, named exception message; correct capacity-derivation
from actual byte count rather than a separately-trusted parameter.

## Final Assessment

Correct implementation of its documented contract; inherits the architecture-level `NoOverwrite` risk from the
paired header.
