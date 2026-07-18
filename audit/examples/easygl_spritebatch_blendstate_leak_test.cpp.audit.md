# Audit: examples/easygl_spritebatch_blendstate_leak_test.cpp

## Metadata

- Source file: `examples/easygl_spritebatch_blendstate_leak_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL SpriteBatch/GraphicsDevice blend-state regression test
- File type: single-frame `Game`-subclass pixel-readback executable (hand-rolled `main()`, not the
  `PixelTestGame` helper)
- XNA/FNA relevance: exercises `SpriteBatch::Begin(SpriteSortMode, BlendState)`,
  `GraphicsDevice::BlendState`'s "lasting side effect" contract, and `BasicEffect` — all
  `Microsoft::Xna`-facing behavior, judged against FNA's `SpriteBatch.cs`/`GraphicsDevice.cs`.
- Build/registration: `cmake/Tests/EasyGLTests.cmake` — `cna_test_easygl_spritebatch_blendstate_leak`,
  CTest name `EasyGL_SpriteBatch_BlendStateLeak` (Task 956 comment block confirms it is wired in, not orphaned).
- Main related production files: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`Begin()`),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`EasyGLSpriteBatchBackend::Begin()`,
  `EasyGLGraphicsBackend::ApplyBlendState`), `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`setBlendStateProperty`), `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp` (`BlendState::Additive`).

## Purpose

Regression test for "Task 956": before the fix, `EasyGLSpriteBatchBackend::Begin()` unconditionally
hardcoded a `SrcAlpha`/`OneMinusSrcAlpha` GL blend function on every `SpriteBatch::Begin()` call,
silently overriding whatever `BlendState` the caller actually requested and leaving that hardcoded
value on the real GL state machine after `End()`. The test proves two things at once: (1) a
`SpriteBatch::Begin(..., BlendState::Additive)` call genuinely reaches `GraphicsDevice.BlendState`
as a lasting side effect (matching FNA, which does not restore prior blend state on `End()`), and
(2) a subsequent, unrelated 3D `BasicEffect` draw that never touches `BlendState` itself still renders
with that Additive state, not a leftover hardcoded one.

## Executive Verdict

**Healthy.** The test's math, its choice of discriminating channel, and its claimed bug shape all check
out against the actual (now-fixed) production code; it is a genuine regression test, not a compile-and-hope
smoke test.

## Checklist Results

### API / XNA / FNA parity
Exercises `SpriteBatch::Begin(SpriteSortMode, BlendState)` (the 2-arg overload,
`SpriteBatch.cpp` lines 54-58) and `BlendState::Additive` (`BlendState.cpp` line 6:
`ColorSourceBlend=SourceAlpha, AlphaSourceBlend=SourceAlpha, ColorDestinationBlend=One,
AlphaDestinationBlend=One`) — confirmed to match FNA's own preset exactly
(`FNA/src/Graphics/States/BlendState.cs` lines 164-170: `Blend.SourceAlpha, Blend.SourceAlpha,
Blend.One, Blend.One`). `BasicEffect.VertexColorEnabled`/`Apply()` used only to drive a 3D draw;
confirmed by inspection that `BasicEffect.cpp` never itself calls `setBlendStateProperty` (grep found
zero blend-state references), so the test's premise that "a following 3D draw inherits whatever
BlendState is current" is not accidentally propped up by the effect itself resetting it.

### Behavioral correctness
Traced the full call chain: `SpriteBatch::Begin(sortMode, blendState)` → the 7-arg overload
(`SpriteBatch.cpp` line 94) calls `graphicsDevice_->setBlendStateProperty(blendState)`
unconditionally → `GraphicsDevice::setBlendStateProperty` (`GraphicsDevice.cpp` lines 1667-1682)
calls `backend_->ApplyBlendState(...)` with the state's raw `Blend`/`BlendFunction` ordinals →
`EasyGLGraphicsBackend::ApplyBlendState` (line 1904) computes `blendEnabled` and, since Additive's
`colorSrcBlend=4` (`SourceAlpha`) is not the `Opaque`-preset special case (`0/1` for src/dst), calls
`device.set_blend_func_separate(SrcAlpha, One, SrcAlpha, One)`. Confirmed
`EasyGLSpriteBatchBackend::Begin()` (line 1034) now does nothing but `begun = true;` — the fix is
genuinely in place, matching the file's own Task 956 comment. `grep`-verified `ApplyBlendState` has
exactly one call site outside every backend's own definition (`GraphicsDevice.cpp` line 1671), so
nothing else in the frame silently re-applies a different blend state between the SpriteBatch draw
and the 3D draw.

