# Audit: examples/sdlgpu_2d_test.cpp

## Metadata

- Source file: `examples/sdlgpu_2d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — SDL_GPU backend 2D vertical-slice smoke test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_2d …)` / `cna_register_backend_test(NAME SdlGpu_2D …)`,
  `cmake/Tests/SdlGpuTests.cmake:23-25`, `TIMEOUT 60`).
- XNA/FNA relevance: direct — `Texture2D::CreateFromPixels`, `SpriteBatch::Draw` overloads
  (position/tint, tint+alpha, rotation+origin, `SpriteEffects` flip flags), `SamplerState`
  presets (`PointWrap`, `LinearWrap`) passed to `SpriteBatch::Begin`.
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (sprite pipeline / `RenderSprites`), `src/CNA/Internal/Backends/SdlGpu/shaders/sprite2d.vert.glsl`
  / `sprite2d.frag.glsl`, `plans/plan_sdlgpu.md` Phase `SDLGPU-13..25`.

## Purpose

Three-check smoke test proving the SDL_GPU backend's 2D vertical slice is real, not stubbed:
(A) `Texture2D::CreateFromPixels()` uploads a 4×4 per-quadrant-colored texture and reports the
right dimensions; (B) a single frame issues four separate `Begin`/`End` blocks covering plain
tint/alpha, 45°-rotation with a custom origin, combined horizontal+vertical flip, and two
non-default `SamplerState` presets (`PointWrap`, `LinearWrap`) with UVs deliberately extending past
`[0,1]` to exercise address-mode wrapping — all with no exception; (C) the same scene renders for
120 consecutive frames with no exception. File placement (`examples/`, backend-named, `SdlGpu`
namespace `using`) is correct for this shard.

## Executive Verdict

**Mostly healthy** — the three checks it makes are real and correctly derived, but by the file's
own header comment this is deliberately a "didn't throw" test, not a pixel test: rotation math,
the flip permutation, and sampler address-mode wrapping are each real, exercised code paths, but
none of their *output* is asserted by the automated `SdlGpu_2D` CTest itself (see F1). The
one-time manual screenshot verification this file's header cites is dated (`plans/plan_sdlgpu.md`
`SDLGPU-25`, 2026-07-15) and is not re-run by CI.

## Checklist Results

### Purpose
Correctly placed and scoped; matches `plans/plan_sdlgpu.md SDLGPU-22..25`'s stated goal verbatim.

