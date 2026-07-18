# Audit: include/CNA/Internal/Backends/D3D9/D3D9Buffers.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D9/D3D9Buffers.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ header
- XNA/FNA relevance: Declares D3D9VertexBufferBackend/D3D9IndexBufferBackend — real IDirect3DVertexBuffer9/IDirect3DIndexBuffer9 wrappers.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Declares D3D9VertexBufferBackend/D3D9IndexBufferBackend — real IDirect3DVertexBuffer9/IDirect3DIndexBuffer9 wrappers.

## Executive Verdict

Needs attention — instantiates an already-recorded, now-3-instance cross-cutting architecture gap.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Header comment (lines 32-34) correctly documents the `SetDataOptions` -> `D3DLOCK_*` flag mapping (`Discard`->`D3DLOCK_DISCARD`, `NoOverwrite`->`D3DLOCK_NOOVERWRITE`). **Confirmed in the paired `.cpp`: no destination-offset parameter anywhere in the upload call chain** — the same `SetDataOptions::NoOverwrite` architecture-level gap already confirmed in D3D11/EasyGL, now a 3rd instance (see `AUDIT_CROSS_CUTTING_FINDINGS.md`).

## Detailed Findings

Header comment (lines 32-34) correctly documents the `SetDataOptions` -> `D3DLOCK_*` flag mapping (`Discard`->`D3DLOCK_DISCARD`, `NoOverwrite`->`D3DLOCK_NOOVERWRITE`). **Confirmed in the paired `.cpp`: no destination-offset parameter anywhere in the upload call chain** — the same `SetDataOptions::NoOverwrite` architecture-level gap already confirmed in D3D11/EasyGL, now a 3rd instance (see `AUDIT_CROSS_CUTTING_FINDINGS.md`).

## Cross-File Observations

3rd confirmed instance (after D3D11, EasyGL) of the missing-destination-offset `SetDataOptions::NoOverwrite` architecture gap — strong evidence this is a project-wide interface limitation.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
