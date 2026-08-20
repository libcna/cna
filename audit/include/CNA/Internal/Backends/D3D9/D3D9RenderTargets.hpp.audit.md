# Audit: include/CNA/Internal/Backends/D3D9/D3D9RenderTargets.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D9/D3D9RenderTargets.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ header
- XNA/FNA relevance: Declares D3D9RenderTargetBackend/D3D9RenderTargetCubeBackend — real D3D9 offscreen render targets.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Declares D3D9RenderTargetBackend/D3D9RenderTargetCubeBackend — real D3D9 offscreen render targets.

## Executive Verdict

Needs attention — 1 honestly-disclosed scope gap, otherwise healthy.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**Mip-chain auto-generation on unbind is explicitly, candidly NOT implemented** (header comment, lines 28-30): `mipMap` is accepted but only a single level is ever allocated — a genuine, named gap (plans/plan_dx9.md's own D9-53 row), not a silent divergence. This means D3D9 is a 3rd, distinct outcome for the cube-mip-regeneration bug family already tracked cross-cuttingly: D3D12 regenerates correctly (only the active face), SdlGpu/D3D11 regenerate the whole cube incorrectly, and D3D9 simply doesn't support mips at all for render targets (nothing to regenerate incorrectly, an honest scope cut rather than a bug). MSAA is correctly clamped against real `CheckDeviceMultiSampleType()` results (never assumed), and `D3D9RenderTargetCubeBackend` deliberately omits MSAA support entirely, matching D3D11's own precedent.

## Detailed Findings

**Mip-chain auto-generation on unbind is explicitly, candidly NOT implemented** (header comment, lines 28-30): `mipMap` is accepted but only a single level is ever allocated — a genuine, named gap (plans/plan_dx9.md's own D9-53 row), not a silent divergence. This means D3D9 is a 3rd, distinct outcome for the cube-mip-regeneration bug family already tracked cross-cuttingly: D3D12 regenerates correctly (only the active face), SdlGpu/D3D11 regenerate the whole cube incorrectly, and D3D9 simply doesn't support mips at all for render targets (nothing to regenerate incorrectly, an honest scope cut rather than a bug). MSAA is correctly clamped against real `CheckDeviceMultiSampleType()` results (never assumed), and `D3D9RenderTargetCubeBackend` deliberately omits MSAA support entirely, matching D3D11's own precedent.

## Cross-File Observations

A 3rd distinct outcome (honest non-support, not a bug) for the cube-mip-regeneration pattern already tracked for D3D12 (correct)/SdlGpu+D3D11 (buggy) in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

No issues found.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
