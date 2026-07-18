# Audit: examples/easygl_spritefont_effects_rotation_scale_test.cpp

## Metadata

- Source file: `examples/easygl_spritefont_effects_rotation_scale_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest), 126 lines
- XNA/FNA relevance: exercises `SpriteBatch::DrawString(SpriteFont, string, Vector2, Color, float, Vector2,
  Vector2, SpriteEffects, float)` — the full 9-argument XNA `DrawString` overload, including `origin`/`scale`
  parameters
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/SpriteBatch.cs` (`DrawString`, ~line 700-854)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp:401-506` (the
  `origin`/`scale`/`rotation`-taking `DrawString` overload)
- Direct sibling/ancestor: `examples/sdlrenderer_spritefont_effects_test.cpp` "Test 2" (Task 694) — this file's own
  header comment states it is a direct EasyGL port of that test

## Purpose

Task 429: confirms `SpriteBatch::DrawString`'s `origin`/`scale` placement math on the EasyGL backend, for a single
glyph 'A' (8×8, White) drawn with `origin=(4,4)`, `scale=(2,2)`, `rotation=0.0f`, at `position=(10,10)`. The file's
own header comment pre-computes the expected destination rect via the shared formula (`localX =
curOffset.X+cCrop.X-origin.X`, `scaledX = localX*scale.X`, `dest.X = round(position.X+scaledX)`,
`dest.Width = round(glyphWidth*scale.X)`) and asserts `dest=(2,2,16,16)`.

## Executive Verdict

**Mostly healthy — the math this file actually exercises (origin+scale placement) is correctly implemented and
independently re-derived and confirmed against `SpriteBatch.cpp`, but the file's own name overstates its scope: no
call in this file (or in any of its SDL_Renderer/D3D11/D3D12/Dx3 sibling ports, cross-checked below) passes a
non-zero `rotation` to `DrawString`, so the actual `sin`/`cos` rotation transform this file is named after is
never exercised anywhere in the codebase for `SpriteFont` text. Its own outside-checks also use a diagonal
corner pattern that, worked through concretely below, would miss a same-axis position error of up to ~7 pixels.**

## Checklist Results

### API / XNA / FNA parity
The 9-argument `DrawString(SpriteFont&, string, Vector2, Color, float, Vector2, Vector2, SpriteEffects, float)`
overload used here (line 91-92) matches FNA's `SpriteBatch.DrawString(SpriteFont, string, Vector2, Color, float,
Vector2, Vector2, SpriteEffects, float)` signature order exactly (rotation, origin, scale, effects, layerDepth).

### Behavioral correctness
Re-derived the destination rect independently against `SpriteBatch.cpp:401-506`:
- `baseOffset = origin = (4,4)` (effects==None, no size-based adjustment, line 433-439).
- `curOffset.X` after the "first-in-line" branch (line 468-472): `+= abs(cKern.X)` where `cKern = (0,8,0)` →
  `curOffset.X = 0`.
- `offsetX = baseOffset.X + (curOffset.X + cCrop.X) * axisDirX[0]` where `axisDirX[0] = -1.0f` (line 427) →
  `offsetX = 4 + (0+0)*(-1) = 4`.
- `localX = -offsetX = -4`; `scaledX = localX*scale.X = -4*2 = -8`.
- `rotation=0` → `cosR=1, sinR=0` → `rotX = scaledX*cosR - scaledY*sinR = -8`.
- `dest.X = round(position.X + rotX) = round(10 + (-8)) = 2`; `dest.Width = round(cGlyph.Width*scale.X) =
  round(8*2) = 16`.
This matches the test's own header comment and the actual production formula exactly — confirmed line-by-line,
not just trusted.

### Logic
The test's `Draw()` samples 3 points: `(10,10)` (expected White, comfortably inside the computed `(2,2,16,16)`
dest rect), `(0,0)` and `(20,20)` (expected Black, both diagonally outside the dest rect's corners). See Finding
F1 for a concrete, computed weakness in this check pattern.

### Memory/resource lifetime
`gdm_`/`sb_`/`atlas_`/`font_` are `unique_ptr` members constructed in the constructor/`Initialize()` and outlive
the single `Draw()` call that uses them — no dangling-pointer risk. `SpriteFont`'s constructor
(`SpriteFont.hpp:44`) takes its `Texture2D` **by value**; verified `Texture2D` internally holds a
`std::shared_ptr<ITextureBackend> backend_` (`Texture2D.hpp:312`), so `font_ = std::make_unique<SpriteFont>(*atlas_,
...)` (line 76) is a cheap, reference-counted copy that shares the same GPU texture as `atlas_`, not an expensive
or semantically-incorrect deep copy.

### C++ correctness
No unsafe casts or lifetime issues; `SharpRuntime::charcs` (`u'A'`) used correctly for the `characters` vector per
`SpriteFont.hpp:47`.

### Testing
This file is the **only** place in the entire repository (grep-verified across `examples/*.cpp` for every
`DrawString(...)` call with a font/rotation argument) that names itself after `SpriteFont` rotation testing. Its
own header comment (line 4) and every sibling port (`sdlrenderer_spritefont_effects_test.cpp` Test 2, and this
file itself) explicitly pass `rotation=0.0f`, describing the code path as "already correct BEFORE Task 694's own
flip fix (untouched by that fix)" — i.e. the rotation branch was never the fix's target and remains, to this day,
untested with a non-zero angle by any file in this codebase. See F1.

## Detailed Findings

### F1 — Rotation is never exercised with a non-zero angle, and the outside-checks use a diagonal pattern with a large undetected-error margin

- Severity: MEDIUM
- Confidence: HIGH (both the "rotation=0 everywhere" claim and the position-margin arithmetic below are directly
  computed, not inferred)
- Category: test-coverage
- Location/symbol: `EasyGLSpriteFontEffectsRotationScaleTest::Draw` (line 91-92, 103-105);
  `SpriteBatch::DrawString`'s `rotX`/`rotY` computation (`SpriteBatch.cpp:492-493`)
- Evidence:
  1. **Rotation coverage**: grepped every `DrawString(...)` call site across `examples/*.cpp` (all backends —
     `easygl`, `sdlrenderer`, `d3d11_smoke_test.cpp`, `d3d12_smoke_test.cpp`, `dx3_spritefont_test.cpp`); every
     single one that supplies an explicit rotation argument passes `0.0f`. The `rotX = scaledX*cosR -
     scaledY*sinR` / `rotY = scaledX*sinR + scaledY*cosR` cross-terms (`SpriteBatch.cpp:492-493`) are therefore
     never evaluated with `sinR != 0` anywhere in this codebase's test suite — a sign error or axis swap in this
     formula (e.g. `rotY = scaledX*cosR + scaledY*sinR`) would currently pass every existing test.
  2. **Margin arithmetic for this file's own checks**: dest rect is `(2,2,16,16)` → occupies `x,y ∈ [2,18)`.
     Check `(10,10)` is 8px from every edge of that rect. A single-axis placement bug — e.g. `dest.X` shifted to
     `7` instead of `2` (a 5px error, still `x ∈ [7,23)`) — leaves `(10,10)` inside the (buggy) rect (still White,
     test still reports PASS), leaves `(0,0)` outside on X regardless (still Black, uninformative), and leaves
     `(20,20)` outside on Y regardless of the X bug (still Black, uninformative). **All three checks would report
     PASS despite a real, 5-pixel single-axis positioning defect.** This is exactly the diagonal-corner
     anti-pattern this test's own sibling (`easygl_spritefont_single_glyph_test.cpp`, audited separately) warns
     against in its own header comment ("not the diagonal corners, since a pure horizontal (or vertical)
     mis-placement wouldn't reach a corner check that is ALSO offset on the other axis").
- Why it matters: this is the one file in the whole `SpriteFont` test family named specifically after rotation,
  yet it neither exercises a non-zero rotation angle nor uses the tighter edge-midpoint check pattern its own
  sibling test uses for exactly this reason. A latent bug in the rotation cross-term formula, or a several-pixel
  regression in the origin/scale math, could ship undetected.
- FNA/XNA comparison: FNA's `DrawString` (`SpriteBatch.cs` ~line 809-847) computes the rotation via
  `Math.Sin(rotation)`/`Math.Cos(rotation)` passed into `PushSprite`, which performs the actual per-vertex rotation
  in `GenerateVertexInfo` (`SpriteBatch.cs:1348-1398`) — the same fundamental sin/cos-based transform CNA
  implements, just applied at a different point in the pipeline (CNA pre-rotates the computed glyph offset to
  produce an axis-aligned `dest` Rectangle, then separately forwards `rotation` again into `pushSprite` for the
  visual quad's own orientation). Neither FNA's nor CNA's version of this transform is exercised with a non-zero
  angle by any DrawString test found in this repository.
- Related files: `examples/sdlrenderer_spritefont_effects_test.cpp` (Task 694's original "Test 2", same
  `rotation=0.0f`, same diagonal-corner check pattern — this file is a faithful, not independently-introduced,
  copy of that gap); `examples/d3d11_smoke_test.cpp` and `d3d12_smoke_test.cpp` do exercise a genuine non-zero
  rotation, but only for `SpriteBatch::Draw` (plain sprites), never `DrawString`.
- Suggested action (not implemented by this audit): add a `DrawString` case with a non-zero rotation (e.g. 90°,
  where the expected pixel permutation is easy to hand-compute, mirroring the D3D11 smoke test's own
  `MathHelper.Pi/2.0f` corner-permutation pattern for `Draw`), and replace the diagonal `(0,0)`/`(20,20)` checks
  here with edge-midpoint checks at a 1px margin, as `easygl_spritefont_single_glyph_test.cpp` already does.

## Cross-File Observations

- Contrast with `easygl_spritefont_multiglyph_spacing_test.cpp` and `easygl_spritefont_newline_test.cpp` (both
  audited in this same batch): both of those use single-axis-only checks (fixed `y=6` or fixed `x=6`) placed at
  the exact midpoint of the gap being tested, which — independently re-derived in this audit — would correctly
  catch a spacing/line-spacing regression of even 1-2 pixels. This file is the outlier in the batch that uses the
  weaker diagonal-corner pattern.
- `colourMatch`'s `tol=40` tolerance (line 35-40) is loose in absolute terms but adequate here since every check is
  a stark White-vs-Black discrimination, not a subtle blend comparison — not a concern in isolation.

## Missing or Weak Tests

- No test anywhere in this repository exercises `SpriteBatch::DrawString` with a genuinely non-zero rotation
  angle (see F1) — a repository-wide gap this file's name specifically, but does not actually, address.
- This file does not exercise `SpriteEffects::FlipHorizontally`/`FlipVertically` in combination with rotation+scale
  (that combination is covered separately, without rotation, by `easygl_spritefont_effects_flip_test.cpp`).

## Positive Findings

- The origin/scale placement formula this file *does* test was independently re-derived from
  `SpriteBatch.cpp:401-506` line-by-line and confirmed to produce exactly the destination rect the test's own
  header comment predicts — a genuine, verified regression test for that part of `DrawString`.
- Correctly uses a real end-to-end render + `GetBackBufferData` readback (not a mock), consistent with this
  shard's established pattern.
- `Texture2D`-by-value into `SpriteFont`'s constructor is safe and cheap due to `Texture2D`'s internal
  `shared_ptr` backend handle — verified, not assumed.

## Final Assessment

The origin/scale math this file actually exercises is correctly implemented and was independently confirmed
against the production `DrawString` formula. However, the file is misleadingly named: it contributes no coverage
of `DrawString`'s actual rotation transform (rotation is held at exactly `0.0f`, matching every other DrawString
call site in the codebase), and its own outside-position checks use a diagonal pattern that, worked through with
concrete numbers, would miss a same-axis position bug of up to several pixels. This is a real, evidence-based
test-coverage gap, not a defect in the production code under test.
