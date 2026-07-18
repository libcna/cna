# Audit: src/CNA/Internal/Backends/D3D9/D3D9ConstantTable.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9ConstantTable.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements ParseConstantTableEXT() — a low-level, hand-rolled parser for D3D9's real CTAB shader-constant-table binary format.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements ParseConstantTableEXT() — a low-level, hand-rolled parser for D3D9's real CTAB shader-constant-table binary format.

## Executive Verdict

Healthy — genuinely careful, well-defended low-level binary parsing with a candidly documented historical bug-fix.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Header comment (lines 53-61) candidly documents a real, previously-shipped off-by-4 bug in this exact function (constant-table offsets are relative to 4 bytes PAST the 'CTAB' FourCC, not the FourCC's own position) — caught via cross-validation against `D3DDisassemble()`'s own independent output, not merely assumed from documentation. A defensive bounds check (line 95: `if (numConstants > dataSize / kConstantInfoStride) return result;`) prevents an unbounded `reserve()` from throwing `std::bad_alloc` on malformed/garbled input, returning the promised empty result instead — a genuine, thoughtful robustness fix, also empirically motivated (found via this file's own mutation-testing pass per the comment).

## Detailed Findings

Header comment (lines 53-61) candidly documents a real, previously-shipped off-by-4 bug in this exact function (constant-table offsets are relative to 4 bytes PAST the 'CTAB' FourCC, not the FourCC's own position) — caught via cross-validation against `D3DDisassemble()`'s own independent output, not merely assumed from documentation. A defensive bounds check (line 95: `if (numConstants > dataSize / kConstantInfoStride) return result;`) prevents an unbounded `reserve()` from throwing `std::bad_alloc` on malformed/garbled input, returning the promised empty result instead — a genuine, thoughtful robustness fix, also empirically motivated (found via this file's own mutation-testing pass per the comment).

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

A candidly documented historical off-by-4 bug-fix, cross-validated against an independent disassembler; a genuine, tested defensive bounds check against malformed input.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
