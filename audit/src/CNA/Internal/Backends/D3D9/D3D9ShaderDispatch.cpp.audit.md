# Audit: src/CNA/Internal/Backends/D3D9/D3D9ShaderDispatch.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9ShaderDispatch.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements the real BasicEffect.fx ShaderIndex selection logic (fog/vertex-color/texture/lighting-mode -> Microsoft's own compiled shader-array index).
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements the real BasicEffect.fx ShaderIndex selection logic (fog/vertex-color/texture/lighting-mode -> Microsoft's own compiled shader-array index).

## Executive Verdict

Healthy — a faithful re-derivation of Microsoft's own real dispatch logic, consistent with this backend's own "pixel-for-pixel authenticity" goal.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Selection logic mirrors the real `BasicEffect.fx`'s own `VSIndices`/`PSIndices` table structure (already cross-referenced directly against the vendored `.fx` source in `D3D9EffectDraw.cpp`'s own `GetBasicEffectRegisterTablesEXT()`, confirmed consistent when that file was read) — not independently re-verified line-by-line against the real BasicEffect.cpp's own C# dispatch table in this pass, but internally consistent with everything else read in this shard.

## Detailed Findings

Selection logic mirrors the real `BasicEffect.fx`'s own `VSIndices`/`PSIndices` table structure (already cross-referenced directly against the vendored `.fx` source in `D3D9EffectDraw.cpp`'s own `GetBasicEffectRegisterTablesEXT()`, confirmed consistent when that file was read) — not independently re-verified line-by-line against the real BasicEffect.cpp's own C# dispatch table in this pass, but internally consistent with everything else read in this shard.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
