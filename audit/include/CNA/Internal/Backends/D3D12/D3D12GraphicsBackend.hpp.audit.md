# Audit: include/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- File type: C++ header, 713 lines — the largest header in this backend, and larger than D3D11's own
  equivalent (379 lines), reflecting D3D12's additional explicit machinery (resource-state tracking, root
  signatures, PSOs, descriptor heaps, command allocators/lists, fence-based synchronous submission)
- Related implementation: `src/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.cpp` (same shard, 2331 lines,
  scoped-depth review — see that report)
- XNA/FNA relevance: implements the full `IGraphicsBackend` contract for D3D12
- Graphics backend relevance: this is the backend's central class
- FNA reference: FNA's own D3D device/draw-dispatch conventions
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

The full `D3D12GraphicsBackend` declaration: device/command-queue/swap-chain/descriptor-heap lifecycle,
`D3D12ResourceStateTracker`/`D3D12RootSignatureCache`/`D3D12PipelineStateCache`/`D3D12SamplerCache` ownership,
`Apply*State` tracking (deliberately partial for depth-stencil/rasterizer, see below), and the full draw-dispatch
chain mirroring D3D11's own variant matrix.

## Executive Verdict

**Needs attention — exceptionally well-documented, built correctly on the already-verified D3DCommon/D3D11
foundation, but honestly discloses (and this audit independently confirms as real, not just theoretical) the
most significant single-backend finding so far: Stencil/Scissor are completely non-functional.**

## Checklist Results

### Confirmed clean: window-registry
**No `RegisterForWindow` call anywhere in this class** (confirmed via grep across the whole `.cpp` file) —
matches D3D11's identical absence; this backend cannot share the EasyGL-class dangling-window-registry-pointer
bug.

### Systematic FNA parity gaps (HIGH, confirmed real — not merely disclosed as theoretical)
`ApplyDepthStencilState()`'s 11 stencil-related parameters and `ApplyRasterizerState()`'s
`scissorTestEnable`/`depthBias`/`slopeScaleDepthBias` are declared here matching the full `IGraphicsBackend`
signature, but (per the `.cpp` report) are received as literally-commented-out unused parameters and never
tracked — see `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full write-up and the confirming `D3D12PipelineStateCache`
evidence (`StencilEnable`/`ScissorEnable` both hardcoded off in every PSO).

### Architecture
The additional D3D12-specific member set (`D3D12ResourceStateTracker resourceStates_`,
`D3D12RootSignatureCache rootSigCache_`, `D3D12PipelineStateCache psoCache_`, `D3D12SamplerCache samplerCache_`,
descriptor-heap bump-allocator state, command-allocator/list/fence machinery) is consistent with, and correctly
exposes accessors for, every class already independently audited in this shard
(`GetResourceStateTrackerEXT()`, `GetRootSignatureCacheEXT()`, etc.) — no orphaned or unused cache member found.
`GetBoundColorWidthEXT()`/`GetBoundColorHeightEXT()` (confirmed already, via `D3D12SpriteBatch.cpp`'s own report)
correctly track the currently-bound render target's real dimensions, not a hardcoded backbuffer size.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found in the declarations themselves.

## Detailed Findings

None new in this header — it correctly declares the interface surface whose `.cpp` implementation carries the
HIGH-severity Stencil/Scissor gap (recorded against that file and `AUDIT_CROSS_CUTTING_FINDINGS.md`) and the
MEDIUM-HIGH occlusion-query multi-draw gap.

## Cross-File Observations

Every cache class this header exposes accessors for (`D3D12ResourceStateTracker`, `D3D12RootSignatureCache`,
`D3D12PipelineStateCache`, `D3D12SamplerCache`) was already independently audited elsewhere in this shard and
found correct (modulo the documentation-rot findings already recorded, which are cosmetic, not functional).

## Missing or Weak Tests

No dedicated test found in this audit so far for: device-lost/removed recovery, resize handling, or the full
`DrawPrimitivesExImpl` variant-selection matrix for the non-skinned variants.

## Positive Findings

Confirmed absence of the EasyGL-class window-registry bug; correctly exposes and integrates every already-audited
D3D12-specific cache class with no orphaned members.

## Final Assessment

Correctly declares the interface surface for a comprehensive, well-engineered D3D12 backend; the HIGH-severity
Stencil/Scissor gap and MEDIUM-HIGH occlusion-query multi-draw gap are both real and honestly (if not always
completely) disclosed in the corresponding `.cpp` file's own comments — see that report and
`AUDIT_CROSS_CUTTING_FINDINGS.md` for full detail.