### Logic
Expected-math derivation in the header comment (lines 23-28) is internally consistent and was
independently re-derived here: with Additive genuinely active and the quad's color alpha=255 (i.e.
alpha factor 1.0), `SourceAlpha` collapses numerically to the same result as `One` would — so the
comment's simplified "colorSrc=colorDst=alphaSrc=alphaDst=One" (line 24) is an approximation that
happens to be numerically exact for this specific opaque draw, not a literally accurate restatement
of `BlendState::Additive`'s real factors (`SourceAlpha`/`One`). This is a harmless simplification, not
a test bug — flagged as F1 below purely as a documentation-precision nit.
G-channel-only assertion (line 121: `got.getGProperty() >= 80 && <= 120`) is a deliberately chosen
"clean discriminator": under the old bug (hardcoded `SrcAlpha`/`OneMinusSrcAlpha`, quad alpha=255 →
dst factor `1-1=0`), the gray background's G=100 is multiplied by 0 and dropped to G≈0; under the
real fix (Additive, dst factor `One`), G=100 survives additively. R would saturate to 255 either way
(200+100 or 200 alone, both clamp near/at 255) and B behaves identically to G — so G alone is a
sufficient, well-chosen witness; the test doesn't need to (and correctly doesn't) assert on R/B.

### Memory/resource lifetime
`gdm_`, `sb_`, `cornerTex_` are `unique_ptr` members constructed in `Initialize()`/the constructor and
never manually freed — normal RAII, consistent with every other example in this shard. No leak/UAF
concerns: single-frame, single-process lifetime.

### C++ correctness
`const VertexPositionColor verts[6]` is a plain aggregate array, contiguous positions passed via
raw pointer decay into `DrawUserPrimitives(..., verts, 0, 2)` — `primitiveCount=2` matches 6 vertices
÷ 3 for `TriangleList` (correctly the *primitive* count, not the *vertex* count — this project has a
documented history of test authors passing vertex count by mistake; this file gets it right).
`Rectangle reg(vp.getWidthProperty() / 2, ...)` with `kSize=64` evaluates to `(32,32,1,1)`, safely away
from the corner sprite's `(0,0,4,4)` footprint — no overlap risk between the two draws' regions.

### Performance
N/A — single-frame test, not a hot-path concern.

### Thread safety
N/A — single-threaded example, matches every sibling test in this shard.

