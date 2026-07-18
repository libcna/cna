# Audit: examples/bgfx_spritefont_single_glyph_test.cpp

## Metadata

- Source file: `examples/bgfx_spritefont_single_glyph_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — Task 809, `SpriteFont` single-glyph placement on Bgfx
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_spritefont_single_glyph …)` /
  `cna_register_backend_test(NAME Bgfx_SpriteFont_SingleGlyph …)`, `cmake/Tests/BgfxTests.cmake:810-812`).
- XNA/FNA relevance: direct — `SpriteFont`'s internal constructor (glyph/cropping/kerning tables),
  `SpriteBatch::DrawString`.
- FNA reference: `Graphics/SpriteFont.cs` (internal constructor, lines 95-119),
  `Graphics/SpriteBatch.cs` (`DrawString` glyph-placement math, lines ~790-840).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteFont.hpp` (constructor,
  lines 42-49), `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`DrawString`, lines 401-505,
  in particular the `cCrop`/`offsetX`/`offsetY` computation at lines 478-482).

## Purpose

Builds a minimal hand-authored single-glyph `SpriteFont` (one 8×8 solid-white glyph 'A', zero
cropping offset, zero left/right kerning bearing, glyph width 8) and asserts that
`SpriteBatch::DrawString(font, "A", Vector2(4,4), White)` places the glyph at the exact screen
rectangle `(4,4,8,8)` on an otherwise black 24×24 backbuffer. Five point-samples are checked: one
inside the glyph rect (must be White) and four just outside each of its four edges at that edge's own
midpoint (must be Black), so an X-only or Y-only placement bug is independently caught by at least
one of the four edge checks rather than being masked by symmetry.

## Executive Verdict

**Healthy** — the glyph-table construction (`Vector3(0,8,0)` kerning, `(0,0,8,8)` cropping) was
independently traced through both FNA's and CNA's `DrawString` glyph-offset formula and produces
exactly the claimed `(4,4,8,8)` destination rectangle; the five-point sampling strategy genuinely
discriminates X-only/Y-only errors, not just "something got drawn."

## Checklist Results

