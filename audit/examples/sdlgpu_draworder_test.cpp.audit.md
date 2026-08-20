# Audit: examples/sdlgpu_draworder_test.cpp

## Metadata

- Source file: `examples/sdlgpu_draworder_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — real chronological draw-order regression proof
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_draworder …)` /
  `cna_register_backend_test(NAME SdlGpu_DrawOrder …)`, `cmake/Tests/SdlGpuTests.cmake:112-114`,
  `TIMEOUT 60`).
- XNA/FNA relevance: indirect — the underlying XNA contract (draw calls are issued and rendered
  in the order the application makes them, "last write wins" for opaque overlapping geometry) is
  implicit rather than an explicit named API, but is fundamental to `GraphicsDevice.Draw*`/
  `SpriteBatch.Draw` semantics.
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`drawOrder_`, `DrawKind` enum, `RenderQueuedDraws()`).

## Purpose

Regression proof for a real, previously-confirmed bug (the file's own header calls it
"adversarial-review finding #4"): this backend used to always render every queued 3D draw before
every queued `SpriteBatch` sprite, regardless of the real order the application called
`Draw()`/`SpriteBatch.Draw()` in. Two checks, each drawing a full-screen opaque 3D quad (green)
and a full-screen opaque sprite (red) into the same offscreen `RenderTarget2D`, with depth testing
off so there is no ambiguity beyond draw order: Check A issues sprite-then-3D (3D, issued later,
must win — expect GREEN); Check B issues 3D-then-sprite (sprite, issued later, must win — expect
RED). Both readbacks use `RenderTarget2D::GetData()`. The two checks together are a genuine,
unambiguous discriminator against the specific old bug: the old fixed-family-order implementation
would have produced RED for *both* checks (sprites always rendered last regardless of real call
order), not GREEN-then-RED.

## Executive Verdict

**Healthy** — this is a well-constructed, real regression test. Its central claim (a
`drawOrder_` vector of `{DrawKind, index}` pairs, replayed in real chronological order by
`RenderQueuedDraws()`) was independently confirmed against the current production source, not
just taken from the header comment, and the fix it guards is confirmed via `git log` to be a real,
already-landed change (`35d60fcc`/`e41d0b3d`, "close SDLGPU-52 -- real chronological draw
ordering") — not a stale or aspirational claim.

## Checklist Results

### API / XNA / FNA parity
N/A directly (no single named XNA API is under test) — the underlying implicit ordering contract
is correctly identified and is the same one every backend must honor for correct
`SpriteBatch`+3D-mixed-scene rendering.

### Behavioral correctness
Independently traced `RenderQueuedDraws()` (confirmed present at
`SdlGpuGraphicsBackend.cpp:3555`) and its `drawOrder_` population: every `Queue*Draw()` method in
this backend (`QueueColoredDraw`, `QueueTexturedDraw`, `QueueLitTexturedDraw`, `QueueAlphaTestDraw`,
`QueueDualTextureDraw`, `QueueEnvMapDraw`, `QueueSkinnedDraw`, `QueuePbrDraw`, and the sprite-batch
path) appends exactly one `{DrawKind::X, index}` entry to `drawOrder_` at the moment the draw is
issued (confirmed via grep: 9 distinct `drawOrder_.push_back` call sites, one per draw family),
and `RenderQueuedDraws()` (lines 3555-3627+) iterates `drawOrder_` in insertion order, dispatching
each entry to its family-specific `Issue*Draw()` by a `switch` on `DrawKind` — this is a genuine
real-order replay, not a fixed-family-order render loop with a superficial rename. The fix this
file guards is confirmed real and current.

### Logic
The `DrawSprite()`/`Draw3DQuad()` helpers both explicitly set `dev.setBlendStateProperty(
BlendState::Opaque)` before drawing and `Draw3DQuad()` explicitly disables depth testing
(`dev.SetDepthTestEnabled(false)`) — correctly removing blend-math and depth-test ambiguity so
"last write wins" is the *only* variable under test, exactly as the header comment states.

### C++ correctness
Uses a function-local `static bool done` guard (line 134) inside `Draw()` to ensure the two-check
scene only runs once despite `Draw()` being called every frame by the `Game` loop until `Exit()`
takes effect — functionally correct for a single-instance-per-process test binary (this pattern
recurs correctly, if a `static` local is a slightly unusual choice over a member `bool` given the
class already has member state for `passCount_`/`result_`; purely a style observation, not a
defect, since `SdlGpuDrawOrderTest` is only ever constructed once in `main()`).

### Robustness
Correctly clears each `RenderTarget2D` with `Color::Black` before drawing into it (line 143, 151)
so a "GetData returns black" failure mode (e.g., an unbound render target silently returning
default-constructed zero data) would visibly read back as neither GREEN nor RED, not accidentally
matching one of the expected colors by coincidence.

### Testing
Both checks are strong, targeted, and their own header comment's claim about what the *old* bug
would have produced (RED for both) was independently verified plausible given the confirmed real
fix mechanism — a regression back to fixed-family-order rendering would indeed make both checks
fail in the specific, diagnosable way the header describes.

## Detailed Findings

No correctness defects found. Two minor, non-blocking observations below (not raised to Detailed
Findings severity since neither affects correctness):

- The `static bool done` guard (line 134) is functionally correct here but is a slightly unusual
  choice given `passCount_`/`result_` are already instance members tracking this same "already
  ran" state implicitly (checking `frame_`/an explicit member `bool` would be marginally more
  consistent with sibling files in this shard, which mostly use a `frame_` counter). LOW
  severity/INFO — no behavioral difference for a single-construction-per-process test binary.
