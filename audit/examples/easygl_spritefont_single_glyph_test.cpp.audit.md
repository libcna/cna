# Audit: examples/easygl_spritefont_single_glyph_test.cpp

## Metadata

- Source file: `examples/easygl_spritefont_single_glyph_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest), 143 lines
- XNA/FNA relevance: exercises `SpriteBatch::DrawString(SpriteFont&, string, Vector2, Color)` for the base case
  — a single glyph, no rotation/scale/origin/effects
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/SpriteBatch.cs` (`DrawString`,
  `PushSprite`/vertex-generation path)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp:401-506`
- Direct sibling/ancestor: `examples/sdlrenderer_spritefont_single_glyph_test.cpp` (Task 690) — this file's own
  header comment states it is a direct EasyGL port and that Task 690 was "the first-of-its-kind SpriteFont pixel
  test in this project"

## Purpose

Task 424: the foundational single-glyph placement test for this shard's whole `SpriteFont`/`DrawString` test
family. A minimal 8×8 solid-white 'A' glyph with zero cropping and zero kerning bearing is drawn at `(4,4)`, and
the test asserts the glyph lands at exactly `(4,4,8,8)` by probing one interior point and the midpoint of each of
the 4 edges immediately outside the glyph rect.

## Executive Verdict

**Healthy — this is the best-designed test in the 4-file `SpriteFont` sub-batch.** Its own header comment
explicitly reasons about *why* edge-midpoint checks (not diagonal corners) are used, and that reasoning was
independently re-derived here and confirmed correct: this file's check design has essentially zero margin for a
single-axis placement error, in contrast to the diagonal-corner pattern found (and flagged) in
`easygl_spritefont_effects_rotation_scale_test.cpp`.

## Checklist Results

### API / XNA / FNA parity
Uses the 3-argument `DrawString` overload; production code correctly forwards `rotation=0, origin=Zero,
scale=(1,1)` (`SpriteBatch.cpp:378-385`) matching FNA's own default-args forwarding chain.

### Behavioral correctness
With `origin=Zero`, `scale=(1,1)`, `rotation=0`, and zero cropping/kerning bearing, the formula in
`SpriteBatch.cpp:481-499` collapses to `dest.X = round(position.X + cCrop.X) = round(4+0) = 4`,
`dest.Width = round(cGlyph.Width*1) = 8` — giving `dest = (4,4,8,8)`, matching the test's own inline comment
("Glyph occupies screen rect (4,4,8,8) -> x in [4,12), y in [4,12)"). Independently re-derived and confirmed.

### Logic
The five checks (lines 103-110):
- `(8,8)` White — dead center of the glyph.
- `(3,8)` Black — 1px left of the left edge (`x=4`), at the vertical midpoint.
- `(12,8)` Black — 1px right of the right edge (`x=12` is the first excluded column), at the vertical midpoint.
- `(8,3)` Black — 1px above the top edge, at the horizontal midpoint.
- `(8,12)` Black — 1px below the bottom edge, at the horizontal midpoint.

Each of the four "outside" checks varies **only one** axis relative to the glyph's own center-line on the other
axis, and sits exactly 1px past the relevant edge. Re-derived the discriminating power directly: a single-axis
placement bug of even 1px (e.g. `dest.X` off by 1, making the rect `(5,4,8,8)`, i.e. `x ∈ [5,13)`) flips the
`(3,8)`-vs-left-edge relationship not at all (still outside) but changes the `(12,8)` check's outcome: `x=12` is
now inside `[5,13)` → the check would read White instead of the expected Black and correctly report `[FAIL]`.
Symmetric reasoning holds for a Y-axis bug via the `(8,3)`/`(8,12)` pair. This is a genuinely tight, 1px-margin
test — the tightest in this 4-file batch.

### Memory/resource lifetime
Same `unique_ptr`-member pattern as this batch's other `SpriteFont` tests — members outlive the single `Draw()`
call, no dangling-pointer risk.

### Testing
This file anchors the whole sub-batch's fixture convention (8×8 solid-color atlas, zero cropping, `kerning=(0,8,0)`
representing "no bearing, 8px advance") that `multiglyph_spacing`, `newline`, and `effects_rotation_scale` all
build on, per each of their own header comments. Confirmed by direct inspection that all four files use the
identical `SpriteFont` construction pattern (`glyphBounds=cropping=(0,0,8,8)`, `kerning=(0,8,0)` per glyph), so
this file's fixture choices are load-bearing context for the whole batch, not just itself.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings for this file — the strongest-designed test in its sub-batch.

### F1 — No check for glyph-height/width correctness independent of position (a uniform scale bug that grew the glyph symmetrically around its own center would not be caught)

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: `EasyGLSpriteFontSingleGlyphTest::Draw` (checks, lines 103-110)
- Evidence: all five checks are anchored relative to the *expected* `(4,4,8,8)` rect's own edges/center. A
  hypothetical bug that computed `dest.Width`/`dest.Height` correctly as `8` but sourced them from the wrong
  field (e.g. always `8` regardless of `scale`, which happens to be `1.0` in this specific test) would not be
  distinguished from a correct implementation here, since this test never varies `scale` away from its default.
  This is an inherent, reasonable scope limit of a single-glyph *base-case* test — the `scale` dimension is
  covered by the separate `effects_rotation_scale` test — not a defect.
- Why it matters: purely a scope-boundary note; each of this batch's 4 files intentionally isolates one variable
  (position baseline / spacing / newline / origin+scale), and this finding is recorded only to make that
  intentional split explicit rather than implying an accidental gap.
- FNA/XNA comparison: N/A.
- Suggested action: none — the batch's overall coverage strategy (each file isolating one axis of `DrawString`'s
  behavior) is sound; noted here only for completeness.

## Cross-File Observations

- This file's edge-midpoint check pattern is explicitly reasoned about in its own header comment ("not the
  diagonal corners, since a pure horizontal (or vertical) mis-placement wouldn't reach a corner check that is
  ALSO offset on the other axis, silently passing a broken test") — that reasoning was independently verified in
  this audit (see Logic above) and found to be accurate. `easygl_spritefont_effects_rotation_scale_test.cpp`
  (audited in this same batch) does **not** follow this same discipline and was flagged there for exactly the
  weakness this file's own comment warns against — worth surfacing as a concrete, cross-file "the codebase
  documents the right pattern in one place but doesn't consistently apply it" observation for
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

- None beyond the intentional, reasonable scope split noted in F1.

## Positive Findings

- Genuinely rigorous 1px-margin check design, independently confirmed via direct substitution to catch a 1-pixel
  single-axis placement regression on either axis.
- Its own header comment correctly documents and justifies its check-point design choice, and that justification
  holds up under independent scrutiny.
- Serves as the well-founded fixture template the rest of this test sub-batch correctly builds on.

## Final Assessment

The strongest-designed test among this batch's four `SpriteFont` files: a tight, well-reasoned, independently
re-verified 1px-margin regression test for `DrawString`'s baseline glyph-placement math, whose own documented
design rationale (edge-midpoint over diagonal-corner checks) is correct and — by contrast with a sibling file in
this same batch — not applied uniformly elsewhere in the codebase.
