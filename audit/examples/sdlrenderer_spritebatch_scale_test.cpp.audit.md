# Audit: examples/sdlrenderer_spritebatch_scale_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_scale_test.cpp` (143 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch::Draw` scalar/`Vector2` scale overload pixel test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_spritebatch_scale …)` /
  `cna_register_backend_test(NAME SDL_Renderer_SpriteBatch_Scale …)`,
  `cmake/Tests/SdlRendererTests.cmake:69-71`. Header traces to Task 672, an explicit port of Task 418's EasyGL
  test (`examples/easygl_spritebatch_scale_test.cpp`).
- XNA/FNA relevance: `SpriteBatch.Draw(texture, position, sourceRectangle, color, rotation, origin, float
  scale, effects, layerDepth)` and its `Vector2 scale` sibling overload.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (the two `scale`-taking
  `Draw` overloads, computing `destRect.Width/Height = sourceRect.Width/Height * scale[.X/.Y]`).
- FNA reference: `Graphics/SpriteBatch.cs`'s float-scale and `Vector2`-scale `Draw` overloads (`destW = scale *
  ...Width`; `scale.X *= ...Width`, `scale.Y *= ...Height`) — same non-uniform-scale-per-axis contract.

## Purpose

Draws a single 20×20 solid-Red texture twice against a Green background: once via the scalar-`float scale`
overload (`position=(50,50)`, `scale=3.0f` → expected 60×60 destination), once via the `Vector2 scale` overload
(`position=(200,50)`, `scale=(2.0f,4.0f)` non-uniform → expected 40×80 destination). Check points for the
second draw are deliberately chosen to catch two specific plausible implementation bugs: using only `scale.X`
for both axes (would give a 40×40 square, with its bottom edge at `y=90` instead of `y=130`), and using only
`scale.Y` for both axes (would give an 80×80 square, with its right edge at `x=280` instead of `x=240`).

## Executive Verdict

**Healthy.** Both overloads' expected destination-rectangle sizes were independently re-derived from
`SpriteBatch.cpp`'s actual scale-application arithmetic and FNA's own scale formula, and the non-uniform-scale
check points genuinely discriminate the two specific "wrong axis" bugs the header claims to guard against — not
just "is the sprite bigger than 20×20."

## Checklist Results

### API / XNA / FNA parity
Uses `Draw(texture, Vector2 position, std::optional<Rectangle>{}, Color::White, 0.0f, Vector2::Zero, 3.0f,
SpriteEffects::None, 0.0f)` (float-scale overload, `SpriteBatch.hpp` lines 235-238) and the `Vector2`-scale
sibling (lines 252-255) — both real, non-`NOXNA` XNA-parity overloads, correctly matching FNA's `Draw(Texture2D,
Vector2, Rectangle?, Color, float, Vector2, float, SpriteEffects, float)` and its `Vector2 scale` sibling
parameter-for-parameter.

### Behavioral correctness
Traced both overloads' bodies directly in `SpriteBatch.cpp`:
```
// float-scale overload:
dw = sourceRect.Width; dh = sourceRect.Height;   // 20, 20 (no explicit sourceRectangle -> whole texture)
destRect = Rectangle(position.X, position.Y, dw*scale, dh*scale);  // 20*3=60, 20*3=60
// Vector2-scale overload:
destRect = Rectangle(position.X, position.Y, dw*scale.X, dh*scale.Y);  // 20*2=40, 20*4=80
```
— matching the test's own claimed 60×60 and 40×80 expected sizes exactly, and matching FNA's own scale
arithmetic (`destW = scale * ...Width` / `scale.X *= ...Width, scale.Y *= ...Height`) in structure.
- Draw 1 checks: `(100,100)` interior → Red (well inside `[50,110)×[50,110)`); `(115,80)` "just past right
  edge" → Green (`115 > 110`, correctly outside the 60-wide square by 5px, not edge-adjacent enough to be
  fragile to off-by-one).
- Draw 2 checks: `(220,110)` interior → Red — independently verified this point lies inside `[200,240)×
  [50,130)` (the correct 40×80 rect) but *outside* the two wrong-bug hypotheses' rects: the "X-for-both" bug
  would produce `[200,240)×[50,90)`, and `y=110 > 90` falls outside that wrong rect, so this check would
  correctly fail under that bug (test comment's own claim, independently confirmed). The "Y-for-both" bug would
  produce `[200,280)×[50,130)`; `(220,110)` still falls *inside* that wrong rect, so this specific point alone
  would not by itself catch the Y-for-both bug — that is exactly why the second Draw-2 check point exists.
- `(245,70)` "just past right edge (rules out Y-for-both bug)": correctly outside the true 40-wide rect
  (`245 > 240`) but *inside* the Y-for-both wrong rect's width (`245 < 280`) — so if the Y-for-both bug were
  present, this point would incorrectly read Red instead of the expected Green, and the check would correctly
  fail. Both wrong-axis hypotheses are therefore genuinely covered, one point each, exactly as the header
  claims — independently confirmed by this audit's own point-by-point geometric re-derivation, not merely
  trusted from the comment.
- Final check `(50,250)` "far from both sprites" → Green background: a sane sanity check unrelated to either
  scale hypothesis.

### Logic
No branching; two straight `Draw` calls, five checks, correct aggregate `result_` semantics.

### Memory/resource lifetime
`tex_`/`sb_` `unique_ptr`-owned, constructed once, consistent with the shard.

### C++ correctness
No unsafe casts; `colourMatch` tolerance (`tol=60`, line 47) discriminates Red from Green decisively.

### Performance
N/A — single-frame test with two small draws.

### Thread safety
N/A.

### Architecture
Correctly requires `PresentationMode::NativeBackBuffer` (line 132), consistent with the rest of this batch.

### Maintainability
143 lines, single-purpose; the header's own explicit statement of which check rules out which specific bug
(lines 19-21) makes this file easier to audit than if the check points had been left unexplained.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The choice of check points for the non-uniform scale case is the standout strength here: rather than one
interior point (which would pass under either wrong-axis bug, since both wrong rectangles still contain the
sprite's own top-left region), the second point is placed specifically in the differential region between the
correct rectangle and each wrong hypothesis — a deliberate, non-trivial test-design technique.

### Testing
Correctly scoped to the two scale overloads specifically; rotation is fixed at `0.0f` and `origin` at `Zero`
throughout (rotation-and-scale-combined behavior is not exercised here, nor claimed to be — a reasonable split
from `sdlrenderer_spritebatch_rotation_test.cpp`, same batch, which fixes scale implicitly at 1.0 via a destRect
draw).

### Cross-file consistency
The scale-application arithmetic traced here (`dw*scale`/`dw*scale.X, dh*scale.Y`) is internally consistent
between `SpriteBatch.cpp`'s two overloads and matches FNA's own per-axis scale contract; no drift found.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Complements `sdlrenderer_spritebatch_overloads_test.cpp` (same batch), which exercises the same two scale
  overloads only superficially (uniform slot-filling scale, single slot-centre check each) — this file is the
  correct, more rigorous owner of the "scale is applied correctly per-axis" claim, and the two files' checks do
  not conflict.
- Shares the `PresentationMode::NativeBackBuffer` idiom with the rest of the batch.

## Missing or Weak Tests

None identified for this file's stated scope. A combined rotation+non-uniform-scale case is not covered by any
file in this batch (each dimension — rotation, uniform scale, non-uniform scale, source-rect cropping — is
tested independently, never in combination) — a reasonable, common test-design tradeoff (combinatorial
explosion vs. per-feature isolation), not flagged as a defect.

## Positive Findings

- The non-uniform-scale check-point placement is a genuinely well-reasoned, differential test design that
  specifically isolates two distinct plausible implementation bugs rather than only confirming "some scaling
  happened."
- Both overloads' expected geometry were independently re-derived from the actual current
  `SpriteBatch.cpp` arithmetic and FNA's own scale formula, and both check out exactly.

## Final Assessment

A well-designed, correctly-verified test of both scale overloads, whose non-uniform-scale check points were
independently confirmed by this audit to genuinely discriminate the two most likely per-axis implementation
mistakes, not merely to prove "the sprite got bigger."
