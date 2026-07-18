# Audit: examples/sdlrenderer_spritefont_multiglyph_spacing_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritefont_multiglyph_spacing_test.cpp` (149 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch::DrawString` horizontal-advance/spacing pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_spritefont_multiglyph_spacing`
  / `SDL_Renderer_SpriteFont_MultiGlyphSpacing`, `cmake/Tests/SdlRendererTests.cmake:175-177`), introduced by
  `73d84286`/`8541a5a2` ("test(Task 691): verify SpriteFont multi-glyph spacing on SDL_Renderer").
- XNA/FNA relevance: direct — `SpriteFont.Spacing`, per-glyph kerning-driven horizontal advance in
  `SpriteBatch.DrawString`.
- FNA reference: `Graphics/SpriteBatch.cs` `DrawString` advance logic (`curOffset.X += Spacing + cKern.X` for
  non-first glyphs, `curOffset.X += cKern.Y + cKern.Z` after every glyph).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`DrawString`, lines 401-506,
  specifically the `firstInLine`/`curOffset.X` block at lines 467-476 and 504).

## Purpose

`SdlSpriteFontMultiGlyphSpacingTest` (lines 61-142) builds a two-glyph font (`'A'` white, `'B'` green, each 8x8,
`kerning=(0,8,0)`) with `spacing=4.0f` (line 91), draws `"AB"` at `(2,2)`, and checks: inside `'A'`'s glyph
(white), inside `'B'`'s glyph (green), the exact 4px gap between them (background black), and the regions
immediately left of `'A'` and right of `'B'` (also background) — proving the `spacing` constant is genuinely
applied (not zero, not some other value) and that each glyph samples its own, correctly-indexed atlas region.

## Executive Verdict

**Healthy.** Independently re-derived both glyphs' destination rectangles from the actual `DrawString`
implementation and they match the file's own header-comment arithmetic exactly: `'A'` at `(2,2,8,8)`, `'B'` at
`(14,2,8,8)`, with the 4px gap `x∈[10,14)` landing precisely between them.

## Checklist Results

### API / XNA / FNA parity

`SpriteFont::setSpacingProperty`/`getSpacingProperty` (constructed here via the `spacing` constructor parameter,
line 91) maps to FNA's own `SpriteFont.Spacing` property; the advance formula this test exercises
(`curOffset.X += spacing_ + cKern.X` for the second-and-later glyph in a line, `SpriteBatch.cpp:475`) is a direct,
correctly-ordered port of FNA's own `SpriteBatch.cs` `DrawString` advance step.

### Behavioral correctness

Re-derived by hand using the real production values (`effects=None`, `origin=(0,0)`, `scale=(1,1)`,
`position=(2,2)`, both glyphs' `cropping_=(0,0,8,8)`, `kerning_=(0,8,0)`, `glyphData_[0]=(0,0,8,8)` for `'A'`,
`glyphData_[1]=(8,0,8,8)` for `'B'`):
- `'A'` (`firstInLine`): `curOffset.X += abs(0) = 0`. `offsetX = 0 + (0+0)*(-1) = 0`; `dest.X = round(2+0) = 2`.
  `dest = (2,2,8,8)` — matches the header comment exactly.
- After `'A'`: `curOffset.X += cKern.Y(8)+cKern.Z(0) = 8`.
- `'B'` (not first): `curOffset.X += spacing_(4) + cKern.X(0) = 4` → `curOffset.X = 12`. `offsetX = 0 +
  (12+0)*(-1) = -12`; `localX = 12`; `dest.X = round(2+12) = 14`. `dest = (14,2,8,8)` — matches the header
  comment exactly.
- `'A'` occupies `x∈[2,10)`, `'B'` occupies `x∈[14,22)`, leaving `x∈[10,14)` (exactly 4px) as background — this
  audit independently confirms the file's own claimed geometry is the actual, current behavior of `DrawString`,
  not merely internally self-consistent commentary.

### Logic

The five checks (lines 109-115) each target a distinct region: inside `'A'`, inside `'B'`, the gap, left of
`'A'`, right of `'B'` — collectively ruling out both "spacing is zero/wrong" and "glyph index mix-up" (a bug that
swapped which colour renders at which position would fail at least one of the two "inside glyph" checks, since
`'A'`=white and `'B'`=green are visually distinct).

