# Audit: src/CNA/Internal/Backends/D3D9/D3D9VertexDeclarations.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9VertexDeclarations.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements the stride-keyed D3DVERTEXELEMENT9 layout tables for every vertex format used across this backend (strides 16/20/24/28/32/48/52/56/68).
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements the stride-keyed D3DVERTEXELEMENT9 layout tables for every vertex format used across this backend (strides 16/20/24/28/32/48/52/56/68).

## Executive Verdict

Healthy — a genuinely candid, empirically-verified historical bug-fix documented in-file.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Header comment (lines 8-20) candidly documents a real, live-reproduced bug (D9-82): `COLOR0` was originally declared `D3DDECLTYPE_D3DCOLOR`, which MSDN documents as expecting ARGB-packed memory bytes and byte-swizzling them into RGBA register order — but XNA's own `Color.PackedValue` is R,G,B,A ascending, so feeding that native layout through `D3DDECLTYPE_D3DCOLOR` silently swapped R and B (empirically confirmed: opaque red 0xFF0000FF read back as opaque BLUE before the fix). `D3DDECLTYPE_UBYTE4N` (no reorder) is the correct type and is what every stride's `COLOR0` element now correctly uses. Every stride's byte-offset table is cross-referenced against `EasyGLGraphicsBackend.cpp`'s own `ApplyLayout()` cases for the CNA-custom-shader strides (48/56/68), ensuring cross-backend vertex-layout consistency.

## Detailed Findings

Header comment (lines 8-20) candidly documents a real, live-reproduced bug (D9-82): `COLOR0` was originally declared `D3DDECLTYPE_D3DCOLOR`, which MSDN documents as expecting ARGB-packed memory bytes and byte-swizzling them into RGBA register order — but XNA's own `Color.PackedValue` is R,G,B,A ascending, so feeding that native layout through `D3DDECLTYPE_D3DCOLOR` silently swapped R and B (empirically confirmed: opaque red 0xFF0000FF read back as opaque BLUE before the fix). `D3DDECLTYPE_UBYTE4N` (no reorder) is the correct type and is what every stride's `COLOR0` element now correctly uses. Every stride's byte-offset table is cross-referenced against `EasyGLGraphicsBackend.cpp`'s own `ApplyLayout()` cases for the CNA-custom-shader strides (48/56/68), ensuring cross-backend vertex-layout consistency.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

A candidly documented, empirically-reproduced (not just theorized) R/B-channel-swap bug-fix, with before/after verification described directly in the comment.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
