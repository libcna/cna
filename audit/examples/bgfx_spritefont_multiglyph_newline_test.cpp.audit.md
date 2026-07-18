# Audit: examples/bgfx_spritefont_multiglyph_newline_test.cpp

## Metadata

- Source file: `examples/bgfx_spritefont_multiglyph_newline_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SpriteFont` multi-glyph spacing + newline advance pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_spritefont_multiglyph_newline …)` /
  `cna_register_backend_test(NAME Bgfx_SpriteFont_MultiGlyphSpacingNewline …)`,
  `cmake/Tests/BgfxTests.cmake:819-821`).
- XNA/FNA relevance: direct — `SpriteFont.Spacing`, `SpriteFont.LineSpacing`, per-glyph `Kerning`
  (left-bearing/width/right-bearing triple), and `SpriteBatch.DrawString`'s newline handling.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`
  (`DrawString`, lines 400-497 — kerning/newline logic at 442-495).

## Purpose

Combined 8-check pixel test (Task 810, merging Task 425's multi-glyph-spacing and Task 426's newline EasyGL/
Vulkan tests into one Bgfx file, matching this row's own bundled scope). Two-glyph font: 'A' (White) and 'B'
(Green), both 8×8 with `kerning=(0,8,0)`, `spacing=4`, `lineSpacing=10` — deliberately different from the
glyphs' own 8px height, specifically so a bug that advanced lines by glyph height instead of the font's real
`lineSpacing_` would be caught. Drawing `"AB\nAB"` at `(2,2)`, the file hand-derives the expected destination
rectangle of all 4 glyphs and the resulting 2px background gap between the two text lines
(`y∈[10,12)`), and provides 8 checks: 2 per glyph interior, 1 intra-line gap, 1 inter-line gap, and 2
outside-the-text-block boundary checks.

## Executive Verdict

**Healthy** — this audit independently re-executed `SpriteBatch::DrawString`'s per-character loop by hand
against the test's exact font parameters and confirmed all 4 glyph destination rectangles the test's header
comment claims, including the specific 2px inter-line gap that is this test's core discriminating assertion
(lineSpacing vs. glyph-height advance).

## Checklist Results

### API / XNA / FNA parity
`SpriteFont`'s `Kerning` triple (per-glyph `Vector3(leftSideBearing, width, rightSideBearing)`) and
`LineSpacing`/`Spacing` properties match FNA's `SpriteFont` fields exactly in meaning; this test's kerning
values `(0,8,0)` (zero bearings, 8px advance width) are a deliberately simple case isolating the spacing/
lineSpacing behavior from bearing-offset complexity.

### Behavioral correctness
Independently traced `SpriteBatch::DrawString(font, "AB\nAB", (2,2), White)` against `SpriteBatch.cpp:442-495`
(`curOffset` starts at `(0,0)`, `firstInLine=true`, no `SpriteEffects` so `axisDirX[0]=axisDirY[0]=-1`,
`baseOffset=origin=(0,0)`):
- `'A'` (line 1, first): `firstInLine` ⇒ `curOffset.X += |kernX|=0` (still 0). `offsetX = 0 + (0 + cCrop.X=0)*(-1)
  = 0` ⇒ `localX=0` ⇒ `dest.X = 2+0 = 2`. `offsetY` similarly `0` ⇒ `dest.Y=2`. `dest=(2,2,8,8)` — matches the
  file's claimed "Line 1 'A': dest (2,2,8,8)". After render, `curOffset.X += kernY+kernZ = 8+0 = 8`.
- `'B'` (line 1, not first): `curOffset.X += spacing(4)+kernX(0) = 4` ⇒ `curOffset.X = 12`. `offsetX = (12+0)*
  (-1) = -12` ⇒ `localX=12` ⇒ `dest.X = 2+12 = 14`. `dest=(14,2,8,8)` — matches "Line 1 'B': dest (14,2,8,8)".
  `curOffset.X += 8 ⇒ 20` (unused before the newline resets it).
- `'\n'`: `curOffset.X=0; curOffset.Y += lineSpacing_(10)` ⇒ `curOffset=(0,10)`; `firstInLine=true`.
- `'A'` (line 2, first): `curOffset.X += 0 ⇒ 0`. `offsetX=0 ⇒ dest.X=2`. `offsetY = (10+0)*(-1) = -10` ⇒
  `localY=10 ⇒ dest.Y=2+10=12`. `dest=(2,12,8,8)` — matches "Line 2 'A': y∈[12,20)". `curOffset.X += 8 ⇒ 8`.
- `'B'` (line 2, not first): `curOffset.X += 4+0 = 4 ⇒ 12`. `offsetX=(12+0)*(-1)=-12 ⇒ localX=12 ⇒ dest.X=14`.
  `dest=(14,12,8,8)` — matches "Line 2 'B': dest (14,12,8,8)".
- The two lines therefore span `y∈[2,10)` and `y∈[12,20)` respectively — an exact 2px gap at `y∈[10,12)` that
  is only correct because `lineSpacing_=10` (not the glyphs' own height of 8) is what actually advances
  `curOffset.Y`. Had the implementation used glyph height instead, line 2 would start at `y=2+8=10` with **no**
  gap, and the `(6,11)`-gap check (expected Black) would instead read White/'A' — this is exactly the
  discriminating bug the file's header comment names, and this audit confirms the current implementation
  (`curOffset.Y += spriteFont.lineSpacing_`, not any glyph-derived quantity) avoids it.
- Verified all 8 check points against the 4 derived rectangles: (6,6)/(18,6)/(6,16)/(18,16) each land inside
  their respective glyph's 8×8 box (matching White/Green/White/Green); (12,6) falls in the 2px gap between
  line-1 'A' `[2,10)` and 'B' `[14,22)` → Black; (6,11) falls in the inter-line 2px gap `y∈[10,12)` → Black;
  (0,6) is left of line-1 'A' (`x<2`) → Black; (26,6) is right of line-1 'B' (`x≥22`) → Black.

### Logic
The deliberate choice `lineSpacing=10 ≠ glyphHeight=8` is exactly the right technique to make the newline
advance amount an independently falsifiable quantity rather than one that would coincidentally still "look
right" if the wrong field were read — this audit confirms the chosen values do create a real, checkable
divergence between the two candidate (correct vs. buggy) behaviors.

### Testing
8 checks across 2 glyphs × 2 lines, an intra-line gap, an inter-line gap, and 2 block-boundary checks is
thorough for the stated combined scope (spacing + newline); this audit found no check that is redundant with
another or that would pass under a plausible bug the test doesn't already target.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW defects found in this file.

## Missing or Weak Tests

- `SpriteFont.Spacing` and `Kerning`'s non-zero left/right-bearing components (`kernX`/`kernZ`) are both held
  at `0` throughout this test — a deliberate simplification (per the file's own stated aim of isolating
  spacing/lineSpacing from bearing offsets), but it means this specific file does not independently verify
  the bearing-offset arithmetic (`curOffset.X += |kernX|` for first-in-line, `curOffset.X += kernY+kernZ` after
  each glyph) beyond the trivial zero case. A non-zero-bearing case is presumably covered by
  `easygl_spritefont_multiglyph_spacing_test.cpp` (Task 425) or a sibling unit test; not re-verified as part of
  this file's own audit scope.
- `SpriteEffects` (flip) combined with multi-line text is not exercised here; `axisDirX`/`axisDirY`'s dependency
  on `effIdx` (used elsewhere for flip support, per `SpriteBatch.cpp`'s own comment about a "previously CNA's
  own bug" in this exact function) is not touched by this file, which always uses `effects=None` — a reasonable
  scope boundary given the file's stated combined purpose, but worth noting since that same function has a
  documented history of a flip-related bug.

## Positive Findings

- All 4 glyph destination rectangles and both background-gap checks were independently re-derived by this
  audit from `SpriteBatch::DrawString`'s actual per-character loop, not merely copied from the file's own
  comment — and they match exactly, with no stale-constant or accidental-tolerance-pass pattern found (the
  category of issue this audit was specifically primed to look for based on a sibling EasyGL finding).
- The `lineSpacing ≠ glyphHeight` test-design choice is a genuinely effective way to make the newline-advance
  behavior falsifiable rather than merely plausible.

## Final Assessment

A rigorous, correctly-derived combined spacing/newline test; this audit's independent re-execution of the
production `DrawString` algorithm against the test's exact parameters reproduces every one of its 8 expected
outcomes, including the specific 2px inter-line gap that is its central discriminating assertion.
