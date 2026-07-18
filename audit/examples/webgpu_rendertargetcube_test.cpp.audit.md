# Audit: examples/webgpu_rendertargetcube_test.cpp

## Metadata

- Source file: `examples/webgpu_rendertargetcube_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `RenderTargetCube` support test (WEBGPU-114)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_webgpu_test(cna_test_webgpu_rendertargetcube examples/webgpu_rendertargetcube_test.cpp)` /
  `cna_register_backend_test(NAME WebGPU_RenderTargetCube …)`, `cmake/Tests/WebGpuTests.cmake:176-177`).
- XNA/FNA relevance: `GraphicsDevice.SetRenderTarget(RenderTargetCube, CubeMapFace)`, `RenderTargetCube`
  constructor (`mipMap`, `DepthFormat`), `RenderTargetCube.GetData(face, ...)`,
  `EnvironmentMapEffect.EnvironmentMap` sampling a `RenderTargetCube`.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`WebGPURenderTargetCubeBackend` constructor lines 982-1099, `BindAsRenderTargetFace()` lines 1119-1134,
  `FlushCurrentRenderTarget()` lines 5448-5459, `RenderPendingDrawsToRenderTargetCubeFace()` lines 5339-5438,
  `ResolveCubeSamplable()`/`IWebGPUCubeSamplable` lines 175-181, `ComputeLogicalViewport()`/`QueueSprite()`
  lines 4734-4788+).

## Purpose

