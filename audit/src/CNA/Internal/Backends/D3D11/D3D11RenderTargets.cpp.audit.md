# Audit: src/CNA/Internal/Backends/D3D11/D3D11RenderTargets.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D11/D3D11RenderTargets.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ implementation (317 lines)
- Related header: `include/CNA/Internal/Backends/D3D11/D3D11RenderTargets.hpp` (same shard)
- XNA/FNA relevance: implements the 2 render-target backends
- Graphics backend relevance: D3D11-specific
- FNA reference: FNA's own D3D11 render-target/MSAA conventions
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Implements construction (color/resolve/depth texture creation, RTV/SRV/DSV creation, per-face RTV array for the
cube variant), `Bind*`/`Unbind*`, and `ResolveAndGenerateMipsEXT()`/`ResolveMsaaEXT()`.

## Executive Verdict

**Needs attention — otherwise excellent, thoroughly cross-verified MSAA/mip logic; confirms the cross-backend
whole-cube-mip-regeneration risk already flagged against the paired header.**

## Checklist Results

### Behavioral correctness / Logic
`ClampMultiSampleCount()` correctly never assumes a requested sample count is supported
(`CheckMultisampleQualityLevels`), matching this project's own DX-45 precedent. The MSAA color/resolve-texture
split (`colorTexture_` never sampled directly when MSAA; `resolveTexture_` is the real sampled/read-back target)
is correctly wired for both the 2D and cube variants, including the cube-specific constraint that
`D3D11_RESOURCE_MISC_TEXTURECUBE` cannot combine with `SampleDesc.Count > 1` on one resource (correctly handled by
making the MSAA cube array a plain, non-cube-flagged `Texture2DMSArray` used only as an RTV target).

### Systematic FNA parity gaps
**F1 (MEDIUM, confirmed):** `D3D11RenderTargetCubeBackend::UnbindAsRenderTarget()` (lines 306-315) calls
`context_->GenerateMips(srv_.Get())` unconditionally whenever `mipMap_` is true, after
`BindAsRenderTargetFace(face)` was used to render to only ONE face. Since `srv_` (when not MSAA) points at the
whole 6-face `texture_` resource, this regenerates mip chains for all 6 faces from whatever content each
currently holds — potentially still-uninitialized data for faces not yet rendered to in a genuine multi-face
cube-map-generation workflow. This exactly mirrors SdlGpu's own already-confirmed `TextureCube::SetData()`
instance of the same shape — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise. `ResolveMsaaEXT()`'s subresource math (`dstSubresource = activeFace_ * levelCount_`,
correctly `activeFace_` alone for the MSAA source since it has no mip levels) independently verified correct.

## Detailed Findings

**F1 (MEDIUM):** whole-cube mip regeneration on single-face unbind, `UnbindAsRenderTarget()` lines 309-312.

## Cross-File Observations

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the SdlGpu cross-reference.

## Missing or Weak Tests

No dedicated test found for a genuine multi-face mip-generation workflow.

## Positive Findings

Thorough, correct, independently-re-derived MSAA resolve and cube-face RTV logic across both the 2D and cube
variants — one of the more carefully-engineered render-target implementations reviewed in this audit.

## Final Assessment

One MEDIUM cross-backend architecture-level finding (F1); otherwise correct and well-engineered.