### Architecture
Correctly avoids the separate, already-known, unrelated Task 933 finding ("a full-backbuffer
SpriteBatch draw before any 3D draw breaks that frame's 3D rendering") by deliberately keeping the
sprite draw to a small 4×4 corner region (lines 8-12 explicitly call this out) — good test hygiene,
isolating the one behavior under test from a second, already-tracked defect.

### Maintainability
Comment block (lines 1-30) is unusually thorough for a test file: states the bug being regression-
tested, the deliberate scope-avoidance of Task 933, the exact expected math, and why G is the
discriminating channel. This is exactly the kind of self-documenting test file the project's
`known_bugs.md`/audit process benefits from.

### Portability
N/A — single backend (EasyGL), no platform-conditional code.

### Robustness
Not applicable in the "input validation" sense (this is a self-contained example, not a library
entry point) — the one thing worth noting is that if `Draw()` were somehow never invoked, `result_`
would default to `1` (fail-safe), unlike several sibling files in this shard which default `result_`
to `0` (see the sourcerect/scale/rotation/layerdepth/spritefont audits) — this file's default is the
more defensive of the two patterns.

### Testing
This file *is* a test; see Cross-File Observations for what it does/doesn't additionally prove.

## Detailed Findings

### F1 — Header comment states Additive's factors imprecisely (documentation-only, not a logic bug)

- Severity: LOW
- Confidence: HIGH
- Category: documentation-accuracy
- Location/symbol: file header comment, line 24 ("with Additive ... genuinely still active: result =
  red(200,0,0) + gray(100,100,100)")
- Evidence: `BlendState::Additive` is actually `ColorSourceBlend=SourceAlpha, ColorDestinationBlend=One`
  (confirmed against both `BlendState.cpp` line 6 and FNA's `BlendState.cs` lines 164-170), not
  "colorSrc=One" as the comment's simplified restatement implies. The simplification is numerically
  correct only because the quad's alpha is 255 (factor 1.0), which collapses `SourceAlpha` to the same
  value `One` would give.
- Why it matters: purely cosmetic — a future reader modifying this test to use a non-opaque quad color
  would get a materially different (and, per the comment as currently worded, surprising) blend result,
  since the comment doesn't mention the alpha-collapse dependency.
- FNA/XNA comparison: FNA's own `BlendState.Additive` preset (`SpriteBatch.cs`'s sibling file
  `BlendState.cs` lines 164-170) is `SourceAlpha`/`SourceAlpha`/`One`/`One` — matches CNA's value; the
  test comment's simplification is the only imprecise part.
- Suggested future action (not implemented by this audit): reword the comment to note the factors are
  `SourceAlpha`/`One`, simplified to `One`/`One` only because this draw's alpha is 255.

## Cross-File Observations

- This is one of several files in this shard (also `layerdepth`, `rotation`, `rotation_golden`,
  `scale`, `sourcerect`) that must pass a `SamplerState*` (non-`const`) to `SpriteBatch::Begin`
  — this file avoids that specific pattern (it uses the 2-arg `Begin(sortMode, blendState)` overload,
  which supplies `nullptr` for the sampler), so the `const_cast<SamplerState*>(&SamplerState::PointClamp)`
  wart noted in the sibling reports doesn't appear here.
- The fix this test guards (`EasyGLSpriteBatchBackend::Begin()` no longer clobbering GL blend state)
  is explicitly cross-referenced in the source comment to an equivalent, earlier SDL_Renderer fix
  (Task 695, `docs/sdl-renderer-2d-completeness.md`) — worth checking, in a future backend-comparison
  pass, whether every other 2D-capable backend (Bgfx, Vulkan, WebGPU, SdlGpu, Canvas, Software, Ascii,
  Dx3) has an equivalent regression test, since this exact bug shape ("SpriteBatch hardcodes blend
  state instead of deferring to `GraphicsDevice.BlendState`") is generic enough to recur per backend.

## Missing or Weak Tests

- Only the G channel is asserted; while well-justified (see Logic section), a second assertion at a
  *different* corner using a distinguishable non-additive `BlendState` (e.g. `Opaque`) immediately after
  the Additive one would additionally prove the leak-free behavior generalizes across more than one
  state transition, rather than just "Additive persists once."

## Positive Findings

- Genuine, well-reasoned regression test: independently re-derived math (own analysis above) matches
  the file's own comment and the current (fixed) production code path across
  `SpriteBatch.cpp` → `GraphicsDevice.cpp` → `EasyGLGraphicsBackend.cpp`.
- Deliberately scopes around a second, unrelated known issue (Task 933) instead of conflating two bugs
  in one test — good test isolation discipline.
- Chooses a single, well-justified discriminating channel (G) rather than an over-broad, potentially
  flaky full-color match.

## Final Assessment

A tight, evidence-backed regression test that accurately targets the fixed Task 956 defect; its own
internal math checks out against the real blend-state pipeline. The only finding is a cosmetic
comment imprecision (F1), not a functional issue.
