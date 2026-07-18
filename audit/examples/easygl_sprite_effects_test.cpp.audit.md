# Audit: examples/easygl_sprite_effects_test.cpp

## Metadata

- Source file: `examples/easygl_sprite_effects_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SpriteBatch.Draw(..., SpriteEffects, ...)` UV-flip test
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_sprite_effects …)` /
  `cna_register_backend_test(NAME EasyGL_SpriteEffects_Flip …)`,
  `cmake/Tests/EasyGLTests.cmake:773-776`).
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::SpriteEffects` (`None`,
  `FlipHorizontally`, `FlipVertically`) and `SpriteBatch.Draw(...)`'s per-corner UV mapping. FNA source:
  `Graphics/SpriteEffects.cs` (`None=0`, `FlipHorizontally=1`, `FlipVertically=2`), `Graphics/
  SpriteBatch.cs`'s `PushSprite()` (lines ~1404-1411: `TextureCoordinateN.X = flipX[N^effects]*sourceW +
  sourceX`, an XOR-indexed per-corner lookup table).
- Production code exercised: `EasyGLSpriteBatchBackend::Draw(...)`'s UV computation
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` lines 1189-1265, specifically the
  `u1`/`u2`/`v1`/`v2` swap at lines 1218-1219 and the per-corner vertex assembly at lines 1262-1265).

## Purpose

Task 167's integration test for `SpriteEffects::FlipHorizontally`/`FlipVertically` at the UV level,
using `SamplerState::PointClamp` (nearest-neighbour) so every destination pixel maps to exactly one
source texel with no bilinear blending ambiguity. Four 100×100 screen sections: a 2×1 `[Red|Blue]`
texture drawn with no flip and with `FlipHorizontally`, and a 1×2 `[Red/Blue]` texture drawn with no
flip and with `FlipVertically` — eight total sample points, each expected to read one of exactly two
colors with no interpolation ambiguity.

## Executive Verdict

**Healthy.** Traced the production UV-swap logic (`std::swap(u1,u2)` / `std::swap(v1,v2)`) and the
per-corner vertex assembly line-by-line against this test's eight expected sample values and found them
to match exactly; also cross-checked semantics against FNA's own XOR-indexed corner lookup table and
found the two implementations, while structurally different, produce identical observable UV mappings
for the axis-aligned, non-rotated quads this test uses.

## Checklist Results

### API / XNA / FNA parity
`SpriteEffects::None`/`FlipHorizontally`/`FlipVertically` values (used at lines 115/119/123/127) match
FNA's `SpriteEffects` enum exactly (`None=0`, `FlipHorizontally=1`, `FlipVertically=2` — confirmed
against `Graphics/SpriteEffects.cs`). The `Draw(texture, destRect, srcRect, color, rotation, origin,
effects, layerDepth)` overload used here (lines 114/118/122/126) matches one of FNA's own
`SpriteBatch.Draw` overload shapes (`Draw(Texture2D, Rectangle, Rectangle?, Color, float, Vector2,
SpriteEffects, float)`, `SpriteBatch.cs` line ~600) parameter-for-parameter.

### Behavioral correctness
Traced `EasyGLSpriteBatchBackend::Draw()`'s UV math (lines 1213-1219):
`u1=X/texW, v1=Y/texH, u2=(X+W)/texW, v2=(Y+H)/texH`, then `if (effects & FlipHorizontally)
std::swap(u1,u2)`, `if (effects & FlipVertically) std::swap(v1,v2)`. The four screen-space corners are
then assigned `(u1,v1)`/`(u2,v1)`/`(u2,v2)`/`(u1,v2)` to top-left/top-right/bottom-right/bottom-left
respectively (lines 1262-1265, no destination-rectangle Y-flip applied elsewhere in this path).

