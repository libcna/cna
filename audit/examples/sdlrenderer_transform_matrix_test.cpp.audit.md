# Audit: examples/sdlrenderer_transform_matrix_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_transform_matrix_test.cpp` (157 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch::Begin(...transformMatrix)` pixel
  integration test.
- CTest registration: `cna_sdl_test(cna_test_sdl_transform_matrix examples/sdlrenderer_transform_matrix_test.cpp)`
  / `cna_register_backend_test(NAME SDL_Renderer_TransformMatrix ...)` (`cmake/Tests/SdlRendererTests.cmake:90-92`).
- XNA/FNA relevance: direct — `SpriteBatch.Begin(SpriteSortMode, BlendState, SamplerState,
  DepthStencilState, RasterizerState, Effect, Matrix transformMatrix)` and two `Draw` overloads
  (`Draw(texture, Vector2, Color)` and the 8-parameter destRect/srcRect/rotation/origin/effects/
  layerDepth overload).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`pushSprite`/
  `flushSingle`, lines 143-183, 269-279), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::Draw`, the `SDL_RenderTextureAffine` transform path, lines 251-304).
- Task provenance (verified via `git log --all --oneline`): Task 675
  (`3af48166 fix(Task 675): implement transformMatrix support on SDL_Renderer`) introduced the
  affine-transform code path this test exercises; Task 671 (`4e7106fa`) fixed the rotation-pivot
  math the same code path also uses; Task 705 (`e6876f5d`) fixed an unrelated unchecked-downcast
  UB bug in the same `Draw` overloads. Task 915 (`1e047fa0`) implemented `ReadBackbuffer`, which
  this test's own header comment correctly cites as the reason it must use
  `PresentationMode::NativeBackBuffer`.

## Purpose

Pixel-integration test proving that `SpriteBatch::Begin(...)`'s `transformMatrix` parameter is
genuinely applied by the SDL_Renderer backend, using two sprites: (1) a 1x1 red texture drawn at
`(0,0)` under `Matrix::CreateTranslation(100,50,0)`, verified to render at `(100,50)` and *not*
at `(0,0)`; (2) a 2x1 `[Red|Blue]` texture drawn into `destRect(0,100,40,20)` with
`SpriteEffects::FlipHorizontally` under the same translation, verified at two points chosen to
disambiguate the interaction between the flip-corner-permutation logic and the affine transform
(not just a plain translated flip in isolation).

## Executive Verdict

**Healthy.** This audit independently re-derived the screen-space geometry for both sprites by
hand-tracing `SdlSpriteBatchBackend::Draw`'s affine-transform branch
(`SdlGraphicsBackend.cpp` lines 266-298) and confirms both checks assert the mathematically
correct outcome for the current code, not merely a plausible-looking one.

## Checklist Results

### Purpose
Correctly placed under `examples/`, named/registered per the shard's `sdlrenderer_*_test.cpp`
convention; single-responsibility (one feature: `transformMatrix`), matching its filename.

### API / XNA / FNA parity
`Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState, effect,
transformMatrix)` (line 99-102) is FNA's full 7-argument `SpriteBatch.Begin` overload, called with
real `SamplerState::PointClamp`, `nullptr` depth/rasterizer state, `nullptr` effect, and a real
transform — an appropriate, non-trivial exercise of the full signature. `Draw(texture, Vector2,
Color)` (line 105) and the 8-parameter destRect/srcRect/color/rotation/origin/effects/layerDepth
overload (line 111-112) both match `SpriteBatch.hpp`'s declared signatures exactly (cross-checked
against `include/.../SpriteBatch.hpp` lines 211 and 289-296).

### Behavioral correctness
Traced `SpriteBatch::Draw(texture, Vector2, Color)` (`SpriteBatch.cpp` lines 269-279) and confirmed
it converts to the same canonical `pushSprite(...)` → `flushSingle` → `backend_->Draw(fullSignature)`
path as the 8-parameter overload (not the simpler `Draw(texture,x,y)` 2-parameter backend method at
`SdlGraphicsBackend.cpp` lines 124-141, which has **no transform-matrix handling at all** and would
silently ignore `transformMatrix` if it were reached) — so the first sprite's translation genuinely
does flow through the affine-transform-aware code path this test intends to exercise, not a
different, transform-blind overload.

Re-derived sprite 1's expected pixels: with `rotation=0`, `origin=Vector2::Zero`,
`transformMatrix=CreateTranslation(100,50,0)`, `SdlGraphicsBackend.cpp`'s affine branch (lines
266-282) computes `topLeft = Transform(rotateAndPlace(0,0), tx) = (0,0)+(100,50) = (100,50)` — i.e.
the untransformed corner at `destinationRectangle.X/Y = (0,0)` maps to exactly `(100,50)` post-
transform, matching the check `(100,50) -> kRed` and `(0,0) -> kBlack` (original position now
vacated).

Re-derived sprite 2's expected pixels by hand-computing the full affine-corner math for
`destRect(0,100,40,20)`, `origin=(0,0)`, `rotation=0`, `SpriteEffects::FlipHorizontally`,
under the same `+(100,50)` translation:
- Unflipped corners after translation: `topLeft=(100,150)`, `topRight=(140,150)`,
  `bottomLeft=(100,170)`, `bottomRight=(140,170)`.
- With `flipH=true, flipV=false`, the corner-permutation logic (lines 286-288) sets
  `originCorner=topRight=(140,150)`, `rightCorner=topLeft=(100,150)`,
  `downCorner=bottomRight=(140,170)`.
- `SDL_RenderTextureAffine`'s contract maps source-x=0 (the *Red* half of the 2x1 `[Red|Blue]`
  texture) to `originCorner=(140,150)` and source-x=`src.w`=2 (end of the *Blue* half) to
  `rightCorner=(100,150)`; interpolating, the source midpoint (`sx=1`, the Red/Blue boundary) maps
  to screen `x=120` — exactly the midpoint of the translated dest rect `[100,140)`.
- Consequently the source's Red half (`sx∈[0,1)`) maps to screen `x∈[120,140)` (the **right**
  half of the destination) and the Blue half (`sx∈[1,2)`) maps to `x∈[100,120)` (the **left**
  half) — i.e. flipped, as intended.
- This exactly matches the test's own checks: `(110,160) -> kBlue` (110 is in `[100,120)`,
  the left half) and `(130,160) -> kRed` (130 is in `[120,140)`, the right half).

This is not a coincidental pass: the assertions are the mathematically correct output of the
current, real affine-transform-plus-flip-permutation code, independently re-derived rather than
merely pattern-matched against the test's own comment.

### Logic
`colourMatch`'s per-channel tolerance (`tol=40`, line 51) is generous relative to the ~255-unit
separation between black/red/blue, but appropriate given `BlendState::Opaque` (no blending
uncertainty) and `SamplerState::PointClamp` (no filtering blur at the sampled points, which are
all ≥10px from any check region's own edge) — the tolerance exists purely as headroom for
platform/driver rounding, not because the expected values are uncertain.

### Memory/resource lifetime
`redTex_`/`hTex_`/`sb_`/`gdm_` are all `std::unique_ptr` members constructed once in
`Initialize()`, drawn once in the first `Draw()` call (guarded by `done_`), then the process exits
via `Exit()` — no reuse-after-free or double-destruction risk; ownership is straightforward and
matches the shard's established single-shot pixel-test idiom.

### C++ correctness
`Color(0,0,0,0)` sentinel + `GetBackBufferData(&reg, &got, 0, 1)` (line 126-128) is safe: `reg` is
always exactly 1x1 and in-bounds for the configured 400x200 backbuffer for all four check points.
No unsafe casts.

### Performance
N/A for a one-shot test — the two draws and four 1x1 readbacks are negligible.

### Thread safety
N/A — single-threaded `Game`/`Draw()` callback, consistent with every other file in this shard.

### Architecture
Correctly stays at the XNA-facing `SpriteBatch`/`Texture2D` API level; does not reach into backend
internals directly (verified indirectly by tracing the call chain from the production source, not
by the test itself touching backend types).

### Maintainability
157 lines, proportionate; the header comment's geometry walkthrough (lines 1-29) is precise enough
that this audit could verify it arithmetically without guessing intent — a good example of the
"self-documenting math" style also praised in the sibling EasyGL specular-test audit.

### Portability
N/A beyond what's already backend-specific (SDL_Renderer only); no platform-conditional code in
the test itself.

### Robustness
Not applicable in the "input validation" sense — this is a fixed-scenario pixel test, not an API
surface exercising malformed input.

### Testing
This is itself a test; see Missing or Weak Tests below for gaps in what it does *not* cover.

### Cross-file consistency
`Draw(*hTex_, Rectangle(...), Rectangle(0,0,2,1), Color::White, 0.0f, Vector2::Zero,
SpriteEffects::FlipHorizontally, 0.0f)` (lines 111-112) matches the `SpriteBatch::Draw` overload at
`SpriteBatch.hpp` line 289-296 (`destRect, optional<Rectangle> src, color, rotation, origin,
effect, layerDepth`) — `Rectangle(0,0,2,1)` implicitly converts to `std::optional<Rectangle>`,
confirmed by inspecting `SpriteBatch.cpp` lines 343-355 (the corresponding overload forwards the
rectangle unchanged into `pushSprite`).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings in this file. One LOW/INFO observation:

### F1 — The transform-matrix affine path is only reached because `SpriteBatch::Draw(texture,
Vector2, Color)` happens to route through the same canonical draw call as every other overload;
this is correct today but not obviously guaranteed by the API surface itself

- Severity: INFO
- Confidence: HIGH (traced directly)
- Category: architecture / test-design note, not a defect
- Location/symbol: `SpriteBatch::Draw(const Texture2D&, Vector2, Color)`
  (`SpriteBatch.cpp` lines 269-279) vs. the unused
  `SdlSpriteBatchBackend::Draw(const ITextureBackend&, float, float)` overload
  (`SdlGraphicsBackend.cpp` lines 124-141, which never applies `transformMatrix`)
- Evidence: every `SpriteBatch::Draw` overload in `SpriteBatch.cpp` (including the simplest ones)
  converges on `pushSprite(...)` → `flushSingle`/`flushBatch` → the single 8-parameter
  `backend_->Draw(...)` call; the plain-`(x,y)` backend overload appears to be dead code from
  `SpriteBatch`'s current call pattern (not reached by any code path this audit traced).
- Why it matters: nothing wrong today — this test's first sprite (`Draw(texture, Vector2, Color)`)
  does exercise the real transform path, confirmed by trace. Flagged only because if a future
  change re-introduced a fast-path `SpriteBatch::Draw(texture, x, y)` micro-optimization that called
  the transform-blind backend overload directly, this specific check (translation of a plain
  `Draw(texture, position, color)` call) would silently stop catching the regression, since its own
  assertion is at the pixel level (opaque to *why* the sprite moved) rather than asserting anything
  about which backend method was invoked.
- Related files: `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`'s
  `ISpriteBatchBackend::Draw` interface (declares the 2-parameter overload that appears unused).
- Suggested future action (not implemented by this audit): none required now; worth a one-line
  comment at the unused 2-parameter backend overload noting it currently has no caller and no
  transform support, so a future caller doesn't assume transform support "for free."

## Cross-File Observations

- The header comment's claim that `PresentationMode::NativeBackBuffer` is *required* (not just
  convenient) for pixel-exact readback is independently corroborated by the already-audited
  `SdlGraphicsBackend.cpp` report's own account of `ReadBackbuffer` (Task 915): it "throws rather
  than silently returning wrong pixels when the physical/logical size mismatch would make
  exact-pixel readback untrustworthy" under the default `FixedHeightDynamicWidth` presentation
  mode. This test's constructor (line 145) correctly opts out of that mismatch up front rather than
  discovering it via a runtime throw.
- This test and `sdlrenderer_vertexdeclaration_construction_test.cpp` /
  `sdlrenderer_viewport_project_unproject_test.cpp` all use the same `NativeBackBuffer` +
  `GetBackBufferData` idiom consistently — a positive sign of a shared, deliberate methodology
  across this shard rather than three independently-reinvented approaches.

## Missing or Weak Tests

- No check exercises `transformMatrix` combined with a non-zero `rotation` (this file always uses
  `rotation=0.0f` for both sprites) — the affine branch's `rotateAndPlace` lambda (`SdlGraphicsBackend.cpp`
  lines 268-276) has rotation-dependent trigonometry that is entirely untested by translation-only
  transforms; a rotation+transform combination is a genuinely different code path (the
  `cosR`/`sinR` terms are exercised at their `rotation=0` identity values — `cosR=1, sinR=0` — here,
  never at a non-trivial angle). This is a real gap in this specific file's own coverage (not
  necessarily the whole shard's, which this audit did not exhaustively search for a
  rotation+transform combination elsewhere).
- No check exercises `SpriteEffects::FlipVertically` or the combined
  `FlipHorizontally|FlipVertically` case under a transform (only `FlipHorizontally` is tested).

## Positive Findings

- The test's own header comment gives an exact, checkable geometric prediction before the checks
  run, and this audit's independent re-derivation confirms it precisely — including the
  less-obvious flip-plus-transform interaction for the second sprite, not just the trivial
  translation case for the first.
- Correctly identifies and works around a real backend constraint (`NativeBackBuffer` requirement
  for exact readback) rather than fighting it or getting a spurious failure.
- Positions the two sprites' check regions so they provably don't overlap (`y=100` pre-transform
  vs. the first sprite's `(0,0)`/`(100,50)`), a small but real test-design detail that prevents a
  false-positive from one sprite's pixels bleeding into the other's check.

## Final Assessment

A strong, correctly-designed pixel-integration test whose core claims were independently verified
against the actual current backend code, not just the test's own narrative. Only gaps are
additional untested parameter combinations (rotation+transform, vertical/combined flip+transform),
not any defect in what is actually tested.