### API / XNA / FNA parity
The `SpriteFont` constructor used here (`SpriteFont(texture, glyphBounds, cropping, characters,
lineSpacing, spacing, kerning, defaultCharacter)`, lines 104-106) is correctly marked `NOXNA` in
`SpriteFont.hpp` — FNA's own matching constructor (`SpriteFont.cs` lines 95-108) is `internal`, only
ever invoked by `SpriteFontReader` from XNB content; since CNA has no XNB pipeline this test (and the
production API) correctly exposes it directly rather than inventing a different shape. Parameter
order matches FNA's internal constructor field-for-field (`texture, glyphBounds, cropping,
characters, lineSpacing, spacing, kerningData, defaultCharacter`).

### Behavioral correctness
Traced the glyph-placement formula by hand against `SpriteBatch.cs`'s `DrawString` (lines 790-814,
mirrored in CNA's `SpriteBatch.cpp` lines 478-482, which the code itself comments as "Mirrors FNA's
SpriteBatch.DrawString axis-direction tables"): for the first (and only) character in the line,
`curOffset.X += Abs(cKern.X)` = `Abs(0) = 0`; `offsetX = baseOffset.X + (curOffset.X + cCrop.X) *
axisDirX = baseOffset.X + 0`. With `cCrop=(0,0,8,8)` and default origin (`Vector2::Zero` — the
2-argument `DrawString(font, text, position, color)` overload used here forwards `origin=Vector2::Zero`,
confirmed at `SpriteBatch.cpp` line 383), `baseOffset` reduces to `position = (4,4)` with no
origin-driven shift. The glyph is therefore drawn at exactly `(4,4)` with size
`(cGlyph.Width*scale.X, cGlyph.Height*scale.Y) = (8,8)` (default `scale=Vector2::One`), i.e. screen
rect `(4,4,8,8)` — matching the file's own stated design and all five expected check values.

### Logic
`RunCheck` (lines 67-86) redraws the *entire* scene fresh on every retry iteration (`Clear` + full
`Begin/DrawString/End`) and reads back exactly once per iteration, correctly following this shard's
established Bgfx `GetBackBufferData` "first read per rendered frame" workaround (Task 406) rather
than the single-frame multi-region read the EasyGL/Vulkan ancestor test used — consistent with the
file's own header comment explaining the restructuring rationale.

### C++ correctness
`colourMatch` (lines 46-51) only compares R/G/B, not A — appropriate here since every expected color
in this test (`kWhite`, `kBlack`) is fully opaque and the backbuffer read always returns opaque data;
not a defect for this specific file's scope.

### Robustness
The four edge-midpoint checks (`(3,8)`, `(12,8)`, `(8,3)`, `(8,12)`) are placed exactly one pixel
outside each of the glyph rect's four sides at the side's midpoint — this specifically catches an
axis-only off-by-one or a swapped-axis bug that a single "surrounding area" check would miss (e.g. a
bug that shifted the glyph one pixel too far right would turn the `(12,8)` check from Black to White
while leaving `(3,8)`/`(8,3)`/`(8,12)` unaffected).

### Testing
Genuinely exercises `SpriteFont`'s glyph/cropping/kerning table plumbing end-to-end through
`DrawString`, not just "does it compile" — the five assertions are position-discriminating, not merely
existence-discriminating.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `colourMatch`'s ±40 tolerance is generous for a hand-authored 8×8 solid-color atlas

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: `colourMatch` (lines 46-51), default `tol=40`
- Evidence: the glyph atlas is a flat solid-white 8×8 texture with `TextureFilter` left at its
  default (`Linear`, since no `SamplerState` is passed to `Begin()` — line 75 uses the parameterless
  `Begin()` overload), so under nearest-edge sampling near a glyph boundary a small amount of
  blend-driven partial coverage is plausible and the generous tolerance absorbs it.
- Why it matters: a ±40/255 tolerance on a binary black/white test could in principle mask a modest
  (~15%) mis-registration of the glyph rectangle in a way a tighter tolerance would catch; this is a
  design trade-off already used consistently by every sibling `*_transform_matrix_test.cpp`/
  `*_spritefont_single_glyph_test.cpp` file in this project (same helper, same constant), so it is a
  shared, deliberate convention rather than an oversight specific to this file.
- Related files: `examples/bgfx_transform_matrix_test.cpp` (same `colourMatch` helper, same
  tolerance), `examples/easygl_spritefont_single_glyph_test.cpp` (ancestor).
- Suggested future action: none required — flagged for completeness; the five-point sampling
  strategy's discriminating power comes primarily from *where* it samples, not from a tight numeric
  tolerance.

## Cross-File Observations

- Direct, intentionally-non-verbatim adaptation of `examples/easygl_spritefont_single_glyph_test.cpp`
  (Task 424) — the restructuring from "one frame, five regions" to "five frames, one region each" is
  the only substantive difference, and it is the correct fix for Bgfx's read-back quirk rather than a
  behavior change.
- Shares its `colourMatch`/`kWhite`/`kBlack` helper shape with `bgfx_transform_matrix_test.cpp` in
  this same batch — both are small, single-purpose pixel-placement tests using an identical idiom.

## Missing or Weak Tests

None beyond F1's tolerance observation. The five-point strategy already covers the interesting
X/Y-discrimination cases for a single glyph at a non-origin position.

## Positive Findings

- Glyph table construction (`glyphBounds`, `cropping`, `kerning`) was independently re-derived from
  FNA's `DrawString` formula and confirmed to produce exactly the stated `(4,4,8,8)` destination rect,
  not merely trusted from the file's own comment.
- The edge-midpoint sampling strategy is a genuinely well-designed technique for catching axis-only
  placement bugs, reused consistently across this project's placement tests.
- Correctly follows the shard's established Bgfx read-back-quirk workaround pattern.

## Final Assessment

A small, well-targeted, and behaviorally-verified test; no correctness issues found beyond one shared,
low-severity tolerance observation that applies project-wide, not uniquely to this file.