Verified each of the test's 8 assertions against this exact mapping:
- Section 0 (`hTex_`, `[Red|Blue]` at texture columns 0/1, no flip): `u1=0` (column 0, Red), `u2=1`
  (column 1, Blue). Left half of the 100px-wide destination samples `u≈0.25` → nearest to `u1`'s column
  → Red; right half samples `u≈0.75` → nearest `u2`'s column → Blue. Matches
  `{25,50,kRed}`/`{75,50,kBlue}` (lines 135-136) exactly.
- Section 1 (`FlipHorizontally`): `u1`/`u2` swapped (`u1=1`→column 1/Blue, `u2=0`→column 0/Red). Left
  half now nearest `u1` (Blue), right half nearest `u2` (Red). Matches
  `{125,50,kBlue}`/`{175,50,kRed}` (lines 138-139) exactly — this is the case that would fail under a
  no-op/broken flip implementation (left would stay Red), so this assertion has genuine discriminating
  power, not just a restatement of the no-flip case.
- Section 2 (`vTex_`, `[Red/Blue]` at rows 0/1, no flip): analogous reasoning on the V axis; top half
  (`v≈0.25`, nearest `v1`=row 0=Red), bottom half (`v≈0.75`, nearest `v2`=row 1=Blue). Matches
  `{250,25,kRed}`/`{250,75,kBlue}` (lines 141-142).
- Section 3 (`FlipVertically`): `v1`/`v2` swapped; top half now nearest `v1`(row1=Blue), bottom half
  nearest `v2`(row0=Red). Matches `{350,25,kBlue}`/`{350,75,kRed}` (lines 144-145).

All eight derivations independently confirm the file's own listed expectations (lines 21-29) — no
transcription error found between the header comment's stated design and the actual `checks[]` array.

Cross-checked against FNA's own mechanism (`SpriteBatch.cs` lines 1404-1411): FNA precomputes a
per-corner `flipX[4]`/`flipY[4]` lookup table indexed by `corner XOR effects`, rather than swapping
`u1`/`u2` directly — a different implementation strategy, but for an axis-aligned, unrotated quad (the
only case this test exercises) the two approaches are provably equivalent: swapping which physical
corner receives the min vs. max source coordinate is exactly what XOR-ing the corner index against the
effects bitmask achieves. This test's assertions are therefore validating genuinely FNA-equivalent
behavior, not merely internally-consistent-with-itself CNA behavior.

### Robustness
`hTex_`/`vTex_` construction (`Texture2D(dev, 2, 1)`/`(dev, 1, 2)` + `SetData`, lines 86-92) and pixel
readback both correctly account for row-major texel ordering (`hPix[0]`=column 0, `hPix[1]`=column 1;
`vPix[0]`=row 0, `vPix[1]`=row 1) — verified this matches the file's own inline comments (lines 71/73)
and the actual `Texture2D::SetData`/`GetBackBufferData` row-major convention used consistently elsewhere
in this codebase.

`colourMatch()`'s `tol=60` default (line 54) is loose in absolute terms but cannot actually confuse Red
vs. Blue for this test's specific colors (`(255,0,0)` vs `(0,0,255)` differ by 255 in both R and B
channels, far outside any plausible `±60` collision), so the looseness does not weaken this specific
test's discriminating power, even though it would be too loose for a test distinguishing more similar
colors.

### Testing
Covers `None`/`FlipHorizontally`/`FlipVertically` individually but not the combined `FlipHorizontally |
FlipVertically` case (`SpriteEffects` is documented — and used elsewhere in this codebase, per FNA's own
`effects &= (SpriteEffects)0x03` masking — as a bit-flaggable enum, so a combined-flip test is a
legitimate, currently-untested combination). Does not test flip combined with non-zero `rotation` or a
non-zero `origin`, both of which interact with the same vertex-assembly code path
(`rotateAndTranslate()`, lines 1240-1258) this test only exercises at `rotation=0`/`origin=(0,0)`.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — production UV-flip logic matches the test's expectations and FNA's
own observable behavior exactly for every case this file exercises.

### F1 — `SamplerState::PointClamp` requires a `const_cast` to satisfy `SpriteBatch::Begin`'s non-`const` pointer parameter

