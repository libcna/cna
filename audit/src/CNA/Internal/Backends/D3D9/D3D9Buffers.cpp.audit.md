# Audit: src/CNA/Internal/Backends/D3D9/D3D9Buffers.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9Buffers.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements D3D9VertexBufferBackend/D3D9IndexBufferBackend real buffer upload via Lock/Unlock.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements D3D9VertexBufferBackend/D3D9IndexBufferBackend real buffer upload via Lock/Unlock.

## Executive Verdict

Needs attention — confirms the 3rd instance of the NoOverwrite architecture gap directly at the source level.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
`Upload()` (line 80) calls `buffer_->Lock(0, byteCount, &locked, LockFlagsFor(options))` — hardcoded offset 0 regardless of `options`, identical shape to D3D11/EasyGL's own confirmed gap. A fresh (just-recreated) buffer correctly forces `Discard` regardless of the caller's requested option (line 109) — a sensible, correct optimization (nothing is "in flight" on a brand-new buffer, so `NoOverwrite`'s driver-sync-skip promise is safe either way).

## Detailed Findings

`Upload()` (line 80) calls `buffer_->Lock(0, byteCount, &locked, LockFlagsFor(options))` — hardcoded offset 0 regardless of `options`, identical shape to D3D11/EasyGL's own confirmed gap. A fresh (just-recreated) buffer correctly forces `Discard` regardless of the caller's requested option (line 109) — a sensible, correct optimization (nothing is "in flight" on a brand-new buffer, so `NoOverwrite`'s driver-sync-skip promise is safe either way).

## Cross-File Observations

See `D3D9Buffers.hpp`'s own report and `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full 3-backend writeup of this architecture-level gap.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