### Memory/resource lifetime

Single-shot `Initialize()`/`Draw()` pattern, `atlas_`/`font_` kept alive for the whole test — no concerns.

### C++ correctness

No unsafe casts; consistent `SharpRuntime::charcs` usage.

### Performance

N/A.

### Thread safety

N/A.

### Architecture

Correctly builds on the single-glyph fixture pattern established by `sdlrenderer_spritefont_single_glyph_test.cpp`
(same shard, same batch), adding exactly one new variable (`spacing`) plus a second glyph — good incremental
test-suite design.

### Maintainability

149 lines, consistent structure/naming with the rest of the SpriteFont sub-suite.

### Portability

Requires `PresentationMode::NativeBackBuffer` (line 138), same justification as sibling files.

### Robustness

Same PASS/FAIL-per-check pattern as the rest of the shard.

### Testing

This file is itself a test; appropriately scoped to the spacing/advance feature specifically, without also trying
to cover newline or defaultCharacter (correctly left to sibling files).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Test's own comment says `curOffset.X = abs(0) = 0` for the first glyph, which is trivially true only because this fixture's kerning left-bearing is itself zero

- Severity: INFO
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: header comment lines 15-17; `kerning = { Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f) }`
  (line 89)
- Evidence: both glyphs use an identical `kerning.X` (left-bearing) of `0.0f`. The `firstInLine` branch in
  `DrawString` (`SpriteBatch.cpp:468-472`) is `curOffset.X += std::abs(cKern.X)` — specifically exercising the
  `std::abs(...)` (a negative left-bearing pushed back to zero, per FNA's own documented behavior: "for the first
  character in a line, always push the width rightward, even if the kerning pushes the character to the left").
  With `cKern.X == 0`, this test cannot distinguish "correctly takes the absolute value of a negative bearing"
  from "never applies the bearing at all" — both produce `curOffset.X = 0` for the first glyph.
- Why it matters: a regression that dropped the `std::abs(...)` entirely (e.g. changed to plain `cKern.X`) would
  not be caught by this file for the specific first-glyph-negative-bearing case, since this fixture's `kerning.X`
  is always `0`. This is a real, if narrow, coverage gap in the "first-in-line negative left-bearing" sub-case of
  the advance formula — none of the 6 SpriteFont files in this batch use a nonzero/negative `kerning.X` value.
- FNA/XNA comparison: FNA's own `SpriteBatch.cs` `DrawString` has the identical `Math.Abs(cKern.X)` step and the
  identical comment explaining why (a first-in-line glyph's own left-bearing must never move it left of
  `position`); this is a real, intentional XNA behavior with no dedicated CNA test coverage discovered anywhere in
  this batch.
- Related files: none of the other 5 SpriteFont test files in this batch use a nonzero `kerning.X` either.
- Suggested future action (not implemented by this audit): add (or confirm elsewhere in the `examples-tests-*`
  corpus) a case with a negative first-glyph left-bearing to directly exercise the `std::abs()` branch.

## Cross-File Observations

- Shares the exact `kerning=(0,8,0)` per-glyph convention with `sdlrenderer_spritefont_single_glyph_test.cpp`,
  `sdlrenderer_spritefont_newline_test.cpp`, and `sdlrenderer_spritefont_effects_test.cpp`'s two-glyph font — none
  of the four exercises a nonzero `kerning.X`, so F1's gap is shared across the whole batch, not unique to this
  file (flagged here since this is the file whose entire purpose is the kerning/spacing advance math).

## Missing or Weak Tests

See F1 — no file in this batch exercises a nonzero (in particular a negative) first-glyph left-bearing.

## Positive Findings

- Precise, independently-reproducible destination-rect derivation matching the header comment exactly for both
  glyphs.
- The five-region check layout (inside-A, inside-B, gap, left-of-A, right-of-B) is well-designed to catch both
  "wrong spacing magnitude" and "glyph index mix-up" failure modes independently.

## Final Assessment

A correct, well-targeted spacing/advance test with one narrow, honestly-scoped coverage gap (F1, INFO severity,
shared across the whole SpriteFont sub-suite in this batch) rather than any defect in the test itself or the
production code it exercises.
