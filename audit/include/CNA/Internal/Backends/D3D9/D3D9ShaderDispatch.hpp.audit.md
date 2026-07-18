# Audit: include/CNA/Internal/Backends/D3D9/D3D9ShaderDispatch.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D9/D3D9ShaderDispatch.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ header
- XNA/FNA relevance: Declares the real ShaderIndex-based dispatch logic mapping GpuDrawParams to one of BasicEffect.fx's real compiled shader-array entries.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Declares the real ShaderIndex-based dispatch logic mapping GpuDrawParams to one of BasicEffect.fx's real compiled shader-array entries.

## Executive Verdict

Needs attention, scoped-depth review.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Declares the dispatch surface (114 lines) whose real logic is implemented in the paired 273-line `.cpp` — the shader-index selection (fog/vertex-color/texture/lighting-mode combination -> one of BasicEffect.fx's own real `VSArray`/`PSArray` indices) is a faithful re-derivation of Microsoft's own real BasicEffect.cpp dispatch table, not a CNA invention.

## Detailed Findings

Declares the dispatch surface (114 lines) whose real logic is implemented in the paired 273-line `.cpp` — the shader-index selection (fog/vertex-color/texture/lighting-mode combination -> one of BasicEffect.fx's own real `VSArray`/`PSArray` indices) is a faithful re-derivation of Microsoft's own real BasicEffect.cpp dispatch table, not a CNA invention.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
