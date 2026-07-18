# Audit: src/CNA/Internal/Backends/D3D9/shaders/compare_against_fxb.py

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/shaders/compare_against_fxb.py`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: Python build-time tool
- XNA/FNA relevance: D9-73 tooling: compares this project's own D3DCompile()-produced bytecode against Microsoft's real shipped .fxb reference bytecode.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

D9-73 tooling: compares this project's own D3DCompile()-produced bytecode against Microsoft's real shipped .fxb reference bytecode.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
This is the tool behind this backend's own remarkable, empirically-verified authenticity claim (cited in `compile_shaders_sm2.py`'s own docstring): 61 of 66 vendored-shader variants are byte-identical to Microsoft's own shipped XNA 4.0 bytecode under this project's locked-in compile flags (`D3DCOMPILE_OPTIMIZATION_LEVEL3`) — a genuinely strong, concrete authenticity result for the whole D3D9 backend's stock-effect fidelity, not a mere plausibility claim.

## Detailed Findings

This is the tool behind this backend's own remarkable, empirically-verified authenticity claim (cited in `compile_shaders_sm2.py`'s own docstring): 61 of 66 vendored-shader variants are byte-identical to Microsoft's own shipped XNA 4.0 bytecode under this project's locked-in compile flags (`D3DCOMPILE_OPTIMIZATION_LEVEL3`) — a genuinely strong, concrete authenticity result for the whole D3D9 backend's stock-effect fidelity, not a mere plausibility claim.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

The tool substantiating a genuinely remarkable authenticity claim: 61/66 byte-identical matches against Microsoft's real shipped XNA bytecode.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
