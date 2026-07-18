# Audit: src/CNA/Internal/Backends/D3D9/shaders/d3d9_pbr_shaders.hpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/shaders/d3d9_pbr_shaders.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ header
- XNA/FNA relevance: Generated header embedding Pbr3D.hlsl/PbrSkinned3D.hlsl's compiled vs_3_0/ps_3_0 bytecode.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Generated header embedding Pbr3D.hlsl/PbrSkinned3D.hlsl's compiled vs_3_0/ps_3_0 bytecode.

## Executive Verdict

Healthy — structural review (899 lines).

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Consumed by `D3D9PbrDraw.cpp`'s shader-creation call sites; the SM3 targeting (not SM2) is consistent with the empirically-documented compiler-enforced reasons given in both `Pbr3D.hlsl`'s and `PbrSkinned3D.hlsl`'s own header comments (ps_2_0's 12-temp-register file and instruction budget are both too small for the live BRDF+TBN+4-texture-sample values these shaders need simultaneously).

## Detailed Findings

Consumed by `D3D9PbrDraw.cpp`'s shader-creation call sites; the SM3 targeting (not SM2) is consistent with the empirically-documented compiler-enforced reasons given in both `Pbr3D.hlsl`'s and `PbrSkinned3D.hlsl`'s own header comments (ps_2_0's 12-temp-register file and instruction budget are both too small for the live BRDF+TBN+4-texture-sample values these shaders need simultaneously).

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
