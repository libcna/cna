# Audit: src/CNA/Internal/Backends/D3D9/shaders/cna/Instanced3D.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/shaders/cna/Instanced3D.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: HLSL shader source
- XNA/FNA relevance: CNA's own NOXNA minimal hardware-instancing shader (no Microsoft Stock Effect has any per-instance-aware vertex shader).
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

CNA's own NOXNA minimal hardware-instancing shader (no Microsoft Stock Effect has any per-instance-aware vertex shader).

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
Minimal, correct: per-instance world matrix reconstructed from 4 `TEXCOORD` rows, `ViewProj` from 4 constant registers, flat diffuse-color output only — no lighting/texture/fog, matching every other backend's own equivalent `instanced3d` shader scope exactly (D3D11/Vulkan/Bgfx all chose the identical minimal scope). No Y-flip needed (D3D9's clip space matches XNA's own convention, same as D3D11/D3D12).

## Detailed Findings

Minimal, correct: per-instance world matrix reconstructed from 4 `TEXCOORD` rows, `ViewProj` from 4 constant registers, flat diffuse-color output only — no lighting/texture/fog, matching every other backend's own equivalent `instanced3d` shader scope exactly (D3D11/Vulkan/Bgfx all chose the identical minimal scope). No Y-flip needed (D3D9's clip space matches XNA's own convention, same as D3D11/D3D12).

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
