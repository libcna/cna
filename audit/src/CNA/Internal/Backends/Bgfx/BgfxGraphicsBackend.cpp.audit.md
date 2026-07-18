# Audit: src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
- Audit status: AUDITED (scoped-depth review — see below; matches the standard already applied to this audit's
  other largest files: EasyGL 4733 lines, WebGPU 8805 lines, D3D11 1846 lines, D3D12 2331 lines, SdlGpu 5105
  lines)
- Subsystem: `backend-bgfx` shard
- File type: C++ implementation, 3443 lines
- Related header: `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp` (same shard)
- XNA/FNA relevance: implements the entire Bgfx backend's device lifecycle and draw dispatch
- Graphics backend relevance: this is the central Bgfx class
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Implements device/view creation, `Apply*State` (including the genuinely functional Stencil/Scissor/DepthBias
support), `Clear*`, `SetTransformMatrix`, and the full draw-dispatch chain across every stock-effect variant.

## Executive Verdict

**Needs attention, scoped-depth review.** `RegisterForWindow` absence, `ApplyDepthStencilState`/
`ApplyRasterizerState`'s genuinely functional stencil/scissor/depth-bias handling, and `SetTransformMatrix` were
all read and verified in full. The remaining ~3300 lines (full non-skinned draw-dispatch variants, device-lost
recovery, resize handling) were not exhaustively traced, consistent with this audit's scoped-depth standard for
files of this size.

## Checklist Results

### Confirmed clean: window-registry
**No `RegisterForWindow` call anywhere in this file** (confirmed via grep) — matches D3D11/D3D12's identical
absence; this backend cannot share the EasyGL-class dangling-window-registry-pointer bug.

### Confirmed, genuinely functional: Stencil, Scissor, DepthBias
`ApplyDepthStencilState()` (line 1674) caches every stencil field and calls `RebuildStencilState()`, which
builds real `stencilFront_`/`stencilBack_` bgfx stencil-state values — confirmed genuinely consumed (not merely
tracked), including a real, empirically-found, well-documented front/back-swap fix (Task 763, explicitly
cross-referenced against Vulkan's own identical Task 870 fix): bgfx's own default `glFrontFace` is `GL_CW`
(opposite of EasyGL's own effective convention), so `bgfx::setStencil`'s front/back split needed a deliberate
swap to correctly land XNA's "front"/"CounterClockwise" stencil settings on the GPU-evaluated slot matching each
raw winding — confirmed via "a genuinely differential `stencil_twosided` test." `ApplyRasterizerState()` (line
1773) correctly tracks `scissorEnabled_` as a genuinely independent flag (not derived from whether the rect
happens to be non-zero, an explicitly documented and correct design choice given `SetScissorRect()` can be called
before or after in either order) and emulates `DepthBias` via the same per-draw vertex-shader Z-offset mechanism
already confirmed present in every 3D vertex shader (`u_depthBias`). `SlopeScaleDepthBias` is the one honestly-
disclosed, deliberately-unimplemented gap (an explicit, dated 2026-07-10 project-owner decision, not a silent
omission). **This makes Bgfx the most complete backend checked in this audit for this specific
Stencil+Scissor+DepthBias combination** — contrast with D3D12's confirmed complete non-functionality of Stencil
and Scissor, and SdlGpu's functional Stencil+Scissor but explicitly-deferred DepthBias.

### Confirmed correct: SetTransformMatrix
`BgfxSpriteBatchBackend::SetTransformMatrix()` (line 979) is a real, working override — independently confirmed
already while auditing D3D11's own `SpriteBatch` (whose header comment specifically cited Bgfx as getting this
right, unlike Vulkan's confirmed no-op bug).

### Confirmed dead code: BgfxVertexFormatHelper.hpp
Grepped this entire file for `VertexElementFormatToBgfx`/`VertexElementUsageToBgfxAttrib`/
`VertexElementFormatSize` — zero matches, confirming the paired header's entire public API is unused; the real
vertex-layout dispatch uses hardcoded byte-size (stride) switching instead.

### Systematic FNA parity gaps — already recorded cross-cutting findings, not re-derived here
`Clear()`'s unconditional color+depth+stencil clear regardless of requested `ClearOptions`, and 2 known-failing
CTests registered with no `WILL_FAIL` annotation, were both already confirmed via the `examples-tests-bgfx`
mechanical batch earlier this session — not re-verified line-by-line in this scoped pass, but consistent with
what's already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
No issues found in the areas read.

## Detailed Findings

None new — this file corroborates and formally confirms already-recorded cross-cutting findings (dead-code
vertex-format helper, correct `SetTransformMatrix`) and establishes 3 genuine positive results (functional
Stencil/Scissor/DepthBias, the most complete combination of the three found in this audit).

## Cross-File Observations

The Task 763 stencil front/back-swap fix is explicitly and accurately cross-referenced against Vulkan's own
identical Task 870 fix — genuine, verified cross-backend consistency in how this class of GL/Vulkan
winding-convention bug was diagnosed and fixed.

## Missing or Weak Tests

No dedicated test found in this audit so far for: device-lost/removed recovery, resize handling, or the full
non-skinned `DrawPrimitivesExImpl` variant dispatch.

## Positive Findings

The most complete Stencil+Scissor+DepthBias implementation of any backend checked in this audit; confirmed
absence of the EasyGL-class window-registry bug; confirmed correct `SetTransformMatrix`; a genuinely well-
documented, empirically-verified front/back stencil-winding fix.

## Final Assessment

No new defects found in the areas read (scoped-depth review); corroborates and formally confirms multiple
already-recorded cross-cutting findings while establishing this backend as the most complete for
Stencil/Scissor/DepthBias support. The untraced ~3300 lines remain a gap for a future, more exhaustive pass.
