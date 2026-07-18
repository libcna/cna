# Audit: src/CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-ascii` shard
- File type: C++ implementation (241 lines)
- Related header/implementation: `include/CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.hpp` (same shard)
- XNA/FNA relevance: a NOXNA-flavored presentation decorator (not a real terminal/TTY backend, per its own class
  comment) that reuses `SdlRenderer::SdlGraphicsBackend` for all real compositing.
- Graphics backend relevance: one of the 14 confirmed backends; architecturally a thin wrapper (`inner_`) around
  the already-audited SdlRenderer backend, plus a Present()-time glyph-grid quantization/redraw step.
- FNA reference: N/A (no FNA equivalent — presentation-only novelty backend).
- Main related tests: `examples-tests-ascii` (6 files, not yet audited).

## Purpose

Wraps a real `SdlRenderer::SdlGraphicsBackend` instance, intercepting the "target the back buffer" idiom (a null
render target) to redirect it at a private offscreen `gameTarget_` instead, so the game always draws to its own
buffer. At `Present()`, reads that buffer back, quantizes it into a colored ASCII glyph grid
(`AsciiQuantizer.hpp`), and draws that grid onto the real backbuffer via an internal, hand-authored glyph atlas
(`AsciiFontAtlas.hpp`) before presenting for real.

## Executive Verdict

**Mostly healthy.** Clean decorator pattern with correct render-target redirection and a defensible, tested-looking
quantization/redraw pipeline. One real, if narrow, finding: the forced `AlphaBlend` state `Present()` applies for
its own internal grid draw is never restored afterward, which could leak into the next frame's game rendering for
a game that doesn't re-set its own `BlendState` every frame (F1).

## Checklist Results

### API / XNA / FNA parity
N/A (NOXNA presentation novelty, no FNA equivalent).

### Behavioral correctness
`SetRenderTarget2D`/`SetRenderTargets`'s null→`gameTarget_` redirection (lines 181-195) correctly distinguishes
"target the real backbuffer" (null/count==0, redirected) from "target the game's own RenderTarget2D" (non-null,
forwarded unchanged) — verified against the constructor's own initial `RecreateGameTarget` call and `Present()`'s
final rebind, so the game can never end up drawing directly onto the real window at any point in the frame
lifecycle checked.

