# Audit: src/CNA/Internal/Backends/D3D9/shaders/cna/SkinnedVertexColor3D.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/shaders/cna/SkinnedVertexColor3D.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: HLSL shader source
- XNA/FNA relevance: CNA's own NOXNA SkinnedEffect+vertex-color custom shader (real XNA SkinnedEffect has no vertex-color input at all).
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

CNA's own NOXNA SkinnedEffect+vertex-color custom shader (real XNA SkinnedEffect has no vertex-color input at all).

## Executive Verdict

Needs attention — confirms 2 already-recorded cross-cutting defect patterns directly, plus 1 genuine positive finding.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**Object-space-only fog confirmed** (line 102-106): fog factor computed from raw, untransformed local-space `vin.Position.z`, not the World/View-transformed depth this same backend's own `ComputeFogVectorEXT()` (used by every vendored stock effect) correctly computes — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s refined writeup. The formula's own arithmetic shape is otherwise correct (`(z+FogEnd)/(FogEnd-FogStart)`, matching FNA's real convention). **Skinned-normal-transform: applies `(float3x3)World` (plain, not inverse-transpose) AFTER the skin-local rotation** (line 98) — the narrower "raw World instead of inverse-transpose" variant already confirmed in EasyGL/SdlGpu/D3D9-PBR/D3D11-D3D12-PBR-skinned. This file's own header comment is unusually candid: explicitly documents this as a DELIBERATE correctness IMPROVEMENT over EasyGL's own `EnsureSkinnedProgram()` (which omits World entirely for its Normal), citing the real Microsoft SkinnedEffect.fx's own convention as justification — a genuinely well-reasoned deviation, not a silent one. **EmissiveColor register (line 114) is confirmed, via the paired `D3D9SkinnedVertexColorDraw.cpp`, to receive the real pre-folded Ambient+Emissive value** — the 4th independent backend (after EasyGL/Bgfx/SdlGpu) confirming this convention, refining the Vulkan/D3D11/D3D12 bug's likely true root cause (see cross-cutting doc).

## Detailed Findings

**Object-space-only fog confirmed** (line 102-106): fog factor computed from raw, untransformed local-space `vin.Position.z`, not the World/View-transformed depth this same backend's own `ComputeFogVectorEXT()` (used by every vendored stock effect) correctly computes — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s refined writeup. The formula's own arithmetic shape is otherwise correct (`(z+FogEnd)/(FogEnd-FogStart)`, matching FNA's real convention). **Skinned-normal-transform: applies `(float3x3)World` (plain, not inverse-transpose) AFTER the skin-local rotation** (line 98) — the narrower "raw World instead of inverse-transpose" variant already confirmed in EasyGL/SdlGpu/D3D9-PBR/D3D11-D3D12-PBR-skinned. This file's own header comment is unusually candid: explicitly documents this as a DELIBERATE correctness IMPROVEMENT over EasyGL's own `EnsureSkinnedProgram()` (which omits World entirely for its Normal), citing the real Microsoft SkinnedEffect.fx's own convention as justification — a genuinely well-reasoned deviation, not a silent one. **EmissiveColor register (line 114) is confirmed, via the paired `D3D9SkinnedVertexColorDraw.cpp`, to receive the real pre-folded Ambient+Emissive value** — the 4th independent backend (after EasyGL/Bgfx/SdlGpu) confirming this convention, refining the Vulkan/D3D11/D3D12 bug's likely true root cause (see cross-cutting doc).

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

A genuinely well-reasoned, explicitly-documented normal-transform improvement over EasyGL's own precedent (applies World rotation post-skin, unlike backends that omit it entirely); correctly forwards the pre-folded Ambient+Emissive value.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
