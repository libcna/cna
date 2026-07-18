# Audit: examples/bgfx_graphicsdevice_clear_stencil_test.cpp

## Metadata

- Source file: `examples/bgfx_graphicsdevice_clear_stencil_test.cpp` (197 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `GraphicsDevice::Clear()` actually clearing the stencil
  buffer to the requested value on `ClearOptions` combinations including `ClearOptions::Stencil`,
  Bgfx backend, Task 871.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_graphicsdevice_clear_stencil …)` /
  `cna_register_backend_test(NAME Bgfx_GraphicsDevice_ClearStencil …)`
  (`cmake/Tests/BgfxTests.cmake:642-644`).
- XNA/FNA relevance: direct — `GraphicsDevice.Clear(ClearOptions, Color, float, int)`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`Clear(ClearOptions,...)` dispatch to `backend_->ClearStencil`/`ClearColorDepthAndStencil`/etc.,
  lines 284-365), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ClearStencil()`,
  `ClearColorDepthAndStencil()`, lines 1974-2000, and the shared `EnsureViewState()`, lines
  1325-1382).

## Purpose

Two-check, two-real-frame pixel test verifying `GraphicsDevice::Clear()` genuinely clears the
stencil buffer to the requested value on Bgfx. Frame N stamps the whole screen to a known stencil
value via a real draw (`StencilFunction::Always`/`StencilPass::Replace`); frame N+1 issues the
`Clear()` under test, then a `StencilFunction::Equal` test draw comparing against the value the
clear was supposed to establish. Check A exercises `ClearOptions::Stencil` alone (clear to `0x03`
over a `0x07` stamp, testing `IGraphicsBackend::ClearStencil`); Check B exercises
`Target|DepthBuffer|Stencil` together with a *non-zero* stencil value (`0x09`, testing
`ClearColorDepthAndStencil`), deliberately non-zero so a broken dispatch leaving the value at an
uninitialized/leftover `0` default cannot coincidentally still pass.

## Executive Verdict

