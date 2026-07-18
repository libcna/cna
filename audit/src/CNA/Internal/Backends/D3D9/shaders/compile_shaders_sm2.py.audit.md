# Audit: src/CNA/Internal/Backends/D3D9/shaders/compile_shaders_sm2.py

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/shaders/compile_shaders_sm2.py`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: Python build-time tool
- XNA/FNA relevance: Build-time tool: compiles the 6 vendored XNA Stock Effect .fx files to real D3D9 SM2 bytecode via the real d3dcompiler_47.dll (under Wine) and emits d3d9_shaders.hpp.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Build-time tool: compiles the 6 vendored XNA Stock Effect .fx files to real D3D9 SM2 bytecode via the real d3dcompiler_47.dll (under Wine) and emits d3d9_shaders.hpp.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Entry-point list is deliberately PARSED from the vendored `.fx` files' own `compile vs_2_0/ps_2_0 <entry>` statements rather than hand-maintained — the docstring explicitly cites a prior README table's own hand-typed list as having silently invented a wrong name, a real, avoided failure mode. Compile flags (`D3DCOMPILE_OPTIMIZATION_LEVEL3`, no others) are explicitly locked in as the exact configuration empirically proven (via `compare_against_fxb.py`) to give 61/66 byte-identical matches, with an explicit warning not to change them without re-running that comparison.

## Detailed Findings

Entry-point list is deliberately PARSED from the vendored `.fx` files' own `compile vs_2_0/ps_2_0 <entry>` statements rather than hand-maintained — the docstring explicitly cites a prior README table's own hand-typed list as having silently invented a wrong name, a real, avoided failure mode. Compile flags (`D3DCOMPILE_OPTIMIZATION_LEVEL3`, no others) are explicitly locked in as the exact configuration empirically proven (via `compare_against_fxb.py`) to give 61/66 byte-identical matches, with an explicit warning not to change them without re-running that comparison.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

Entry points parsed from the vendored source itself rather than hand-maintained, directly preventing a previously-real failure mode (a silently-wrong hand-typed name).

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
