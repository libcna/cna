# Audit: examples/sdlgpu_effects_test.cpp

## Metadata

- Source file: `examples/sdlgpu_effects_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `AlphaTestEffect`/`DualTextureEffect` smoke test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_effects …)` /
  `cna_register_backend_test(NAME SdlGpu_Effects …)`, `cmake/Tests/SdlGpuTests.cmake:36-38`,
  `TIMEOUT 60`).
- XNA/FNA relevance: direct — `AlphaTestEffect` (`AlphaFunction`, `ReferenceAlpha`),
  `DualTextureEffect` (`Texture`, `Texture2`).
- FNA reference: `HLSL/AlphaTestEffect.fx` (`clip((color.a < AlphaTest.x) ? AlphaTest.z :
  AlphaTest.w)` family), `HLSL/DualTextureEffect.fx` (`PSDualTexture`:
  `color.rgb *= 2; color *= overlay * pin.Diffuse;`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`,
  `DualTextureEffect.cpp`, `src/CNA/Internal/Backends/SdlGpu/shaders/{alpha_test3d,
  dual_texture3d}.frag.glsl`.

## Purpose

Three-check proof that `AlphaTestEffect` and `DualTextureEffect` work through this backend's real
public API: (A) `AlphaTestEffect` with `AlphaFunction=Greater`/`ReferenceAlpha=128` on a texture
whose top half is opaque and bottom half fully transparent — draws with no exception, and per the
file's own (manually screenshot-verified) claim, the bottom half is genuinely discarded
(`CornflowerBlue` background shows through) rather than alpha-blended translucent; (B)
`DualTextureEffect` multiplying a quadrant texture by a uniform yellow second texture — draws with
no exception, and per the real `tex1.rgb*=2; result=tex1*tex2*tint` formula the screenshot should
show the texture's blue quadrant turned black and its white quadrant turned yellow; (C) 120 frames
of both draws with no exception.

## Executive Verdict

**Mostly healthy** — both effect formulas were independently re-derived against FNA's real
`AlphaTestEffect.fx`/`DualTextureEffect.fx` HLSL and confirmed correct in this backend's own
`alpha_test3d.frag.glsl`/`dual_texture3d.frag.glsl`. As with `sdlgpu_3d_test.cpp` in this same
batch, the frame-1 checked path duplicates `DrawScene()`'s logic rather than calling it (F1), and
neither check's actual pixel content is asserted by the automated CTest — the discard-vs-blend and
channel-multiply claims rest entirely on a one-time manual screenshot (`plans/plan_sdlgpu.md`
`SDLGPU-31`/`SDLGPU-32`, 2026-07-15), same documented swapchain-readback limitation already
established in this batch's other reports.

## Checklist Results

### API / XNA / FNA parity
`AlphaTestEffect::setAlphaFunctionProperty(CompareFunction::Greater)`/
`setReferenceAlphaProperty(128)` and `DualTextureEffect::setTextureProperty`/
`setTexture2Property` match FNA's real property surface exactly.

### Behavioral correctness
Re-derived both formulas by hand against FNA:
- **AlphaTestEffect**: FNA's `PSAlphaTestGreater`/`PSAlphaTestGreaterEqual` etc. all reduce to
  `clip((color.a < AlphaTest.x) ? AlphaTest.z : AlphaTest.w)`, where `clip()` discards when its
  argument is negative. `AlphaTestEffect.cpp`'s `Greater` case (confirmed at lines 265-267) sets
  `alphaTest.X = reference+threshold; Z=-1; W=1`. `alpha_test3d.frag.glsl` (confirmed lines 25-31)
  implements the identical encoding: `passTest = alpha < pc.alphaRef` (using the `X`-slot
  threshold-adjusted reference), `w = passTest ? alphaPassW : alphaFailW`, `discard` when `w<0`.
  With `Greater`'s `Z=-1`(pass-weight)/`W=1`(fail-weight) mapped through this shader's
  `pc.alphaPassW`/`pc.alphaFailW` fields, a pixel whose alpha is *not* less than the (shifted)
  reference — i.e., alpha > 128 threshold, matching `CompareFunction::Greater`'s real semantics —
  takes the `alphaFailW=+1` (keep) branch, and an alpha below it takes `alphaPassW=-1` (discard).
  This is correct: verified the sign/threshold encoding is self-consistent and matches the
  intended "keep alpha>128, discard alpha<=~128" behavior the test's texture (opaque top half,
  zero-alpha bottom half) is designed to distinguish.
- **DualTextureEffect**: FNA's `PSDualTexture`: `color.rgb *= 2; color *= overlay * pin.Diffuse;`
  — confirmed byte-for-byte equivalent to `dual_texture3d.frag.glsl`'s `tex1.rgb *= 2.0; outColor
  = tex1 * tex2 * fragTint;` (this file's `fragTint` is FNA's `pin.Diffuse`). With the test's own
  quadrant texture (white quadrant = `(255,255,255,255)`) times a uniform yellow second texture
  (`(255,255,0,255)`): `tex1.rgb*2=(2,2,2)` (clamped to `(1,1,1)` in the `[0,1]` shader domain) ×
  `tex2.rgb=(1,1,0)` = `(1,1,0)` = yellow — matches the header comment's predicted result exactly.
  The blue quadrant (`(0,0,255)`) times yellow (`(1,1,0)`) → `(0,0,0)` = black — also matches.
- Neither of these independently-confirmed-correct formulas is actually asserted by this file's
  own CTest, which only checks exception-absence — see F1 (test-coverage gap, not a production
  defect; the underlying shader math is verified correct by this audit).

### Logic
Same duplication pattern as `sdlgpu_3d_test.cpp`: the frame-1 branch (lines 174-216) re-implements
`DrawScene()`'s (lines 140-166) two draw calls verbatim rather than calling it — see F1.

### C++ correctness
No lifetime issues; `alphaTestTexture_`/`quadrantTexture_`/`yellowTexture_`/`quadVb1_`/`quadVb2_`
follow the same `unique_ptr`-in-`LoadContent()` pattern as every other file in this shard.
`dev.SetVertexBuffer(nullptr)` at the end of both `DrawScene()` and the frame-1 branch correctly
clears the binding.

### Robustness
The `try`/`catch` with a `stage` string (lines 176-211) gives useful per-effect failure
diagnostics, matching `sdlgpu_3d_test.cpp`'s identical pattern.

### Testing
Both stock-effect formulas are exercised through the real public API and, per this audit's
independent re-derivation, are computed correctly by this backend's shaders. The gap is entirely
in what the *automated* CTest observes (exception-absence only) versus what a human reviewer once
confirmed by eye (F1) — not in the underlying implementation.

## Detailed Findings

### F1 — Frame-1 scene is a hand-duplicated copy of `DrawScene()`; automated CTest verifies neither effect's actual discard/multiply output

- Severity: MEDIUM
- Confidence: HIGH (direct comparison of the two code blocks; direct grep confirms no
  `GetBackBufferData`/`RenderTarget2D::GetData` call anywhere in this file)
- Category: maintainability / test-coverage
- Location/symbol: `Draw()`'s frame-1 branch (lines 174-216) vs. `DrawScene()` (lines 140-166);
  absence of any pixel-readback call in the file
- Evidence: `DrawScene()` constructs `AlphaTestEffect alphaFx`/`DualTextureEffect dualFx` with a
  fixed sequence of property setters and `DrawPrimitives` calls; the frame-1 branch (lines
  180-204) constructs separately-scoped, identically-named local variables with the identical
  setter sequence, differing only in the interleaved `check(true, …)` calls and `stage` tracking.
  Per the header comment (lines 6-14), the actual proof that check A's discard is genuine (not
  alpha-blended) and check B's channel math is genuine (not "one texture shown") is a screenshot
  taken once at `plans/plan_sdlgpu.md SDLGPU-31`/`SDLGPU-32` (2026-07-15) — this audit's own
  independent formula re-derivation (above) confirms that screenshot's claims *should* have been
  true at that time, but nothing in this file or its CTest registration re-confirms it now.
- Why it matters: identical to `sdlgpu_3d_test.cpp`'s F1 — a future edit to `DrawScene()` alone
  (the function that actually renders every frame after frame 1) that regresses either effect's
  setup would not be caught by the frame-1 checks, which observe only the separately-maintained
  copy. Additionally, since neither check reads back a pixel, even a regression that changed the
  *rendered result* of either effect (e.g., an `AlphaFunction` mapping bug, or `DualTextureEffect`'s
  channel-multiply order swapped) would still report 3/3 PASS as long as no exception was thrown —
  this backend has a proven, working `RenderTarget2D::GetData()` readback path (used successfully
  by `sdlgpu_draworder_test.cpp` and `sdlgpu_envmap_test.cpp` in this same batch) that this file
  does not use, despite both quads here being drawn to fixed, non-overlapping screen locations
  well-suited to a centre-pixel readback per effect.
- FNA/XNA comparison: N/A for the duplication half (test-authoring risk); for the missing-readback
  half, this audit independently confirmed both effects' shader math is currently FNA-correct, so
  there is no live production bug being masked here — purely a coverage gap.
- Related files: `examples/sdlgpu_3d_test.cpp` (identical duplication pattern, see its own F1);
  `examples/sdlgpu_draworder_test.cpp`/`sdlgpu_envmap_test.cpp` (this batch's proof that
  `RenderTarget2D::GetData()` readback already works on this exact backend).
- Suggested future action (not implemented by this audit): factor the two draw stages into a
  shared helper called from both `DrawScene()` and the frame-1 branch (as suggested for
  `sdlgpu_3d_test.cpp`); separately, consider adding a `RenderTarget2D`-based readback variant of
  checks A/B (e.g., sample a top-half and bottom-half pixel for check A, a blue-quadrant and
  white-quadrant pixel for check B) to convert the screenshot-only verification into a permanent,
  CI-enforced regression guard.

## Cross-File Observations

- Shares the `MakeAlphaTestTexturePixels()`/`MakeQuadrantTexturePixels()` pattern (the latter
  copy-pasted identically to `sdlgpu_2d_test.cpp`/`sdlgpu_3d_test.cpp`) — see those files' own
  Cross-File Observations.
- This file's `AlphaTestEffect`/`DualTextureEffect` formulas were cross-checked directly against
  FNA's actual `HLSL/AlphaTestEffect.fx`/`HLSL/DualTextureEffect.fx` source in this pass (not
  merely trusted from the header comment or `plans/plan_sdlgpu.md`'s own claim) and confirmed correct —
  this strengthens confidence in the underlying `alpha_test3d.frag.glsl`/`dual_texture3d.frag.glsl`
  shaders beyond what a prior audit pass might have taken on faith.
- Same duplication shape as `sdlgpu_3d_test.cpp`'s F1 — now confirmed as a repeated pattern across
  at least two files in this shard, worth flagging if a `AUDIT_CROSS_CUTTING_FINDINGS.md`-style
  synthesis pass revisits "duplicated draw-scene logic between frame-1 checks and steady-state
  render" as a recurring shape in the `SdlGpu` example-test family.

## Missing or Weak Tests

- No pixel-level assertion for either check (F1) despite this backend's proven working
  `RenderTarget2D::GetData()` readback mechanism being available and already used elsewhere in
  this exact shard.
- No boundary case for `AlphaFunction` (e.g., `Equal`/`NotEqual`, which use a different
  `alphaTest.Y` threshold-tolerance slot than `Greater`/`Less`) — acceptable for a smoke test,
  but a gap relative to full `CompareFunction` enum coverage.

## Positive Findings

- Both effect formulas independently re-derived and confirmed correct against real FNA HLSL
  source — no live production defect found in either `alpha_test3d.frag.glsl` or
  `dual_texture3d.frag.glsl` for the scenarios this file exercises.
- The `stage`-tracking `try`/`catch` pattern gives genuinely useful per-effect failure diagnostics.
- Correctly exercises two structurally distinct stock effects (discard-based vs.
  multiply-based) in one coherent, minimal scene.

## Final Assessment

The underlying effect math this file exercises is correct, independently re-verified against FNA.
The file's own weaknesses are structural (F1: duplicated draw logic, and a missed opportunity to
use this backend's own proven pixel-readback mechanism to convert a one-time manual screenshot
claim into a permanent automated guard) rather than a live correctness defect.
