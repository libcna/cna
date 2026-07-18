# Audit: src/CNA/Internal/Backends/D3D12/D3D12PipelineStateCache.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D12/D3D12PipelineStateCache.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d12` shard
- XNA/FNA relevance: Implements the PSO-building logic
- Graphics backend relevance: D3D12-specific
- FNA reference: N/A
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Implements `GetOrCreate()`: builds `D3D12_GRAPHICS_PIPELINE_STATE_DESC` from cached DXBC bytecode, D3D12 input-layout arrays, and blend/depth/rasterizer state.

## Executive Verdict

**Needs attention — confirms the HIGH-severity Stencil/Scissor gap at the concrete implementation level.**

## Checklist Results

### API / FNA parity
**F1 (HIGH, confirmed):** `ds.StencilEnable = FALSE;` (line 99) is unconditionally hardcoded for every PSO — stencil testing can never be enabled regardless of what `DepthStencilState` a game applies. `RasterizerState.ScissorEnable` is never set anywhere in this function, left at its zero-initialized `FALSE` default via `D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};` — scissor testing can never be enabled either. Both confirmed via direct line-by-line reading, not inferred.

### Behavioral correctness / Logic
`DeriveBlendEnable()`'s Opaque-detection heuristic (`colorSrcBlend==0 && colorDstBlend==1`) is correctly, explicitly cross-referenced against `D3D11BlendStateCache`'s identical logic in the sibling backend — independently verified consistent. Depth state (`DepthEnable`/`DepthWriteMask`/`DepthFunc`) IS correctly, fully dynamic and functional — only stencil/scissor are the gap.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (HIGH):** `StencilEnable`/`ScissorEnable` both hardcoded off, lines 99 and (by omission) the zero-initialized rasterizer desc — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

Directly corroborated by `D3D12GraphicsBackend.cpp`'s `ApplyDepthStencilState()`/`ApplyRasterizerState()`, which discard the corresponding parameters entirely rather than tracking them for a future PSO-key extension.

## Missing or Weak Tests

No dedicated test found for stencil/scissor behavior on this backend.

## Positive Findings

Correct, fully-functional depth-testing support; correct, verified-consistent blend-Opaque-detection heuristic shared with D3D11.

## Final Assessment

One HIGH-severity, confirmed-at-the-implementation-level finding (Stencil/Scissor non-functional); depth testing and blending are both correct.