### API / XNA / FNA parity
`SpriteBatch::Draw(Texture2D&, Rectangle, Rectangle, Color)`,
`Draw(…, Color, float rotation, Vector2 origin, SpriteEffects, float layerDepth)`, and
`Begin(SpriteSortMode, BlendState*, SamplerState*, DepthStencilState*, RasterizerState*)` are all
used with the correct FNA signatures/defaults (`Begin()` with no arguments uses `Deferred` +
`AlphaBlend` + `LinearClamp`, matching `SpriteBatch.Begin()`'s real XNA default overload).
`SpriteEffects::FlipHorizontally | FlipVertically` is combined via explicit `static_cast<int>`
OR, which is correct C++ practice for a C# `[Flags]` enum with no native CNA `operator|` defined
for `SpriteEffects` — confirmed there is no such operator by grep of `SpriteEffects.hpp`, so this
is not working around a missing feature incorrectly, just verbosely.

### Behavioral correctness
Check A validates `Texture2D::CreateFromPixels()`'s width/height getters only, not the pixel
content itself — acceptable for this file since content correctness is exercised visually by the
rest of the scene, but it does mean a byte-order bug (e.g. R/B channel swap) in
`CreateFromPixels()` would not be caught here even if it silently rendered a differently-colored
(but still valid) scene. Checks B/C are pure exception-absence checks — see F1.

### Logic
`DrawScene()` (lines 109-143) is called from both the frame-1 branch (wrapped in `try`/`catch`)
and the `else` branch for frames 2-120 unconditionally — unlike `sdlgpu_3d_test.cpp` and
`sdlgpu_effects_test.cpp` (see their own reports), this file does **not** duplicate the draw
logic inline for frame 1; it correctly reuses the same `DrawScene()` function in both branches.
This is the cleaner pattern of the three multi-frame `SdlGpu` example tests reviewed in this
batch — worth noting as a positive contrast (see Positive Findings).

### C++ correctness
No lifetime issues: `texture_`/`spriteBatch_` are constructed in `LoadContent()` (called by the
`Game` base before the first `Draw()`), held by `unique_ptr`, destroyed in declaration order at
game teardown after `Run()` returns. `MakeQuadrantTexturePixels()`'s `setPixel` lambda captures
`pixels` by reference and is only invoked synchronously within the same function scope — safe.

### Robustness
`Texture2D::CreateFromPixels()` failure is caught and reported via `check(false, …)` rather than
propagating an uncaught exception that would abort the whole test binary — good defensive style,
consistent with this shard's convention.

### Performance
Not a concern for a smoke test; 120 frames of a small fixed scene is a deliberate, bounded loop
(`kTotalFrames`), not a performance-sensitive path.

### Testing
Covers: texture upload, four independent `Begin`/`End` cycles per frame (verifying `SpriteBatch`
supports being started/ended multiple times within one `Draw()` call, relevant to the
project's own documented "multiple Begin/End" `known_bugs.md` concern — though this file's
sequential `Begin`→`End`→`Begin`→`End` pattern does not itself exercise the *overlapping*
Begin-without-End case that bug report describes), rotation+origin, combined flip flags, and two
non-default sampler presets. See F1 for what is *not* covered (actual rendered output).

## Detailed Findings

### F1 — Automated CTest asserts only "no exception," not the actual rendered pixel content; the real correctness proof is a one-time, non-repeatable manual screenshot

- Severity: LOW
- Confidence: HIGH (the file's own three `check()` calls are visibly limited to `bool` conditions
  derived from texture dimensions and caught-exception flags; no `GetBackBufferData`/
  `RenderTarget2D::GetData` call appears anywhere in this file)
- Category: test-coverage
- Location/symbol: `Draw()` (lines 145-170), `DrawScene()` (lines 109-143)
- Evidence: the file's own header comment (lines 8-12) states plainly: *"No pixel-level readback
  exists yet on this backend (`ReadBackbuffer` is not implemented … so this test cannot assert
  exact pixel colors …). It proves: real texture upload, a real multi-sprite scene … drawn over
  many frames with no crash."* Cross-referenced against `plans/plan_sdlgpu.md`'s `SDLGPU-39` row: this
  is confirmed to be a **permanent, documented SDL_gpu API contract**, not a stub — the swapchain
  texture is write-only per `SDL_gpu.h`'s own doc comment on
  `SDL_WaitAndAcquireGPUSwapchainTexture`, so `ReadBackbuffer`/`GetBackBufferData` genuinely cannot
  be implemented for this backend's swapchain without a per-frame proxy-texture redesign the
  project explicitly chose not to pay for (`SDLGPU-39`, "Deliberately not implemented 2026-07-16").
  `plans/plan_sdlgpu.md`'s `SDLGPU-25` row states the *actual* proof of correctness was a one-time
  manual screenshot taken 2026-07-15 (`import -window`), which "caught and led to fixing
  `SDLGPU-14`'s Y-flip bug."
- Why it matters: this is a real, structurally-explained limitation (verified against a genuine
  SDL_gpu API constraint, not a stale excuse), but it means a future regression in rotation math,
  the `SpriteEffects` flip-to-UV-swap mapping, or `SamplerState` address-mode wiring — any of
  which would still leave every draw call throwing zero exceptions — would pass this CTest
  silently. The correctness evidence for those code paths rests entirely on a screenshot from one
  specific commit, not re-verified by anything CI re-runs. This is not a defect *in this file*; it
  is a real, disclosed gap in this backend's automated coverage that this file cannot fix on its
  own (it has no readback mechanism to reach for).
- FNA/XNA comparison: N/A — testing-infrastructure limitation, not a behavior deviation from FNA.
- Related files: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp` (the swapchain
  write-only constraint), `examples/sdlgpu_diag_single_sprite.cpp` (this shard's other,
  non-CTest, visually-inspected diagnostic for the same underlying UV/sampling question).
- Suggested future action (not implemented by this audit): none better available today without
  the proxy-texture redesign `SDLGPU-39` already declined; worth flagging if that redesign is ever
  revisited, since it would let this specific file gain real per-check pixel assertions instead of
  exception-absence checks.

## Cross-File Observations

- Shares its `MakeQuadrantTexturePixels()` helper's exact pixel layout (4×4, four solid
  2×2 quadrants: red/green/blue/white) with `sdlgpu_3d_test.cpp` and
  `sdlgpu_effects_test.cpp` — each file re-implements it independently (copy-pasted, not a shared
  header) rather than factoring it into `examples/common/`. Low-severity duplication, consistent
  with this project's own stated policy in `common/PixelTestGame.hpp`'s header comment of not
  retrofitting existing example files into shared infrastructure.
- Correctly disables `SynchronizeWithVerticalRetrace` *before* `Game::DoInitialize()`'s
  `CreateDevice()` call reads it (constructor-time, matching the documented convention repeated
  verbatim across every multi-frame `SdlGpu` example test in this batch) — consistent and correct
  across all of them.
- `ProbeGpuDisplayAvailable()`/`kSkipExitCode` (from `common/PixelTestGame.hpp`) is used correctly
  as a headless-safe pre-flight check before constructing the real `Game`, matching every other
  file in this shard.

## Missing or Weak Tests

- No test in this file (or, per this batch, anywhere in the `SdlGpu_2D` CTest) verifies the actual
  color of an uploaded texture's individual texel, the exact rotated/flipped screen-space
  footprint of a sprite, or that `PointWrap`/`LinearWrap` samplers genuinely tile rather than
  clamp — all three are real, plausible regression targets that would not fail this CTest (F1).

## Positive Findings

- Unlike `sdlgpu_3d_test.cpp` and `sdlgpu_effects_test.cpp` in this same batch, this file does
  **not** duplicate its scene-drawing logic between the frame-1 exception-checked branch and the
  steady-state `else` branch — `DrawScene()` is a single shared function called from both,
  eliminating the drift risk those other two files carry.
- The file's own header comment is transparent about exactly what this test does and does not
  prove (no pixel readback, screenshot-verified once), which made independently confirming F1 a
  matter of cross-checking a already-documented, well-reasoned claim rather than discovering a
  hidden gap from scratch.
- `Texture2D::CreateFromPixels()` failure path is defensively caught rather than left to crash the
  whole test binary.

## Final Assessment

A clean, correctly-scoped smoke test that does exactly what its header comment says it does, no
more and no less. Its only real weakness — asserting exception-absence rather than actual pixel
output — is a genuine, disclosed, and currently-unfixable limitation of this specific backend's
swapchain API contract, not a defect introduced by this file, and this file's own header comment
already explains why. No correctness defect found in this file itself.
