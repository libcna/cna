# Audit: examples/sdlrenderer_presentationparameters_resize_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_presentationparameters_resize_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 711, backbuffer resize via `PresentationParameters`/
  `GraphicsDevice::Reset()`.
- File type: standalone `Game`-subclass executable, 2-frame state machine, CTest-registered
  (`SDL_Renderer_PresentationParameters_Resize` / `cna_test_sdl_presentationparameters_resize`,
  `cmake/Tests/SdlRendererTests.cmake:295-297`).
- XNA/FNA relevance: `GraphicsDevice.Reset(PresentationParameters)` real-surface-resize semantics.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`Reset(const PresentationParameters&, GraphicsAdapter*)`, lines 389-439; `UpdateViewportFromWindow`, lines
  1513-1561), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SetVirtualResolution`, lines 499-507; `Present`, lines 474-497 — the async output-size re-detection).

## Purpose

Confirms `GraphicsDevice::Reset(PresentationParameters)`'s whole chain (store new parameters →
`applyPresentationParametersToWindow()` → `backend_->SetVirtualResolution()` → `UpdateViewportFromWindow()`)
genuinely resizes the *real* rendering surface on SDL_Renderer, not just the XNA-level property values — draws a
full-viewport fill at 32x16, resizes to 64x32 via `Reset()`, and reads back a pixel near the new, larger edge
(well outside the old 32x16 bounds) on the *next* `Draw()` call to confirm the real surface grew. Explicitly
defers the post-resize pixel check to the next frame because real `SDL_SetWindowSize()` resizes are asynchronous
under X11, and reading within the same frame as `Reset()` would hit `ReadBackbuffer`'s own deliberate
physical/logical-mismatch throw before the resize has landed.

## Executive Verdict

**Healthy** — the property-level assertions (checked same-frame) and the deferred pixel-level assertion (checked
next-frame) were both independently re-traced against the actual `GraphicsDevice`/`SdlGraphicsBackend` code and
match exactly; the file's own reasoning for *why* the pixel check must be deferred is correct and consistent with
this backend's already-audited `ReadBackbuffer` design.

## Checklist Results

### Behavioral correctness

Traced `GraphicsDevice::Reset(const PresentationParameters&, GraphicsAdapter*)` (`GraphicsDevice.cpp:389-439`):
line 393 stores the new `presentationParameters_` immediately, line 406 calls
`applyPresentationParametersToWindow()`, and — critically for this test's same-frame property assertions — line
410 calls `backend_->SetVirtualResolution(virtualWidth_, virtualHeight_)` *synchronously*, followed by line 437
`UpdateViewportFromWindow()`. `SdlGraphicsBackend::SetVirtualResolution` (`SdlGraphicsBackend.cpp:499-507`)
updates `logicalWidth`/`logicalHeight` immediately (no async wait). `UpdateViewportFromWindow`
(`GraphicsDevice.cpp:1513-1561`) then calls `backend_->GetViewportSize(width, height)`, which — since
`logicalWidth`/`logicalHeight` were just updated — returns the *new* 64x32 immediately, and pushes this into
`viewport_`. This confirms the test's frame-1 same-frame assertions (lines 96-100:
`PresentationParameters`/`Viewport` both report 64x32 immediately after `Reset()`) are checking something that
genuinely *is* synchronous in the current implementation — not incorrectly assuming synchronicity where none
exists.

The *pixel*-level check is correctly NOT attempted in the same frame — the file's header comment (lines 16-25)
explains this precisely: `SDL_SetWindowSize()` itself is asynchronous under X11, and
`SdlGraphicsBackend::Present()` (lines 474-497) is what detects the real output size actually changing (comparing
against `lastOutputW_`/`lastOutputH_`) and re-applies logical presentation to match — meaning a same-frame
`GetBackBufferData` call (before any `Present()` has run) would hit `ReadBackbuffer`'s own
physical/logical-size-mismatch guard (already confirmed correct in this shard's `SdlGraphicsBackend.cpp` audit)
and throw, rather than silently returning wrong pixels. Deferring to frame 2 (after a full `Draw()`→`Present()`
cycle has had a chance to run, per the engine's normal `Update()`/`Draw()` loop) is the technically correct way to
test this, and the comment's own framing ("expected, correct behavior for a same-frame read, not a bug") is
accurate given the code actually inspected, not merely asserted.

### API / XNA / FNA parity

`PresentationParameters.BackBufferWidth`/`BackBufferHeight` mutation + `GraphicsDevice.Reset()` is exactly FNA's
real resize API surface — this test does not invent a CNA-only resize mechanism.

### Logic

`frame_` state machine (lines 47, 69-120) correctly gates all frame-1 logic behind `if (frame_ == 1)` with an
explicit `return;` (line 103) before any frame-2 logic could run in the same call, and frame-2 logic is gated
behind `if (frame_ == 2)` — no risk of both blocks executing in the same `Draw()` invocation, and no `done_`-style
guard is needed since `Exit()` (line 118) is called at the end of frame 2.

### Robustness

Both pixel checks (`beforeRegion(30, 14, 1, 1)` at 32x16 pre-resize, `afterRegion(60, 28, 1, 1)` at 64x32
post-resize) are deliberately placed near the *new* size's far corner, past the *old* size's bounds — a
meaningfully strict placement, since a bug that only grew the logical viewport property without actually
reconfiguring the real SDL renderer/logical-presentation size would show these coordinates sampling out-of-bounds
or clamped/garbage data rather than the fill colour.

### Testing

Cleanly isolates two independently-failing hypotheses: "does `Reset()` update the XNA-level properties" (frame 1,
synchronous) vs. "does the real underlying surface actually grow" (frame 2, deferred for the real async resize to
land) — a well-reasoned two-phase test structure appropriate to the actual asynchronous nature of the underlying
platform resize.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — every claim in this file's own header comment was independently traced
against the current `GraphicsDevice`/`SdlGraphicsBackend` implementation and found accurate, including the more
subtle claim about *why* a same-frame pixel read is invalid (X11 async resize + `Present()`-driven logical
presentation re-application), which is a materially harder claim to get right than a simple "value round-trips"
assertion.

## Cross-File Observations

- Shares the `PresentationMode::NativeBackBuffer` requirement and rationale with every other pixel-verification
  test in this shard (`sdlrenderer_getbackbufferdata_after_rt_unbind_test.cpp`,
  `sdlrenderer_multisamplecount_decision_test.cpp`, `sdlrenderer_presentinterval_test.cpp`) — consistent,
  correctly-explained precondition reused verbatim across the shard rather than re-derived (and potentially
  gotten subtly wrong) each time.
- Directly corroborates the `SdlGraphicsBackend.cpp` audit's own description of `Present()`'s output-size
  re-detection logic (`lastOutputW_`/`lastOutputH_` comparison) as a real, load-bearing mechanism this test
  actually depends on for its frame-2 assertion to be meaningful — a good example of a test file substantiating a
  production-code audit's claim rather than merely being adjacent to it.

## Missing or Weak Tests

None significant. A shrink-resize (64x32 → smaller) companion case is not present here, but the grow case
already exercises the more failure-prone direction (a stale/too-small logical presentation silently clipping the
new, larger content) more meaningfully than a shrink would.

## Positive Findings

- Correctly reasons about, and works around, real asynchronous OS-level window-resize behavior rather than
  assuming a same-frame check would work — a materially more sophisticated test design than a naive resize test
  would produce.
- Clean separation of same-frame-safe property assertions from next-frame-only pixel assertions, each placed
  exactly where the underlying implementation genuinely supports checking it.
- Deliberately samples near the new size's far edge rather than a safely-interior point, maximizing the check's
  ability to catch a partial/incomplete resize.

## Final Assessment

A carefully reasoned test whose two-phase (same-frame property / next-frame pixel) structure correctly reflects
genuine synchronous-vs-asynchronous behavior in the underlying `GraphicsDevice`/`SdlGraphicsBackend`/SDL/X11
stack. No discrepancies found between its claims and the current implementation.