Six-check test proving the WebGPU backend's first real render-into-a-cube-face support (before WEBGPU-114,
`CreateRenderTargetCube` was `IGraphicsBackend`'s own nullptr-returning default): (A) all 6 faces bound
*directly* face-to-face (no intervening backbuffer switch) each `Clear()`ed to a distinct colour, read back
per-face — proves per-face addressing and that direct face-to-face switching genuinely flushes the outgoing
face rather than collapsing all 6 clears; (B) a real `BasicEffect` quad drawn into one face; (C) an
`EnvironmentMapEffect` sampling round trip, specifically exercising `IWebGPUCubeSamplable`/
`ResolveCubeSamplable()` (a `WebGPURenderTargetCubeBackend`, not just a `WebGPUTextureCubeBackend`, must
resolve correctly); (D) the architecture-critical check — an intervening cube-face-targeted `Clear()` must
not leak into the backbuffer's own render pass; (E) `mipMap=true` must throw; (F) `MultiSampleCount` must
honestly report 0 (MSAA is not implemented for `RenderTargetCube` at all on this backend, unlike
`RenderTarget2D`'s real MSAA support).

## Executive Verdict

**Needs attention** — every check's own assertion is correct and was independently confirmed against the
backend source. However, this file's own Check-C comment **self-discloses a real, confirmed, backend-wide
production defect** (SpriteBatch's clip-space mapping is always derived from the backbuffer's own logical
size, never the currently-bound render target's) and deliberately routes around it rather than fixing it —
this audit independently traced the claim to `ComputeLogicalViewport()`/`QueueSprite()` and confirms it is
real, not merely an unverified assertion in a comment (see F1).

## Checklist Results

### API / XNA / FNA parity

`RenderTargetCube`'s 5-argument constructor (`device, size, mipMap, format, depthFormat`), `GetData(face,
data, count)`, and `SetRenderTarget(RenderTargetCube*, CubeMapFace)` match FNA's own signatures. `CubeMapFace`
enumerators (`PositiveX`…`NegativeZ`) used correctly and exhaustively (all 6 faces exercised in Check A).

### Behavioral correctness

- Check A's "direct face-to-face switch flushes the outgoing face" claim was traced end-to-end:
  `WebGPURenderTargetCubeBackend::BindAsRenderTargetFace()` (lines 1119-1134) calls
  `owner_->FlushCurrentRenderTarget()` unconditionally whenever the target/face actually changes (guarded
  only by an early-return when already bound to the exact same face), and `FlushCurrentRenderTarget()`
  (lines 5448-5459) dispatches to `RenderPendingDrawsToRenderTargetCubeFace()` for the previously-bound
  face — confirming each face's own `Clear()` is genuinely committed into its own render pass before the
  next face takes over, not collapsed.
- Check F's "MSAA is not implemented for `RenderTargetCube` at all" claim was independently confirmed:
  `WebGPURenderTargetCubeBackend`'s constructor hardcodes `colorDescriptor.sampleCount = 1` and
  `depthDescriptor.sampleCount = 1` unconditionally (lines 1020, 1076) with no reference to
  `owner_->sampleCount_` anywhere in the class — unlike `WebGPURenderTargetBackend` (the `RenderTarget2D`
  counterpart), which does mirror the backend's global MSAA state. `getMultiSampleCountProperty()` reporting
  0 regardless of the requested value is therefore architecturally guaranteed, not merely likely.
- Check E's `mipMap=true` throw matches `WebGPURenderTargetCubeBackend`'s constructor lines 993-996 exactly.
- **F1** — Check C's own in-body comment (lines 220-236) claims `WebGPUGraphicsBackend::QueueSprite()`
  computes sprite clip-space geometry from `ComputeLogicalViewport()`, which is always derived from the
  *backbuffer's* `virtualWidth_`/`virtualHeight_`/`physicalWidth_`/`physicalHeight_`, never from whatever
  render target (2D or cube face) happens to be currently bound — independently confirmed by reading
  `ComputeLogicalViewport()` (lines 4734-4769, entirely `physicalWidth_`/`physicalHeight_`/`virtualWidth_`/
  `virtualHeight_`-derived, no render-target-size parameter anywhere) and `QueueSprite()` (line 4787: `const
  LogicalViewport viewport = ComputeLogicalViewport();`, called unconditionally regardless of
  `currentRenderTarget_`/`currentRenderTargetCubeFace_`). This is a real, currently-live defect, not a
  hypothetical — see Detailed Findings.

### Logic

Check C deliberately avoids `SpriteBatch` for painting the cube's 6 faces (`DrawFullScreenQuad()`, a real
`BasicEffect`/3D draw, instead) specifically *because of* F1 — a reasonable, honestly-disclosed workaround
for this specific test, but the underlying gap remains live for any other caller.

### C++ correctness

No issues in the test file itself.

### Memory/resource lifetime

Same `ApplyBasicEffect()` `static BasicEffect* fx = nullptr; new BasicEffect(dev)` leak pattern as
`webgpu_rendertarget2d_test.cpp` (this same batch) — see that file's F1; not re-scored here to avoid
double-counting the identical LOW finding, but noted under Cross-File Observations.

### Performance

N/A — one-shot test.

### Architecture

`IWebGPUCubeSamplable`'s diamond-inheritance-avoidance design (already positively noted in
`WebGPUGraphicsBackend.cpp.audit.md`) is exercised meaningfully here: Check C specifically targets the case
that would have silently regressed before this pattern existed (a `dynamic_cast` to the concrete
`WebGPUTextureCubeBackend` type only, missing a `WebGPURenderTargetCubeBackend` and falling back to a 1x1
white cube) — a well-chosen regression check, not a vacuous one.

### Maintainability

Uses a self-tallying `passCount`/`totalCount` pair (incremented once per `check()` call, lines 85-93) rather
than a hardcoded literal — the more drift-resistant pattern noted as a stylistic gap in
`webgpu_msaa_test.cpp`/`webgpu_pbr3d_test.cpp`/`webgpu_rendertarget2d_test.cpp` in this same batch. No
maintainability concern here.

### Portability

N/A.

### Robustness

N/A — test file.

### Testing

Comprehensive for `RenderTargetCube`'s own feature surface (per-face addressing, direct face switching, real
3D draw, sampling round trip via `EnvironmentMapEffect`, target isolation, mip/MSAA scope cuts). The one gap
is not a missing check in *this* file but the pre-existing backend-wide SpriteBatch-into-render-target
sizing defect it correctly works around rather than exercises (see F1) — meaning no WebGPU test in this
batch currently *proves* `SpriteBatch.Draw()` into an off-screen target at a non-backbuffer-matching size
behaves correctly, because it doesn't.

## Detailed Findings

### F1 — `WebGPUGraphicsBackend::QueueSprite()`'s clip-space mapping is always derived from the backbuffer's logical size, never the currently-bound render target's, so `SpriteBatch.Draw()` into an off-screen `RenderTarget2D`/`RenderTargetCube` face of a different size than the backbuffer maps its destination rectangle incorrectly

- Severity: HIGH
- Confidence: HIGH (independently confirmed by direct source reading of `ComputeLogicalViewport()`/
  `QueueSprite()`, not merely accepted from the test file's own comment)
- Category: correctness / architecture (backend-wide, pre-existing — not introduced by `RenderTargetCube`/
  WEBGPU-114, and not specific to this file)
- Location/symbol: `WebGPUGraphicsBackend::ComputeLogicalViewport()` (lines 4734-4769) and
  `WebGPUGraphicsBackend::QueueSprite()` (lines 4771-4788+, specifically line 4787:
  `const LogicalViewport viewport = ComputeLogicalViewport();`)
- Evidence: `ComputeLogicalViewport()` computes its result exclusively from `physicalWidth_`/
  `physicalHeight_`/`virtualWidth_`/`virtualHeight_`/`presentationMode_` — all backbuffer-scoped state with
  no parameter or member representing "the currently bound render target's own size." `QueueSprite()` calls
  it unconditionally on every sprite, with no branch on `currentRenderTarget_`/`currentRenderTargetCubeFace_`
  at all. The test file's own comment states this was "confirmed empirically: with this test's 64x64
  backbuffer and 32x32 cube faces, a SpriteBatch `Draw(dest=(0,0,32,32))` only covered one quadrant of each
  face, leaving the centre pixel un-painted" — i.e. this was actually run and observed, not merely
  theorized.
- Why it matters: any game rendering UI, a minimap, a post-process pass, or any other content via
  `SpriteBatch` *into* an off-screen `RenderTarget2D` or `RenderTargetCube` face whose size differs from the
  backbuffer's current logical size will have every sprite's destination rectangle mis-mapped — this is a
  common technique (render-to-texture UI/effects), not an exotic edge case, making this a "wrong behavior on
  a common path" per this audit's severity rubric. `SpriteBatch.Draw()` *sampling from* a render target back
  onto the backbuffer (as `webgpu_rendertarget2d_test.cpp`'s own Check D and this file's Check C do) is
  unaffected, since that direction never has render-target-relative geometry to get wrong — the backbuffer
  really is the target being mapped against in that case.
- FNA/XNA comparison: FNA's `SpriteBatch` maps destination rectangles in the target surface's own pixel
  space regardless of what that surface is (backbuffer or render target) — there is no equivalent "logical
  viewport" indirection tied specifically to the backbuffer when a `RenderTarget2D`/`RenderTargetCube` is
  bound.
- Related files: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` (the fix would need
  `QueueSprite()`/`ComputeLogicalViewport()` to become target-aware, likely taking the currently-bound
  target's own physical size when one is bound, mirroring how 3D draws' NDC math is already
  target-agnostic); `webgpu_rendertarget2d_test.cpp` (this same batch) shares the identical underlying gap,
  also correctly never exercising SpriteBatch-into-a-render-target for this reason.
- Suggested future action (not implemented by this audit): make `QueueSprite()`'s viewport computation
  target-aware (use the bound `RenderTarget2D`/cube face's own width/height verbatim, bypassing
  `ComputeLogicalViewport()`'s backbuffer-only presentation-mode scaling logic entirely when a render target
  is bound, since presentation-mode letterboxing is a backbuffer-only concept), then add a dedicated
  SpriteBatch-draws-into-a-differently-sized-render-target regression test (currently absent across the
  entire `examples-tests-webgpu` shard, per this file's own honest disclosure).

## Cross-File Observations

- Shares the `ApplyBasicEffect()` leak pattern with `webgpu_rendertarget2d_test.cpp` (see that file's F1) —
  same LOW-severity, copy-paste-origin issue, not re-scored here.
- F1 is a new, independently-verified addition to this audit's known-issue set — not previously catalogued
  in `AUDIT_CROSS_CUTTING_FINDINGS.md` or the `WebGPUGraphicsBackend.cpp.audit.md` report (which was a
  scoped-depth review that did not reach `QueueSprite()`/`ComputeLogicalViewport()`). Worth adding to the
  cross-cutting findings during synthesis, since it plausibly affects every backend with an analogous
  "logical viewport" sprite-batching indirection tied to the backbuffer, not just WebGPU — a priority
  cross-backend check for later passes.

## Missing or Weak Tests

F1 itself is the gap: no test in this shard exercises `SpriteBatch.Draw()` *into* an off-screen render
target of a different size than the backbuffer, so the defect it represents has zero regression coverage
anywhere in this project (this file's own comment says as much).

## Positive Findings

- Check A is a genuinely strong test of direct face-to-face switching (not merely face↔backbuffer), the
  single riskiest case for this backend's "flush on target switch" design, and it passes for the right
  reason (traced to `BindAsRenderTargetFace()`'s own unconditional flush).
- Check C is a well-targeted regression check for a specific, previously-real risk
  (`IWebGPUCubeSamplable`/`ResolveCubeSamplable()` correctly resolving a render-target-backed cube, not just
  an upload-only one) with an honest, load-bearing comment explaining exactly why `SpriteBatch` couldn't be
  used to set up the scene instead.
- The self-tallying `passCount`/`totalCount` pattern is a genuinely more maintainable choice than this
  batch's more common hardcoded literal.
- Rare and valuable: a test file that surfaces a real defect in its own commentary, with enough concrete
  detail (dimensions, observed vs. expected coverage) that this audit could verify it against production
  source without re-running anything — exactly the kind of transparency this audit's template report
  (`easygl_basiceffect_specular_test.cpp.audit.md`) praised in a different file.

## Final Assessment

A well-designed `RenderTargetCube` test suite whose own honest self-disclosure led this audit to confirm a
real, HIGH-severity, backend-wide SpriteBatch-into-render-target sizing defect (F1) — pre-existing, not
introduced by this file's own feature (WEBGPU-114), but newly elevated here from an inline comment to a
tracked finding with independent source-level confirmation.
