# Audit: examples/sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 707, `GraphicsDevice::GetBackBufferData` (no-rect overload)
  correctness after a `RenderTarget2D` bind/unbind cycle.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_GetBackBufferData_AfterRtUnbind`
  / `cna_test_sdl_getbackbufferdata_after_rt_unbind`, `cmake/Tests/SdlRendererTests.cmake:271-273`).
- XNA/FNA relevance: `GraphicsDevice.GetBackBufferData(T[])`/`SetRenderTarget(RenderTarget2D)` semantics.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`GetBackBufferData(const Rectangle*, Color*, int, int)` lines 1778-1813, `SetRenderTarget(RenderTarget2D*)`
  lines 1821-1848, `ResetViewportAndScissorForRenderTarget` lines 1815-1819),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`GetViewportSize` lines 556-578,
  `SetRenderTarget2D`/`SdlRenderTargetBackend::BindAsRenderTarget`/`UnbindAsRenderTarget` lines 731-759,
  `ReadBackbuffer` lines 591-641).

## Purpose

Verifies that the no-rect `GraphicsDevice::GetBackBufferData(Color*, int)` overload (which resolves its region
from `backend_->GetViewportSize()` rather than an explicit `Rectangle`) still reads the *entire, correct*
16x16 backbuffer after a smaller (4x4) `RenderTarget2D` is bound, drawn into, and unbound — clearing the
backbuffer Magenta, binding+clearing a 4x4 RT Cyan, unbinding, then confirming both the `Viewport` and a
full no-rect readback have reverted to the original 16x16 Magenta backbuffer.

## Executive Verdict

**Mostly healthy** — the test's actual assertions are correct and the underlying behavior they check genuinely
holds (independently re-traced through `GraphicsDevice`/`SdlGraphicsBackend`). One finding (F1): the test's own
header comment describes a specific failure *mechanism* ("if the viewport/size resolution were left stale") that,
on inspection of the current code, cannot actually occur through the path described — a mild case of the
stale-claim pattern this audit was asked to check for, though it does not undermine the test's validity as a
regression check.

## Checklist Results

### API / XNA / FNA parity

`GetBackBufferData(Color*, int)` (the two-argument, whole-backbuffer overload used here, line 87/104) forwards
to `GetBackBufferData(nullptr, data, 0, elementCount)` (`GraphicsDevice.cpp:1768-1771`) — matches FNA's
`GraphicsDevice.GetBackBufferData<T>(T[])` overload signature and semantics. `SetRenderTarget(RenderTarget2D*)`
correctly resets `Viewport`/`ScissorRectangle` to the new target's (or, on `nullptr`, the backbuffer's) size —
matches FNA's own `SetRenderTargets` viewport-reset convention, confirmed by the code comment at
`GraphicsDevice.cpp:1834-1835` ("Matches FNA: Viewport/ScissorRectangle always reset...").

### Behavioral correctness

Traced the actual mechanics: `GetBackBufferData(rect=nullptr, ...)` (`GraphicsDevice.cpp:1791-1796`) calls
`backend_->GetViewportSize(w, h)` to resolve `w`/`h` when no explicit rect is given.
`SdlGraphicsBackend::GetViewportSize` (lines 556-578) returns `logicalWidth`/`logicalHeight` — member fields on
the backend that are set only in the constructor and in `SetVirtualResolution()`. Grepped every call site of
`SetVirtualResolution` across all 13 backends and `GraphicsDevice.cpp`: it is invoked only from
`GraphicsDevice::Reset()`/`GraphicsDevice::SetVirtualResolution()`, never from `SetRenderTarget()`/
`SetRenderTarget2D()`/`BindAsRenderTarget()`/`UnbindAsRenderTarget()`. This confirms the actual, present-day
behavior the test's own two checks assert (Viewport reverts to 16x16; readback is all-Magenta) is correct.

### Logic

See F1 below — the specific "staleness" mechanism the file's header comment describes for *why* this test is
meaningful does not correspond to an actual code path in the current implementation, even though the assertions
themselves are sound.

### Robustness

`IsMagenta()`'s tolerant match (`R>=240 && G<=15 && B>=240`, line 46-49) is intentionally loose enough to absorb
ordinary SDL/GPU pixel-format rounding while still discriminating Magenta from Cyan (the RT's fill colour) or
black (an unwritten buffer) — a sound choice for a byte-exact-adjacent pixel check.

### Testing

This is itself a test file; its own coverage is the subject. The two checks (Viewport reversion, full-backbuffer
pixel content) correctly discriminate the two independent ways this could regress (XNA-level `Viewport` property
vs. the byte-level backbuffer content), and the "Control" no-rect readback before any RT activity (line 85-90) is
a good baseline that rules out `IsMagenta`/`Clear` itself being broken, isolating the RT bind/unbind cycle as the
only variable under test.

## Detailed Findings

### F1 — Header comment's stated failure mechanism ("stale viewport/size resolution") does not match how `GetBackBufferData`'s no-rect path or `SetRenderTarget` actually interact in the current code

- Severity: LOW
- Confidence: HIGH (traced every call site of the relevant state)
- Category: test-authoring / documentation-accuracy (not a behavioral defect)
- Location/symbol: header comment lines 14-19; `GraphicsDevice::GetBackBufferData` (`GraphicsDevice.cpp:1778-1813`);
  `SdlGraphicsBackend::GetViewportSize`/`SetRenderTarget2D` (`SdlGraphicsBackend.cpp:556-578`, `753-759`)
- Evidence: the comment states the no-rect overload "internally resolves its region from
  `backend_->GetViewportSize()` ... so that if the viewport/size resolution were left stale (still reflecting the
  smaller render target instead of reverting to the backbuffer), the full-backbuffer readback would only fill part
  of the buffer." But `SdlGraphicsBackend::GetViewportSize()` returns `logicalWidth`/`logicalHeight`, fields that
  are set *only* by `SetVirtualResolution()` — a method never called by `SetRenderTarget2D()`,
  `SdlRenderTargetBackend::BindAsRenderTarget()`, or `UnbindAsRenderTarget()` (confirmed by grepping every call
  site of `SetVirtualResolution` project-wide). Binding/unbinding an RT of any size therefore cannot, in the
  current architecture, change what `backend_->GetViewportSize()` returns on this backend — the "stale" scenario
  described is not reachable through the mechanism named.
- Why it matters: a future reader debugging a regression matching this description would look in the wrong place
  (RT-bind coupling to logical resolution) rather than the actual risk surface, which is really about whether
  `SDL_SetRenderTarget(renderer, nullptr)` genuinely restores the real backbuffer as the active render target at
  the SDL level (which this test *does* correctly exercise and which does hold, per `SdlRenderTargetBackend::
  UnbindAsRenderTarget`/`SdlGraphicsBackend::SetRenderTarget2D`). The test's pass/fail outcome and its value as a
  regression check are unaffected — only the comment's causal narrative is inaccurate.
- FNA/XNA comparison: N/A (backend-internal implementation detail, not an XNA-facing behavior question).
- Related files: none — self-contained to this file's own header comment.
- Suggested future action (not implemented by this audit): reword the comment to describe the actual risk being
  guarded against (SDL-level render-target unbinding not genuinely restoring the window backbuffer as the active
  target, or a future refactor accidentally coupling `logicalWidth`/`logicalHeight` to RT binding) rather than a
  "stale GetViewportSize()" mechanism that cannot occur in the code as it stands today.

## Cross-File Observations

- This test complements the `SdlGraphicsBackend.cpp` audit's own coverage gap note ("not independently assessed,
  queued for `examples-tests-sdlrenderer`") for `ReadBackbuffer`'s physical/logical mismatch throw — this file
  specifically requires `PresentationMode::NativeBackBuffer` for exactly the reason that audit's Behavioral
  Correctness section already documented (`SDL_RenderReadPixels` operates in physical coordinates).
- `ResetViewportAndScissorForRenderTarget` (`GraphicsDevice.cpp:1815-1819`) is the single shared code path behind
  both the RT-bind and RT-unbind viewport resets this test exercises — correctly reused rather than duplicated.

## Missing or Weak Tests

None identified beyond F1's documentation-accuracy note. The test itself is a good, previously-missing regression
check (there was no prior SDL_Renderer coverage combining a smaller RT bind/unbind with a full no-rect backbuffer
readback, per the file's own framing).

## Positive Findings

- Correctly isolates the RT-bind/unbind interaction as the only variable via an explicit "Control" baseline
  readback before touching any `RenderTarget2D`.
- Uses `PresentationMode::NativeBackBuffer` deliberately and explains why, consistent with this shard's other
  pixel-verification tests and this backend's own documented physical/logical coordinate distinction.
- Correctly distinguishes (via two separate `check()` calls) the XNA-level `Viewport` property reversion from the
  byte-level backbuffer content reversion — two genuinely independent things a bug could get wrong separately.

## Final Assessment

A solid, currently-passing regression test whose assertions are correct and independently re-verified against
the real `GraphicsDevice`/`SdlGraphicsBackend` code paths. Its only flaw is a comment that describes an
inaccurate failure mechanism (F1) — a documentation/test-authoring nit, not a functional defect in the test or
the code it covers.
