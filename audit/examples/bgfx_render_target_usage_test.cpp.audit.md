# Audit: examples/bgfx_render_target_usage_test.cpp

## Metadata

- Source file: `examples/bgfx_render_target_usage_test.cpp` (109 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RenderTargetUsage` (`DiscardContents`/`PreserveContents`)
  smoke test
- File type: standalone `Game`-subclass executable (class `BgfxRTUsageTest`), CTest-registered
  (`cna_test_bgfx_render_target_usage` / `Bgfx_RenderTargetUsage`, `cmake/Tests/BgfxTests.cmake:112-114`)
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.RenderTargetUsage`,
  `GraphicsDevice.SetRenderTarget`'s auto-clear-on-DiscardContents behavior.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:1821-1858`
  (`SetRenderTarget(RenderTarget2D*)`), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`BgfxRenderTargetBackend::BindAsRenderTarget()`, lines 715-723).
- Authoring commit: `a99fc8a9` ("feat(Task 179): Bgfx RenderTargetUsage smoke test —
  DiscardContents/PreserveContents", 2026-06-23).

## Purpose

Task 179: exercises both `RenderTargetUsage` values on Bgfx across two simulated frames — create a
`DiscardContents` RT and a `PreserveContents` RT, bind/clear/unbind each, then (frame 2) re-bind and
immediately unbind both without drawing anything, to exercise the "does re-binding still work" path. The
file's own header comment states its scope limitation up front: *"Pixel-level verification is not
available in the Bgfx backend (SpriteBatch casts textures to BgfxTextureBackend, not
BgfxRenderTargetBackend, and the 3D state API throws for depth/blend changes). This is a smoke test:
create both RT types, bind/unbind them across two frames, and verify no crash."* Exit code 0 = no crash;
no assertion about `DiscardContents` vs. `PreserveContents` actually producing different pixel content is
made or attempted.

## Executive Verdict

**Needs attention** — both of this file's stated reasons for being merely a smoke test are demonstrably
false today, given other files in this exact shard, leaving a real, currently-avoidable pixel-coverage
gap for a genuine XNA-facing behavior (`RenderTargetUsage`). The *mechanism* this file documents (how
`DiscardContents`/`PreserveContents` map to bgfx's per-view clear flags) was independently verified
correct — the issue is entirely about what the test itself can and does prove.

## Checklist Results

### API / XNA / FNA parity
The file's own comment on the DiscardContents mechanism is accurate and was independently confirmed:
`GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (`GraphicsDevice.cpp:1843-1858`) auto-clears
(`Clear(Target|DepthBuffer or just Target, Color(0,0,0,255), ...)`) whenever
`renderTarget->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents` — this audit
cross-checked FNA's `GraphicsDevice.cs` (`clearTarget == RenderTargetUsage.DiscardContents` gating an
equivalent auto-clear) and confirms this matches real XNA/FNA semantics, not a CNA invention.

### Behavioral correctness
Traced `BgfxRenderTargetBackend::BindAsRenderTarget()`
(`BgfxGraphicsBackend.cpp:715-723`):
```cpp
void BgfxRenderTargetBackend::BindAsRenderTarget()
{
    bgfx::setViewFrameBuffer(viewId_, fbo);
    bgfx::setViewRect(viewId_, 0, 0, width, height);
    if (preserveContents)
        bgfx::setViewClear(viewId_, BGFX_CLEAR_NONE, 0, 1.0f, 0);
}
```
This correctly matches the file's own mechanism description: for `PreserveContents`, the view's clear
flags are explicitly suppressed so no carry-over clear from a prior `DiscardContents` frame leaks in; for
`DiscardContents`, the XNA-layer auto-`Clear()` (verified above) sets the view's clear flags itself. This
part of the file's reasoning is accurate and this audit found no defect in the actual
`DiscardContents`/`PreserveContents` implementation.

### Robustness — the file's own justification for being smoke-test-only is stale
**Claim 1, "SpriteBatch casts textures to BgfxTextureBackend, not BgfxRenderTargetBackend"**: this is the
same Task 873 bug documented (and shown fixed) in `bgfx_render_target_sample_test.cpp`'s own report in
this batch — fixed by commit `bda07bac` (Task 878/879), which post-dates this file's own authoring
commit `a99fc8a9` (2026-06-23, i.e. Task 878/879 landed on 2026-07-07, two weeks later). Confirmed via
`git log` this file is chronologically the *oldest* of the three render-target files in this batch.

**Claim 2, "the 3D state API throws for depth/blend changes"**: traced this to
`BgfxGraphicsBackend::SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`
(`BgfxGraphicsBackend.cpp:2002-2004`), which do call a shared `ThrowNo3DState()` helper — but these are a
distinct, legacy `NOXNA` convenience API (`GraphicsDevice::SetDepthTestEnabled(bool)` etc.,
`GraphicsDevice.hpp:674-678`), **not** the `BlendState`/`DepthStencilState`/`RasterizerState`
property-object setters (`setBlendStateProperty`/`setDepthStencilStateProperty`/`setRasterizerStateProperty`)
that every other Bgfx test in this batch uses successfully for real 3D draws. This audit confirmed via
`git log -S"ApplyBlendState"`/`-S"ApplyDepthStencilState"`/`-S"ApplyRasterizerState"` that the *real*
XNA-facing state-application path (`ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`,
commit `e4a80dda`, "Tasks 69-71, 73") was implemented **before** this file was even written
(2026-06-13 vs. this file's 2026-06-23) — meaning the claim that "the 3D state API throws" was already
questionable, or at best referred only to the unrelated legacy convenience methods, even at authoring
time. `bgfx_pbreffect_test.cpp` (`dev.setBlendStateProperty(BlendState::Opaque)`,
`dev.setRasterizerStateProperty(RasterizerState::CullNone)`) and
`bgfx_rasterizerstate_depthbias_test.cpp` (`dev.setDepthStencilStateProperty(dss)`) both call these real
setters successfully and go on to do full 3D draws with `GetBackBufferData` pixel readback — directly
falsifying "pixel-level verification is not available in the Bgfx backend" as a *general* Bgfx-backend
limitation.

### Testing
Given both justifications for smoke-test-only scope are stale/misapplied, this file leaves
`RenderTargetUsage::DiscardContents` vs. `PreserveContents` with **zero pixel-level regression coverage**
on the Bgfx backend, despite that coverage being demonstrably achievable with the same
render→unbind→SpriteBatch-sample→`GetBackBufferData` pattern `bgfx_rendertarget2d_mip_test.cpp` (this
same batch) already uses successfully.

## Detailed Findings

### F1 — Both stated reasons for smoke-test-only scope are stale or based on an unrelated API; RenderTargetUsage has no real pixel coverage on Bgfx despite it being achievable

- Severity: MEDIUM
- Confidence: HIGH (confirmed via direct source reading of the current `BgfxSpriteBatchBackend::Draw`,
  `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`, and `git log` commit-date ordering
  for all three referenced fixes)
- Category: test-coverage / stale-comment
- Location/symbol: file header comment (lines 10-16)
- Evidence: see Behavioral correctness / Robustness sections above — the SpriteBatch cast bug (Task 873)
  was fixed by commit `bda07bac` two weeks after this file was authored; the "3D state API throws"
  claim conflates the unused legacy `SetDepthTestEnabled`/`SetBlendEnabled` convenience methods (which do
  still throw, `BgfxGraphicsBackend.cpp:2002-2004`) with the real `BlendState`/`DepthStencilState`
  property-setter path (which was already implemented and demonstrably works, confirmed by 4 other files
  in this exact shard).
- Why it matters: a concrete, currently-uncaught regression this test's own methodology would miss —
  if `BgfxRenderTargetBackend::BindAsRenderTarget()`'s `if (preserveContents)` condition were ever
  accidentally inverted or dropped (a one-line regression in code this audit otherwise found correct
  today), this test would still print `[PASS]` and exit 0, because it never reads back a single pixel —
  it only checks that bind/unbind calls complete without crashing. The two justifications that were used
  to scope this down to a crash-only smoke test no longer hold, and the gap is avoidable using patterns
  already proven in this exact shard (`bgfx_rendertarget2d_mip_test.cpp`'s
  `RenderIntoRTAndReadColumn`-style fresh-RT-per-checkpoint pattern would adapt directly).
- FNA/XNA comparison: N/A (test-authoring/coverage issue — the underlying `DiscardContents`/
  `PreserveContents` mechanism itself was independently verified correct against FNA's own semantics,
  see Behavioral correctness above).
- Related files: `bgfx_render_target_sample_test.cpp` and `bgfx_render_target_cube_sample_test.cpp` (same
  batch) have the identical "Task 873 not fixed" component of this stale justification; this file adds
  the second, distinct "3D state API throws" misattribution on top.
- Suggested future action (not implemented by this audit): rewrite this test using the
  render→unbind→SpriteBatch-draw→`GetBackBufferData` pattern already proven in
  `bgfx_rendertarget2d_mip_test.cpp`, asserting the `DiscardContents` RT reads back as its last `Clear()`
  color and the `PreserveContents` RT genuinely retains content across a re-bind that would otherwise be
  cleared under `DiscardContents` semantics (the actual FNA-documented behavioral difference between the
  two enum values, which this file currently never distinguishes at the pixel level at all).

## Cross-File Observations

- This is the oldest (by authoring date) of the three render-target-related files in this batch with a
  stale-justification pattern, and its own comment's two-part justification chain compounds both an
  outdated bug reference and what looks like a pre-existing conflation between two different backend
  APIs (the legacy `SetDepthTestEnabled`-family convenience methods vs. the real
  `BlendState`/`DepthStencilState`/`RasterizerState` property setters) — worth flagging to the project
  owner as a documentation-hygiene pattern across this shard, not just three isolated incidents.
- `bgfx_rendertarget2d_mip_test.cpp`'s own header comment independently corroborates part of this
  finding: it explicitly documents that reusing a single already-rendered RT across more than one
  `Clear+Draw+GetBackBufferData` cycle is unreliable on Bgfx (unrelated to any particular feature), and
  works around it with a fresh-RT-per-checkpoint pattern — the same pattern a rewritten version of this
  file would need to adopt.

## Missing or Weak Tests

See F1 in full — this is the central gap in this file.

## Positive Findings

- The `DiscardContents`/`PreserveContents` mechanism itself, at both the XNA layer
  (`GraphicsDevice::SetRenderTarget`'s auto-clear) and the Bgfx-backend layer
  (`BindAsRenderTarget`'s conditional `setViewClear`), is correctly implemented and matches FNA's
  documented semantics — this audit found no defect in the feature itself, only in this file's ability
  to detect a regression in it.
- The two-frame bind/re-bind structure is a reasonable skeleton to build a real assertion on top of, once
  pixel readback is added — the mechanical scaffolding (RT construction with each `RenderTargetUsage`
  value, correct constructor argument order matching `RenderTarget2D`'s 8-arg ctor) is already correct.

## Final Assessment

The feature works; the test doesn't prove it. Both reasons given for the test's narrow scope were
independently checked against current production code and git history and found stale or misapplied,
and this shard's own sibling files (particularly `bgfx_rendertarget2d_mip_test.cpp`) demonstrate the
exact capability this file claims doesn't exist.