**Needs attention** — both checks pass and genuinely verify that `Clear()` establishes the
requested stencil value (the file's own stated purpose), but this audit's independent trace of
`EnsureViewState()` found that the Bgfx backend cannot currently perform a genuinely *selective*
clear at all: every `Clear*()` call unconditionally re-clears color+depth+stencil together
(F1) — a real production-code gap this file's own scene happens not to expose, because its
background color/depth are identical across every frame in the test.

## Checklist Results

### Behavioral correctness
`StampStencil()` (lines 84-98) clears `Target|DepthBuffer` then draws a full-screen quad with
`StencilFunction::Always`/`StencilPass::Replace`/`ReferenceStencil=value` — a standard, correct
technique to unconditionally write a known stencil value regardless of the buffer's prior contents.
`ClearThenTestStencilEquals()` (lines 100-121) issues the `Clear()` under test, then draws a green
quad gated by `StencilFunction::Equal`/`ReferenceStencil=expected`/`StencilPass=StencilFail=Keep` —
correctly non-destructive to the stencil buffer it's testing (both pass and fail paths `Keep`).
`DrawQuad()` (lines 57-71) applies `RasterizerState::CullNone` (per the Task 896 finding,
independently re-verified against `RasterizerState.cpp:11`'s `CullCounterClockwiseFace` default and
`BgfxGraphicsBackend.cpp:1781-1782`'s `BGFX_STATE_CULL_CCW` mapping — accurate, current).
`ApplyBasicEffect()`'s `IsGreen`-adjacent readback threshold (`c.getGProperty() >= 200 &&
c.getRProperty() <= 60 && c.getBProperty() <= 60`, line 120) is a reasonably generous but sound
green-detection heuristic against the `kBackground(20,20,20)` vs `kGreen(0,255,0)` palette used.

### Logic — the two-real-frame structure is necessary and independently confirmed sound
The header comment (lines 6-11) explains the stamp/clear/compare sequence is deliberately spread
across two real game `Draw()` frames (rather than three sequential calls within one `Draw()`)
because "a same-frame … sequence cannot reliably discriminate this fix on a backend whose own
render pass/view clear always applies before that frame's draws regardless of call order." This
audit traced `ReadBackbuffer()` (`BgfxGraphicsBackend.cpp:303-325`) and confirmed it internally
calls `bgfx::frame()` up to 3 times to force the screenshot callback to fire — meaning any
`GetBackBufferData()` call mid-`Draw()` already advances bgfx's own frame counter, so a genuinely
same-frame "stamp, then Clear, then compare" sequence would have its two draws land in *different*
bgfx frames anyway (each with its own `EnsureViewState()`-driven view clear), corroborating the
comment's reasoning independently rather than merely trusting it.

## Detailed Findings

### F1 — Bgfx's `EnsureViewState()` unconditionally clears color+depth+stencil on every `Clear*()` call, regardless of the `ClearOptions` actually requested — a genuine violation of XNA's selective-clear semantics that this test's own scene cannot expose

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production code) / test-coverage (this file's own check design)
- Location/symbol: `BgfxGraphicsBackend.cpp:1380-1381` — `bgfx::setViewClear(spriteViewId,
  BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, clearRgba, clearDepthValue_,
  clearStencilValue_);` inside `EnsureViewState()`, called unconditionally from every one of
  `Clear()`, `ClearStencil()`, `ClearDepthAndStencil()`, `ClearColorAndStencil()`,
  `ClearColorDepthAndStencil()`, and `ClearColorAndDepth()` (lines 1384-2000). `ClearStencil(int
  stencil)` itself (lines 1974-1979) only assigns `clearStencilValue_` and leaves `clearRgba`/
  `clearDepthValue_` at whatever they were last set to — but `EnsureViewState()` re-requests *all
  three* clear flags regardless, using those stale cached values.
- Evidence: this test's own Check A calls `dev.Clear(ClearOptions::Stencil, kBackground, 1.0f,
  0x03)` (line 148), which `GraphicsDevice::Clear(ClearOptions,...)` correctly dispatches to
  `backend_->ClearStencil(0x03)` alone (`GraphicsDevice.cpp:361-363`, since only the `Stencil` bit
  is set). But `ClearStencil()`'s call to `EnsureViewState()` still requests
  `BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH|BGFX_CLEAR_STENCIL` together — i.e. the color and depth
  buffers are *also* re-cleared (to `clearRgba`/`clearDepthValue_`, whatever they last held), not
  left untouched as real XNA/FNA `ClearOptions.Stencil`-alone semantics require. This test's own
  scene happens not to expose it only because `clearRgba` is `kBackground` in every single
  `Clear()` call across both frames (`StampStencil()` and `ClearThenTestStencilEquals()` both pass
  `kBackground`, lines 86, 103) and `clearDepthValue_` is likewise `1.0f` throughout — so the
  redundant, semantically-wrong re-clear is visually indistinguishable from a correctly-selective
  one in this specific scenario.
- Why it matters: any real game that (a) renders something to the color buffer, then (b) calls
  `Clear(ClearOptions.Stencil, ...)` expecting the just-rendered color content to persist (a common
  technique — e.g. resetting a stencil mask between two stencil-gated passes over the same rendered
  scene) will have that color buffer silently wiped to a stale/default clear color on the Bgfx
  backend, a real behavioral divergence from FNA. The same applies to `ClearOptions.DepthBuffer`
  alone and any other partial combination not equal to all three.
- FNA/XNA comparison: real `GraphicsDevice.Clear(ClearOptions options, ...)` clears *only* the
  buffer(s) named in `options`, leaving the others' content genuinely untouched — this is
  fundamental to the API's whole reason for taking a bitmask rather than always clearing everything.
- Related files: `BgfxGraphicsBackend.cpp` (this audit's finding is entirely within the production
  backend file, not the test itself — cited per the audit's instruction to check the code a test
  exercises).
- Suggested future action (not implemented by this audit): gate the flags passed to
  `bgfx::setViewClear()` in `EnsureViewState()` on which aspect(s) were actually requested by the
  most recent `Clear*()` call (tracking a "pending clear mask" alongside the existing
  `clearRgba`/`clearDepthValue_`/`clearStencilValue_` cached values), rather than always requesting
  all three.
- Consequence for this file specifically: Check A's own label ("`ClearOptions::Stencil` alone …
  exercises `IGraphicsBackend::ClearStencil`", header comment line 18) is accurate as far as it
  goes (the value written IS correct, and the dispatch IS to `ClearStencil` alone), but the test
  cannot and does not verify the "alone" (i.e., color/depth-preserving) half of that claim — it only
  verifies the resulting stencil value, not that color/depth were left untouched. A stronger version
  of Check A would render a *distinctive* (non-background) color in frame N before the stencil-only
  clear in frame N+1, then assert that color is still present afterward.

## Cross-File Observations

- `ApplyBasicEffect()`'s `static BasicEffect* fx = nullptr` lazily-constructed, never-destroyed
  singleton (lines 74-82) is an unusual pattern for a short-lived test executable — harmless here
  (the process exits shortly after `Exit()` is called, lines 168-172, so the leaked heap allocation
  is reclaimed by the OS), but worth noting as a style outlier versus this batch's sibling
  `reference_stencil_test.cpp`, which constructs a fresh `BasicEffect` per iteration instead (see
  that file's own report).
- Shares the exact `DrawQuad()`/`CullNone` idiom and `Task 896` cull-mode finding with
  `bgfx_graphicsdevice_reference_stencil_test.cpp` (this batch's other stencil file) —
  cross-verified identically in both reports rather than assumed shared.

## Missing or Weak Tests

- See F1 — no check in this file asserts that a partial `ClearOptions` clear leaves the
  *un-requested* buffer(s)' content genuinely untouched; both checks here only assert the
  *requested* buffer ends up correct, which is a strictly weaker property than what
  `ClearOptions`'s bitmask API contract promises.
- No `ClearOptions::DepthBuffer`-alone check exists in this file (only `Stencil`-alone and
  `Target|DepthBuffer|Stencil`-together); given `ClearDepth()` itself is a documented no-op on this
  backend (`BgfxGraphicsBackend.cpp:1969`, "Bgfx depth-only clear not yet implemented"), a
  depth-alone variant of this test would likely fail today and is presumably why it doesn't exist —
  but this remains an unaudited/untested combination worth flagging.

## Positive Findings

- Both checks' expected stencil values are deliberately chosen to avoid false-pass-by-coincidence:
  Check A clears to `0x03` over a `0x07` stamp (a value change, not a clear-to-zero-over-nonzero
  ambiguity), and Check B deliberately uses non-zero `0x09` specifically to rule out an
  uninitialized/leftover-`0` stencil value coincidentally matching.
- The two-real-frame test structure was independently confirmed necessary (not just claimed) by
  tracing `ReadBackbuffer()`'s own internal `bgfx::frame()` calls.

## Final Assessment

Both of this file's own checks are sound and pass for the right reason — `Clear()` does establish
the requested stencil value correctly on Bgfx. This audit's independent trace of the shared
`EnsureViewState()` clear path, however, surfaced a genuine, previously untracked defect (F1): Bgfx
cannot perform a real selective clear at all, silently re-clearing color and depth on every call
regardless of the requested `ClearOptions` — a defect this file's own uniform-background scene
cannot and does not expose.
