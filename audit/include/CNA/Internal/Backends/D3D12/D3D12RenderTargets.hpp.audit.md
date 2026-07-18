# Audit: include/CNA/Internal/Backends/D3D12/D3D12RenderTargets.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12RenderTargets.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: `RenderTarget2D`/`RenderTargetCube` backend contracts
- Graphics backend relevance: D3D12-specific
- FNA reference: FNA's own D3D device conventions (behavioral reference)
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Declares `D3D12RenderTargetBackend`/`D3D12RenderTargetCubeBackend`, both with optional MSAA and mip generation implemented via manual CPU-side box-filter downsampling (D3D12 has no built-in `GenerateMips()` equivalent to D3D11's, unlike D3D11 which uses a real device call).

## Executive Verdict

**Healthy — a genuinely valuable positive finding on cube-face mip regeneration.**

## Checklist Results

### Systematic FNA parity gaps — POSITIVE COUNTER-EXAMPLE
**Confirmed, see `AUDIT_CROSS_CUTTING_FINDINGS.md`: `D3D12RenderTargetCubeBackend::GenerateMipsEXT()` correctly regenerates mips for ONLY the actually-drawn-to face** (`face = activeFace_`, correct per-face subresource indexing), unlike the already-confirmed whole-cube-regeneration bug shared by SdlGpu's `TextureCube` and D3D11's own `RenderTargetCube` — D3D12 gets this right despite sharing almost every other finding with its sibling D3D11 backend.

### Architecture
The manual CPU-side box-filter mip downsampling (readback a level, downsample, upload the next level) is a real, substantive, and more expensive implementation choice than D3D11's single `GenerateMips()` device call — correctly documented as necessary given D3D12 has no equivalent built-in.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None — a positive finding, not a defect.

## Cross-File Observations

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the cross-backend mip-regeneration comparison.

## Missing or Weak Tests

No dedicated test found exercising a genuine multi-face cube-map mip-generation workflow that would prove this correct-scoping finding end-to-end.

## Positive Findings

Correct, narrower-than-D3D11 mip-regeneration scope for cube render targets — the only backend in this audit confirmed to get this specific detail right.

## Final Assessment

No issues found; a genuine positive cross-backend finding.