- Severity: LOW
- Confidence: HIGH
- Category: API design (cross-file, not unique to this test)
- Location/symbol: `const_cast<SamplerState*>(&SamplerState::PointClamp)` (line 107); `SpriteBatch::Begin
  (SpriteSortMode, BlendState, SamplerState* samplerState, DepthStencilState*, RasterizerState*, Effect*,
  Matrix)` (`SpriteBatch.hpp` lines 150-156) takes a non-`const SamplerState*`, while
  `SamplerState::PointClamp` (like `BlendState::Opaque`/`CornflowerBlue`-style builtin static states
  throughout this project) is a `static const` instance — its address can only be passed to a
  non-`const`-pointer parameter via an explicit `const_cast`.
- Evidence: confirmed this exact `const_cast<SamplerState*>` pattern recurs in 49 other files under
  `examples/` — a pervasive, established idiom project-wide, not an isolated workaround unique to this
  file.
- Why it matters: cosmetic/ergonomic only — `Begin()` does not (and structurally should not, given
  `SamplerState` objects are read-only render-state descriptors, not mutated by `SpriteBatch`) write
  through this pointer, so the `const_cast` is safe in practice, but its sheer repetition across 49 files
  suggests `Begin()`'s own parameter type (`SamplerState*`/`DepthStencilState*`/`RasterizerState*`)
  would more accurately be `const SamplerState*` etc., removing the need for every caller passing a
  builtin static state to work around it. This is an API-design observation belonging to
  `SpriteBatch.hpp`'s own audit (a different shard), not a defect in this test file, which is merely
  conforming to the established (if slightly awkward) existing API shape.
- FNA/XNA comparison: N/A — C# has no `const`, so FNA's own `Begin(..., SamplerState samplerState, ...)`
  has no analogous friction.
- Suggested future action (not implemented by this audit): flag for `SpriteBatch.hpp`'s own audit
  report; not a change this file's audit should drive on its own given the scale of the existing
  pattern (49 call sites).

## Cross-File Observations

- This is the only file in this 8-file batch that exercises 2D `SpriteBatch` rather than a 3D stock
  effect — its production code path (`EasyGLSpriteBatchBackend::Draw`) is entirely independent of the
  `SkinnedEffect`/`SkinnedPbrEffect` shader-dispatch code the other seven files in this batch exercise,
  so none of this batch's `SkinnedEffect`-related findings (missing world-space normal transform, etc.)
  are relevant here.
- `BlendState::Opaque` used for both the outer `dev.setBlendStateProperty()` (line 102) and the
  `sb_->Begin(...)` call (line 106) — consistent, avoids a state mismatch between the device-level and
  batch-level blend state that could otherwise cause an unnecessary/incorrect blend-state transition.

## Missing or Weak Tests

- No test of `SpriteEffects::FlipHorizontally | FlipVertically` combined (both bits set) — a legitimate,
  currently-untested bitwise-OR combination of the two flags this file otherwise tests individually.
- No test of flip combined with non-zero `rotation`/`origin` — both interact with the same
  `rotateAndTranslate()` vertex-assembly code this file only exercises at the identity case.

## Positive Findings

- Genuinely well-designed pixel test: uses `SamplerState::PointClamp` specifically to eliminate
  bilinear-sampling ambiguity, samples well away from section seams, and uses two-color textures whose
  channel separation (255 vs. 0) makes the ±60 tolerance harmless rather than a hidden weakness.
- The four-section layout (`h`-flip and `v`-flip each get a no-flip control case directly adjacent to
  the flipped case) is a strong design: it demonstrates the flip flag genuinely changes behavior
  relative to its own local baseline, not just that some hardcoded expected color happens to appear.

## Final Assessment

A correct, well-targeted test whose production code was traced end-to-end and found to match both the
test's own expectations and FNA's real `SpriteEffects` UV-flip semantics; its only gaps are two
reasonable, low-priority missing combinations (combined flip flags; flip + rotation/origin) and a
purely cosmetic, project-wide `const_cast` pattern that belongs to a different file's audit.
