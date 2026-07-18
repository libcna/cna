# Audit: src/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.cpp`
- Audit status: AUDITED (scoped-depth review — see below; matches the standard already applied to this audit's
  other largest files, `EasyGLGraphicsBackend.cpp` 4733 lines, `WebGPUGraphicsBackend.cpp` 8805 lines,
  `D3D11GraphicsBackend.cpp` 1846 lines)
- Subsystem: `backend-d3d12` shard
- File type: C++ implementation, 2331 lines — the largest single file in this backend, and larger than D3D11's
  own equivalent (1846 lines)
- Related header: `include/CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp` (same shard)
- XNA/FNA relevance: implements the entire D3D12 backend's device lifecycle and draw dispatch
- Graphics backend relevance: this is the central D3D12 class
- FNA reference: FNA's own D3D device/draw-dispatch conventions
- Main related tests: `examples-tests-d3d12` (not yet audited)

## Purpose

Implements device/command-queue/swap-chain/descriptor-heap creation, fence-based synchronous command-list
submission (`ExecuteCommandListAndWaitEXT`), `Apply*State`, occlusion-query bracket recording in every draw
method, and the full `DrawPrimitivesExImpl` variant-dispatch chain (mirroring D3D11's own, reusing the same
`D3DCommon` shader variants and constant-buffer layouts).

## Executive Verdict

**Needs attention, scoped-depth review.** Constructor, `RegisterForWindow` absence, `ApplyDepthStencilState`/
`ApplyRasterizerState`'s parameter-discarding (the Stencil/Scissor gap), all 4 draw methods' occlusion-query
bracket recording, and the skinned/PBR ambient-vs-emissive constant-buffer fill logic were all read and verified.
The remaining ~1800 lines (full device/swap-chain/descriptor-heap creation detail, device-lost recovery, resize
handling) were not exhaustively traced given this file's size.

## Checklist Results

### Confirmed clean: constructor/window-registry
**No `RegisterForWindow` call anywhere in this file** (confirmed via grep) — matches D3D11's identical absence;
this backend cannot share the EasyGL-class window-registry bug.

### Systematic FNA parity gaps — HIGH, confirmed real
**`ApplyDepthStencilState()` (lines 1210-1227) and `ApplyRasterizerState()` (lines 1229-1239) receive all
stencil-related and `scissorTestEnable`/`depthBias`/`slopeScaleDepthBias` parameters as literally-named
commented-out unused parameters** (`bool /*stencilEnable*/`, etc.) and only track `depthEnable`/`depthWriteEnable`/
`depthFunc` and `cullMode`/`fillMode` respectively. Both functions' own comments honestly cite "DX-118" and
correctly cross-reference `D3D12PipelineStateCache`'s own documented "first key/desc" scope cut — this is a
coherent, deliberate, consistently-applied gap across both files, not an inconsistency. **Confirmed at the
concrete implementation level** (not just inferred from the PSO cache's own hardcoded `StencilEnable=FALSE`) that
Stencil/Scissor testing is completely non-functional on this backend — see `AUDIT_CROSS_CUTTING_FINDINGS.md` for
the full write-up.

### Systematic FNA parity gaps — MEDIUM-HIGH, confirmed real
**All 4 draw-recording methods (`DrawColoredPrimitives`, `DrawIndexedColoredPrimitives`, `DrawPrimitivesExImpl`,
`DrawInstancedPrimitivesEx`) independently wrap their own single command-list submission in its own
`BeginQuery`/`EndQuery` pair** (confirmed via grep, all 4 call sites) whenever `activeOcclusionQueryHeap_` is set
— since a D3D12 query-heap slot holds only one result, a second draw between one `OcclusionQuery.Begin()`/`.End()`
pair overwrites the first draw's captured samples instead of the combined total XNA's real semantics require. See
`AUDIT_CROSS_CUTTING_FINDINGS.md` and the `D3D12OcclusionQueryBackend` reports for the full write-up.

### Systematic FNA parity gaps — corroborates already-recorded D3DCommon finding
The `Skinned3d` draw-param-fill block correctly sets `perDraw.AmbientColor` from `params.ambientColor` (lines
1908-1910/1988-1990) but never sets any `EmissiveColor` field for the skinned path (matching
`D3DSkinnedExtraConstants`'s lack of such a field, already confirmed in `backend-d3dcommon`); the unskinned lit
path (lines 2014-2016) correctly forwards `EmissiveColor` — identical scoping to D3D11's own confirmed gap.

### Architecture — positive findings
`GetBoundColorWidthEXT()`/`GetBoundColorHeightEXT()` correctly track the currently-bound render target's real
dimensions (confirmed via `D3D12SpriteBatch.cpp`'s usage), so this backend's `SpriteBatch` does not share the
WebGPU-specific always-backbuffer-relative bug. `D3D12SpriteBatchBackend::SetTransformMatrix()` is a real,
working override (confirmed via grep) — this backend does not share the Vulkan-specific no-op bug either.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
No issues found in the areas read.

## Detailed Findings

**F1 (HIGH):** Stencil/Scissor testing completely non-functional — confirmed at the concrete implementation
level (see above and `AUDIT_CROSS_CUTTING_FINDINGS.md`).
**F2 (MEDIUM-HIGH):** multi-draw occlusion queries only capture the last draw's samples, not the combined total
(see above and `AUDIT_CROSS_CUTTING_FINDINGS.md`).

## Cross-File Observations

F1 is coherently, consistently disclosed across three files (`D3D12GraphicsBackend.cpp`'s two `Apply*State`
methods, `D3D12PipelineStateCache.hpp`/`.cpp`'s own documented scope cut) — a deliberate first-implementation
limitation, not an inconsistency between layers. F2 is confirmed via the 4 draw-method call sites here plus
`D3D12OcclusionQueryBackend.cpp`'s own `Begin()`/`End()` design.

## Missing or Weak Tests

No dedicated test found in this audit so far for: device-lost/removed recovery, resize handling, the full
non-skinned `DrawPrimitivesExImpl` variant dispatch, `StencilState`/`ScissorRectangle` behavior, or a multi-draw
`OcclusionQuery` sequence — all consistent with this backend having no Windows-native CI per D-P4.

## Positive Findings

Confirmed absence of the EasyGL-class window-registry bug; correctly render-target-relative `SpriteBatch`
sizing (unlike WebGPU's confirmed bug); correctly implements `SetTransformMatrix` (unlike Vulkan's confirmed
bug); coherent, consistently-applied (if significant) Stencil/Scissor scope cut across every relevant file.

## Final Assessment

Two significant, confirmed-at-the-implementation-level findings (F1 HIGH: Stencil/Scissor non-functional; F2
MEDIUM-HIGH: multi-draw occlusion query overwrite), both honestly disclosed in the codebase's own comments as
deliberate first-implementation scope cuts rather than hidden defects. The areas read otherwise corroborate
(not introduce) already-recorded `backend-d3dcommon` findings and confirm two genuine positive cross-backend
results (SpriteBatch transform/render-target-relative correctness). The untraced ~1800 lines remain a gap for a
future, more exhaustive pass.
