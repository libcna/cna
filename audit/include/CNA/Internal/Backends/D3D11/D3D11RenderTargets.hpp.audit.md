# Audit: include/CNA/Internal/Backends/D3D11/D3D11RenderTargets.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11RenderTargets.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (169 lines)
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11RenderTargets.cpp` (same shard)
- XNA/FNA relevance: `RenderTarget2D`/`RenderTargetCube` backend contracts (MSAA, mip generation, depth-stencil)
- Graphics backend relevance: D3D11-specific
- FNA reference: FNA's own D3D11 render-target/MSAA-resolve conventions
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Declares `D3D11RenderTargetBackend`/`D3D11RenderTargetCubeBackend`, both supporting optional depth-stencil,
optional device-queried MSAA (with automatic resolve-on-unbind), and optional full mip-chain regeneration.

## Executive Verdict

**Needs attention — comprehensive, well-engineered design; one confirmed cross-backend architecture-level
mip-regeneration risk (2nd instance found in this audit).**

## Checklist Results

### API / FNA parity
MSAA-and-mip-chain mutual exclusivity is correctly and consistently enforced in both classes
(`mipMap_ = mipMap && !isMsaa_`), matching this project's own established EasyGL/Vulkan precedent (a mip chain
needs `GenerateMips()`, which requires a single-sample source).

### Systematic FNA parity gaps
**F1 (MEDIUM, confirmed 2nd instance of a cross-backend pattern — see `AUDIT_CROSS_CUTTING_FINDINGS.md`):**
`D3D11RenderTargetCubeBackend::UnbindAsRenderTarget()` (see `.cpp` report) regenerates mips for the WHOLE 6-face
cube resource whenever `mipMap_` is true, even though only the single most-recently-bound face's content actually
changed. First confirmed in SdlGpu's `TextureCube::SetData()` path; this is the same architectural shape recurring
in a second backend and a second resource type (`RenderTargetCube` here, vs. `TextureCube` there).

### Architecture
The owner-backend (`D3D11GraphicsBackend*`) non-owning pointer pattern for restoring the back-buffer on unbind is
correctly documented and matches `VulkanRenderTargetBackend`'s own identical pattern for the identical reason.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (MEDIUM):** whole-cube mip regeneration on any single-face unbind — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

See `.cpp` report and `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found exercising a genuine multi-face `RenderTargetCube` mip-generation workflow (rendering to
each face in sequence, then checking every face's mip chain) that would surface F1.

## Positive Findings

Comprehensive, correctly-documented MSAA support (device-queried, never assumed) matching this project's own
established DX-45 precedent; correct depth-stencil-optional design.

## Final Assessment

One MEDIUM cross-backend architecture-level finding (F1); otherwise a well-engineered, correctly-documented
design.
