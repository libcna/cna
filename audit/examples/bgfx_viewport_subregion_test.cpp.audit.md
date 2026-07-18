# Audit: examples/bgfx_viewport_subregion_test.cpp

## Metadata

- Source file: `examples/bgfx_viewport_subregion_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `GraphicsDevice.Viewport` GPU-wiring pixel test (Task 880)
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_viewport_subregion …)`
  / `cna_register_backend_test(NAME Bgfx_Viewport_Subregion …)`, `cmake/Tests/BgfxTests.cmake:425-429`); confirmed
  via `git log` (`98e1c32d`/`86226bc3 fix(Task 880): wire GraphicsDevice.Viewport to a real GPU viewport on all 3
  backends`).
- XNA/FNA relevance: direct — `GraphicsDevice.Viewport` (`Microsoft::Xna::Framework::Graphics::Viewport`) is a core
  XNA 4.0 render-state property; a sub-region viewport (as used for split-screen rendering) is standard XNA usage.
- FNA reference: FNA's `GraphicsDevice.Viewport` setter applies the viewport directly to the active render target
  via the platform backend (`FNA3D_SetViewport`); the *rectangle-clips-rendering-to-region* semantic this test
  proves is the same behavior FNA guarantees.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`SetViewport()` lines
  1855-1867, `ApplyViewportOverride()` lines 1869-1878, `EnsureViewState()` lines 1325-1382, `ReadBackbuffer()`
  lines 303-352, `DrawPrimitivesEx()` lines 2365-2374), `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`setViewportProperty()`/`getViewportProperty()` lines 235-247, `GetBackBufferData()` lines 1768-1813,
  `DrawUserPrimitives(VertexPositionColor*)` lines 868-892).

## Purpose

Proves that `GraphicsDevice::setViewportProperty()` on the Bgfx backend has a real GPU effect (Task 880 fixed a
prior state where `EnsureViewState()` unconditionally reset the view rect to the full window size on every
`Clear()`/`Present()`/`SubmitSprite()` call, silently discarding any custom `Viewport`). Three checks: (1) a
left-half sub-region `Viewport(0,0,leftHalfW,H)` — a full-NDC quad drawn under it must be visible at a sample
point inside the left half and absent (black) at a sample point in the right half; (2) restoring the full
`Viewport` — the same quad must now reach both halves; (3) a plain CPU-side get/set round-trip on an arbitrary
`Viewport(10,20,300,200)`, checking `X`/`Y`/`Width`/`Height` are preserved.

## Executive Verdict

**Healthy** — this audit independently traced the entire call chain from `GraphicsDevice::setViewportProperty()`
through to the actual `bgfx::setViewRect()` call and confirmed the test's premises are correct: `SetViewport()`
stores the rect, `ApplyViewportOverride()` re-applies it immediately before every 3D `submit()` (including the
`DrawUserPrimitives(VertexPositionColor*)` → `DrawPrimitivesEx()` path this test actually uses), overriding the
full-size rect that the preceding `Clear()`→`EnsureViewState()` call had set. The header comment's claim about a
"Bgfx GetBackBufferData single-reliable-read-per-frame" quirk was independently verified against
`ReadBackbuffer()`'s actual implementation (it internally advances up to 3 `bgfx::frame()` calls waiting for a
screenshot callback, which is a plausible, concrete mechanism for exactly the kind of frame-boundary staleness the
comment describes) rather than taken on faith. No HIGH/CRITICAL findings.

## Checklist Results

### Purpose
Correctly placed and scoped; a direct, explicitly-acknowledged structural port of
`easygl_viewport_subregion_test.cpp` adapted to this family's single-Draw()-call/retry-loop pattern. PASS.

### API / XNA / FNA parity
`Viewport(int,int,int,int)`, `getWidthProperty()`/`getHeightProperty()`/`getXProperty()`/`getYProperty()`,
`GraphicsDevice::setViewportProperty()`/`getViewportProperty()` all map correctly to FNA's `Viewport` struct and
`GraphicsDevice.Viewport` property. Checked `Viewport.hpp`: the 4-arg constructor used by the test
(`Viewport(0, 0, leftHalfW, H)`, `Viewport(10, 20, 300, 200)`) documents "MinDepth=0, MaxDepth=1" defaults,
matching FNA's `Viewport(int,int,int,int)` constructor semantics. PASS.

### Behavioral correctness
Traced the full sub-region wiring end to end:
- `dev.setViewportProperty(Viewport(0,0,leftHalfW,H))` → `GraphicsDevice::setViewportProperty()`
  (`GraphicsDevice.cpp:240-247`) stores `viewport_` and calls `backend_->SetViewport(x,y,w,h,minDepth,maxDepth)`.
- `BgfxGraphicsBackend::SetViewport()` (`BgfxGraphicsBackend.cpp:1855-1867`) is storage-only:
  `viewportX_/Y_/W_/H_` + `viewportSet_ = true` (confirmed by its own comment, "Storage-only (Task 880); applied
  via ApplyViewportOverride() right before each 3D submit").
- `drawQuad()` → `dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2)` (verts typed
  `VertexPositionColor`) resolves to `GraphicsDevice::DrawUserPrimitives(PrimitiveType, const VertexPositionColor*,
  int, int)` (`GraphicsDevice.cpp:869-892`), which calls `backend_->DrawPrimitivesEx(*vb, world, view, proj, type,
  count, p)`.
- `BgfxGraphicsBackend::DrawPrimitivesEx()` (`BgfxGraphicsBackend.cpp:2365-2374`) calls `ApplyViewportOverride()`
  as its very first statement, *before* setting up the WVP uniform and submitting the draw.
- `ApplyViewportOverride()` (lines 1869-1878): `if (viewportSet_ && currentViewId_ == 0 && viewportW_ > 0 &&
  viewportH_ > 0) bgfx::setViewRect(currentViewId_, viewportX_, viewportY_, viewportW_, viewportH_);` —
  `currentViewId_` and `spriteViewId` are confirmed kept in lock-step (both default to `0`, both updated together
  at every render-target bind site, e.g. `BgfxGraphicsBackend.cpp:751-752/759-760/794-795/895-896`), so for the
  default backbuffer case (no RT bound, as in this test) `currentViewId_ == 0` holds and the override fires.
- Ordering within `renderOnce()` (test lines 100-109): `Clear()` (→ `EnsureViewState()` → resets view-0 rect to the
  *full* window size, lines 1337-1357) runs *before* `setViewportProperty(sub-region)`, which runs *before*
  `drawQuad()` (→ `ApplyViewportOverride()` → re-applies the *sub-region* rect right before the actual
  `bgfx::submit()`). This ordering is exactly what makes the sub-region rect win over `Clear()`'s full-size rect
  for the actual draw — correctly exercised by the test.
- Confirmed the `bgfx::Attrib::Position`/`Color0` semantics of the quad's drawn color are unaffected by
  lighting: `BasicEffect` defaults `LightingEnabled=false`; with `VertexColorEnabled=true` and identity
  `World`/`View`/`Projection`, the rendered color is the raw vertex color (255,0,0)/(0,255,0), matching the test's
  `isRed()`/`isGreen()` thresholds (`R>=200,G<=60,B<=60` and the mirror).
PASS — the test's core premise (sub-region Viewport clips a full-NDC quad to that region on Bgfx, and restoring
the full Viewport un-clips it) is correctly wired in current production code, and the wiring was independently
re-derived rather than assumed from the file's own comments.

### Logic
Coordinate math verified: with `gdm_->setPreferredBackBufferWidthProperty(64)`/`Height(64)` (ctor), `W=H=64`,
`leftHalfW=32`, `leftX=16`, `rightX=32+16=48`, `midY=32` — `leftX` is well inside `[0,32)` and `rightX` well
outside it, with no boundary-adjacency ambiguity. `renderOnce()`'s parameterization (`useSubregionViewport` bool)
correctly toggles between applying and not applying the sub-region `Viewport`, and check 2 restores `fullVp` before
re-drawing to confirm no lingering clip state. PASS.

### Robustness (retry-loop design)
`renderAndReadFresh()` (lines 113-125) retries the full render+read pass up to 20 times only for the "expected
non-black" sample points, and does a single non-retried render+read for "expected black" sample points
immediately following a retried (now-warm) pass. This is a sound design against the documented Bgfx
readback-staleness quirk: retrying re-runs the *actual* draw each time (not just re-reading the same frame), so a
genuine viewport-clipping regression would still reliably reproduce as black after 20 identical failed attempts
rather than being papered over — the retry only compensates for asynchronous screenshot-callback timing, not for
geometry correctness. PASS.

### C++ correctness
`isRed`/`isGreen`/`isBlack` threshold helpers (lines 46-57) use plain integer comparisons on `bytecs`-typed
`Color` component getters; no signed/unsigned or overflow concerns. `readAt()` constructs a 1×1 `Rectangle` and
calls the 4-arg `GetBackBufferData(rect, data, startIndex, elementCount)` overload correctly. PASS.

### Testing
Checks 1 and 2 are strong, independently-re-derived pixel assertions of the actual sub-region-clipping behavior.
Check 3 (get/set round-trip) is a trivial, low-risk CPU-side struct-copy check — see "Missing or Weak Tests" for
what it does not cover; this is a reasonable division of labor since `Viewport`'s own value-type correctness is
presumably covered by a dedicated unit-test file elsewhere, not this GPU-wiring integration test.

## Cross-File Observations

- The file's claimed Bgfx-only quirk ("`GetBackBufferData()` only reliably reflects the first read call per
  rendered frame") was independently verified against `BgfxGraphicsBackend::ReadBackbuffer()`
  (`BgfxGraphicsBackend.cpp:303-352`): it calls `bgfx::requestScreenShot()` then loops `bgfx::frame()` up to 3
  times waiting for the async screenshot callback — each such `bgfx::frame()` call advances/finalizes bgfx's
  internal frame state independent of the caller's own render loop, a concrete, plausible mechanism for exactly
  the "second read without a fresh draw returns stale/blank data" symptom the comment describes. This is a
  corroborated, non-stale claim (unlike some sibling-shard findings in this audit where header comments describing
  "known quirks" turned out to be outdated).
- Direct structural sibling of `easygl_viewport_subregion_test.cpp` (explicitly acknowledged in the file's own
  header, line 12) — the two exercise the same `GraphicsDevice.Viewport` XNA-facing behavior on different backends
  and should be expected to track each other if the shared `GraphicsDevice`-level API surface changes.
- `ApplyViewportOverride()`'s own comment (lines 1871-1875) documents an intentional scoping limitation — only the
  backbuffer (view 0) honors a custom sub-region `Viewport`; render-target passes stay at their
  `EnsureViewState()`/`BindAsRenderTarget()`-established full-RT-size default — and explicitly cross-references
  "Vulkan's identical RT-pass scoping decision." This test does not exercise the RT case at all (only the default
  backbuffer), so it cannot corroborate or refute that cross-referenced claim; that would need a distinct
  RT-plus-custom-viewport test, out of scope for this file.

## Missing or Weak Tests

- Check 3 (get/set round-trip, lines 172-182) verifies only `X`/`Y`/`Width`/`Height`; `Viewport` also carries
  `MinDepth`/`MaxDepth` (confirmed present in `Viewport.hpp`, defaulted to 0/1 by the 4-arg constructor used here)
  which are never asserted — a regression that corrupted `MinDepth`/`MaxDepth` storage in `GraphicsDevice`'s
  round-trip would not be caught by this file (likely acceptable, assuming a dedicated `Viewport`/`GraphicsDevice`
  unit test elsewhere covers this; not confirmed as part of this file-scoped audit).
- All three checks anchor the sub-region `Viewport` at `X=0,Y=0` (`Viewport(0,0,leftHalfW,H)`); no check in this
  file exercises a *non-zero-origin* sub-region (e.g. the right half, `Viewport(leftHalfW,0,...)`, or a
  letterboxed/offset region) with an actual pixel readback, so `ApplyViewportOverride()`'s `viewportX_`/`viewportY_`
  parameters passed to `bgfx::setViewRect()` are exercised structurally (always `0,0` here) but not verified for
  correctness at a non-zero offset by this file. (Check 3 does construct a `Viewport(10,20,300,200)`, but only
  asserts the CPU-side getter round-trip, not any GPU-visible clipping effect of the non-zero X/Y.)
- No RenderTarget2D + custom-Viewport combination is tested here, leaving `ApplyViewportOverride()`'s documented
  "only view 0 honors a custom Viewport" scoping decision unverified by this file (see Cross-File Observations).

## Positive Findings

- The core sub-region-clipping mechanism (`SetViewport` → `ApplyViewportOverride` → `bgfx::setViewRect`) was
  independently traced through five source locations across two files and confirmed to match the test's
  assumptions exactly, including the `Clear()`-then-override ordering that makes the test meaningful rather than
  vacuously true.
- The retry-loop design correctly separates "compensate for async readback timing" from "verify actual rendered
  geometry," avoiding a common testing pitfall of retries masking a real regression.
- The header comment's specific technical claim about Bgfx's readback behavior was checked against the real
  `ReadBackbuffer()` implementation rather than assumed correct, and holds up.
- `RasterizerState::CullNone` is correctly applied before each draw, sidestepping any triangle-winding/backface-
  culling ambiguity that is irrelevant to what this file is actually trying to prove (viewport clipping, not
  culling).

## Final Assessment

A well-constructed, currently-accurate GPU-wiring test. Its two pixel-verification checks were independently
re-derived against the real production call chain (not just trusted from the file's own comments) and hold up;
its documented Bgfx-specific readback quirk was checked against the actual `ReadBackbuffer()` code and is genuine,
not stale. The only gaps are coverage gaps, not correctness defects: no non-zero-origin sub-region pixel check, and
no RenderTarget-plus-custom-Viewport interaction check — both reasonable follow-ups rather than urgent fixes.