- `Matches()`'s ±10 per-channel tolerance (line 62) is reasonable for GPU/driver blending noise on
  an *opaque*, untinted, full-coverage quad/sprite pair — the two expected colors (pure green,
  pure red) are maximally far apart in RGB space, so this tolerance carries essentially zero risk
  of a false-positive match between the two, unlike some MSAA-edge or partial-alpha scenarios
  elsewhere in this project where tolerance choice has previously mattered more.

## Cross-File Observations

- This file and `sdlgpu_envmap_test.cpp` are the only two files in this batch that actually
  perform a real pixel-level readback via `RenderTarget2D::GetData()` — both confirm this
  mechanism genuinely works on this backend (matches `plans/plan_sdlgpu.md SDLGPU-39`'s claim that
  `RenderTarget2D::GetData()`, unlike swapchain readback, is fully implemented and verified).
  `sdlgpu_2d_test.cpp`/`sdlgpu_3d_test.cpp`/`sdlgpu_effects_test.cpp` in this same batch do *not*
  use this mechanism despite it being available and proven — see those files' own reports
  (particularly `sdlgpu_3d_test.cpp`'s F1) for the missed-opportunity note.
- `dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr))` (lines 146, 154) uses an explicit
  cast that is not strictly required by overload resolution — `GraphicsDevice` has only one
  single-argument `SetRenderTarget` overload (`RenderTarget2D*`), confirmed via
  `GraphicsDevice.hpp:296`/`302`/`307` (the other two overloads take 2+ arguments), so a bare
  `nullptr` argument would not be ambiguous. Purely a style/defensive-clarity choice, repeated
  consistently across every file in this shard that clears a render target — not a defect.

## Missing or Weak Tests

- No case exercises three or more interleaved draw kinds in a single scene (only sprite+3D-quad
  pairs) — acceptable given the two checks already fully discriminate the specific historical bug
  they target; a richer interleaving (e.g., sprite→3D→sprite→3D) would be a stronger regression
  guard against a *partial* fix (e.g., one that correctly orders sprites-vs-3D but not
  3D-family-vs-3D-family) but is not necessary to prove the specific fix this file documents.

## Positive Findings

- A genuinely well-designed "last write wins" discriminator: using two full-screen opaque
  passes with depth testing explicitly disabled removes every confound (blending, depth, partial
  coverage) except draw order itself.
- The header comment's central technical claim (`drawOrder_` + `RenderQueuedDraws()` real
  chronological replay) was independently verified against current production source and git
  history, not just trusted — confirmed accurate and current, not stale.
- Correctly exploits this backend's real, working `RenderTarget2D::GetData()` readback path
  instead of settling for an exception-absence check, unlike several sibling files in this same
  batch.

## Final Assessment

A strong, accurate regression test with an accurate, independently-verified header comment. No
defects found in this file. Its readback-based technique is a good model the other multi-frame
`SdlGpu` example tests in this batch (`sdlgpu_3d_test.cpp`, `sdlgpu_effects_test.cpp`) could adopt
to strengthen their own currently exception-only checks.