`DrawQuantizedGridOntoRealBackbuffer()`'s per-cell edge computation (`(row*realHeight)/grid.rows`, not accumulated
by repeated addition) is a correct, deliberate technique to avoid gaps/overlaps between adjacent cells from
rounding — confirmed correct by direct derivation (edges computed independently from row/col index, so adjacent
cells' shared boundary is always numerically identical).

### Logic

**F1 (Detailed Findings)**.

### Memory/resource lifetime
`gameTarget_`/`presentSpriteBatch_`/`fontAtlasTexture_` are all owned via `unique_ptr`, correctly recreated (not
leaked) by `RecreateGameTarget()` on `SetVirtualResolution()` (the old `unique_ptr` is overwritten, releasing the
prior render target through its own destructor before the new one is constructed — standard, correct
`unique_ptr` reassignment semantics).

### C++ correctness
No unsafe casts or lifetime issues found; `std::vector<std::uint8_t> pixels(...)` in
`DrawQuantizedGridOntoRealBackbuffer()` is sized correctly (`virtualWidth_ * virtualHeight_ * 4`) before the
`ReadBackbuffer` call that fills it.

### Performance
`DrawQuantizedGridOntoRealBackbuffer()` reads back the *entire* game-resolution framebuffer every single frame
(a full CPU readback via `ReadBackbuffer`) purely to re-quantize it — an inherent, unavoidable cost of this
backend's whole design (there is no way to quantize pixels into glyphs without seeing them), not a bug; flagged
only as an expected, design-inherent cost rather than an oversight.

### Thread safety
N/A — consistent with every other backend audited so far.

### Architecture
Clean single-responsibility decorator: every non-presentation method is a pure one-line forward to `inner_`
(verified for all ~25 forwarded methods — no forwarding call silently drops a parameter or reorders arguments).

### Maintainability
241 lines, proportionate; well-commented, especially around the two non-obvious behavioral quirks (SDL buffer-swap
invalidating immediate readback, motivating the `DrawQuantizedGridForTesting()`/`Present()` split; the forced
blend state's empirically-discovered rationale).

### Portability
N/A beyond what `SdlRenderer::SdlGraphicsBackend` (already audited) already covers.

### Robustness
`SetCellSize()` validates `width`/`height` are positive, throwing `std::invalid_argument` otherwise — the one
piece of caller-input validation in this file, correctly present.

### Testing
Not independently assessed (queued for `examples-tests-ascii`, 6 files) — the `DrawQuantizedGridForTesting()`/
`GetLastGridDimensionsForTesting()`/`ReadRealBackbufferForTesting()` testing-only API surface (correctly kept out
of the public `IGraphicsBackend` interface) suggests this backend already has dedicated, thoughtfully-designed
test hooks.

## Detailed Findings

### F1 — `Present()`'s internally-forced `AlphaBlend` state is never restored after drawing the glyph grid, and could leak into the next frame's game rendering

- Severity: LOW-MEDIUM
- Confidence: MEDIUM (a real state-leak in the code; real-world impact depends on whether any game relies on
  `BlendState` persisting across frames without re-setting it, which this audit did not verify either way)
- Category: correctness / robustness
- Location/symbol: `AsciiGraphicsBackend::DrawQuantizedGridOntoRealBackbuffer()` (lines 85-87): `inner_
  ->ApplyBlendState(One, One, InverseSourceAlpha, InverseSourceAlpha, Add, Add);` — forces `BlendState.AlphaBlend`'s
  exact factors onto the wrapped `SdlGraphicsBackend`, unconditionally, every `Present()`/
  `DrawQuantizedGridForTesting()` call, with no corresponding restore of whatever blend state was active
  beforehand.
- Why it matters: XNA's `GraphicsDevice.BlendState` is a persistent, sticky device property — a game is entitled
  to set it once (e.g. to `Additive`) and keep drawing with it across multiple frames without re-setting it every
  frame, per XNA's own state-object model. This backend's `Present()` unconditionally overwrites that tracked
  state on the *wrapped* `SdlGraphicsBackend` instance for its own internal grid draw, and never restores the
  game's own value afterward — so the *next* frame's game draws (if the game doesn't itself call
  `GraphicsDevice.BlendState = ...` or `SpriteBatch.Begin(blendState: ...)` again before drawing) would
  unexpectedly render with `AlphaBlend` instead of whatever the game last explicitly set.
- FNA/XNA comparison: real XNA/FNA's `GraphicsDevice.BlendState` persists until explicitly changed; this backend's
  internal presentation step silently changes it as a side effect, which is not something any real backend
  should do to a value XNA contracts as under the *game's* control.
- Related files: `include/CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.hpp` (would need a way to snapshot/
  restore the wrapped backend's current blend factors, which `IGraphicsBackend`'s interface doesn't currently
  expose a getter for — `ApplyBlendState` is set-only, so a fix would need either a new getter or for
  `AsciiGraphicsBackend` to track the last-applied factors itself alongside `inner_`).
- Suggested future action (not implemented by this audit): track the game's most recently `ApplyBlendState`-set
  factors in `AsciiGraphicsBackend` itself (intercepting the call the same way `SetRenderTarget2D` already
  intercepts render-target changes) and restore them after the internal grid draw, or add a save/restore
  mechanism to `IGraphicsBackend` if other decorator-style backends need the same capability.

## Cross-File Observations

- This is the *third* backend using the "wrap a real backend and intercept a subset of calls" decorator pattern
  found in this audit (after Headless/Software's from-scratch implementations and unlike EasyGL/Vulkan/etc.'s
  from-scratch GPU implementations) — Canvas is confirmed (per `cmake/BackendSelection.cmake`'s comment) to be
  another GPU-free 2D-only backend; worth checking whether it shares a similar wrap-and-intercept shape and
  whether it has the same blend-state-leak risk when that shard is audited.
- Does not call `IGraphicsBackend::RegisterForWindow` at all (consistent with the wrapped `SdlGraphicsBackend`,
  which also doesn't) — so this backend is not subject to the EasyGL-style constructor-ordering risk (F1 in that
  report) at all, by construction.

## Missing or Weak Tests

Given F1, a test that sets a non-`AlphaBlend` `BlendState`, calls `Present()`, then draws again *without*
re-setting `BlendState` and checks the second draw's actual blend behavior would directly catch this — not
assessed against the actual `examples-tests-ascii` content yet (queued).

## Positive Findings

- Correct, thorough render-target redirection preventing the game from ever drawing directly to the real window.
- Thoughtful test-support API (`DrawQuantizedGridForTesting()` etc.) with a clearly-documented rationale for why
  it exists separately from the real `Present()`.
- Correct rounding-safe cell-edge computation.

## Final Assessment

A clean, well-designed decorator backend with one narrow but real state-leak finding (F1) that a future game could
plausibly hit if it relies on XNA's persistent-BlendState semantics across frames.
