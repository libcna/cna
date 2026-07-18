# Audit: examples/sdlrenderer_spritefont_newline_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritefont_newline_test.cpp` (140 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch::DrawString` newline/`LineSpacing` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_spritefont_newline` /
  `SDL_Renderer_SpriteFont_Newline`, `cmake/Tests/SdlRendererTests.cmake:181-183`), introduced by
  `74d1736b`/`f7f31817` ("test(Task 692): verify SpriteFont newline advances by lineSpacing on SDL_Renderer").
- XNA/FNA relevance: direct — `SpriteFont.LineSpacing`, the `\n` branch of `SpriteBatch.DrawString`.
- FNA reference: `Graphics/SpriteBatch.cs` `DrawString`'s newline branch (`curOffset.X = 0; curOffset.Y +=
  spriteFont.LineSpacing; firstGlyphOfLine = true;`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`DrawString`, lines 449-455 —
  the `\n` branch).

## Purpose

`SdlSpriteFontNewlineTest` (lines 57-133) builds a single-glyph font (`'A'`, 8x8) with `lineSpacing=10` —
deliberately different from the glyph's own 8px height, so a bug that advanced by glyph height instead of the
font's actual `LineSpacing` would be caught rather than coincidentally passing. It draws `"A\nA"` at `(2,2)` and
checks: inside the first line's glyph, inside the second line's glyph (12px below, not 8px below), the 2px gap
between them, and the region below the second glyph.

## Executive Verdict

**Healthy.** Re-derived the two glyphs' destination rectangles from the real `DrawString` implementation and they
match the header comment's claimed `(2,2,8,8)` and `(2,12,8,8)` exactly — the 10px line spacing (not the 8px
glyph height) genuinely drives the vertical advance, which is precisely the property this test's choice of
`lineSpacing=10 ≠ glyphHeight=8` is designed to distinguish.

## Checklist Results

### API / XNA / FNA parity

The constructor's `lineSpacing=10` argument (line 83) maps to FNA's `SpriteFont.LineSpacing`; the `\n` handling in
`DrawString` (`SpriteBatch.cpp:449-455`: `curOffset.X = 0.0f; curOffset.Y += static_cast<float>(spriteFont.
lineSpacing_); firstInLine = true;`) is a direct, correctly-ordered match for FNA's own newline branch in
`SpriteBatch.cs`'s `DrawString` (same three effects: reset X, advance Y by `LineSpacing`, mark next glyph as
first-in-line so its own left-bearing gets the `Math.Abs()` treatment rather than plain addition).

### Behavioral correctness

Re-derived by hand (`effects=None`, `origin=(0,0)`, `scale=(1,1)`, `position=(2,2)`, `cropping_=(0,0,8,8)`,
`kerning_=(0,8,0)`, `glyphData_[0]=(0,0,8,8)`):
- 1st `'A'` (`firstInLine`): `curOffset=(0,0)`; `offsetX=0`, `offsetY=0`; `dest = (round(2+0), round(2+0), 8, 8)
  = (2,2,8,8)` → occupies `y∈[2,10)` — matches the header comment.
- `'\n'`: `curOffset.X=0`; `curOffset.Y += lineSpacing_(10) = 10`; `firstInLine=true`.
- 2nd `'A'` (`firstInLine` again): `curOffset.X += abs(0) = 0`. `offsetY = baseOffset.Y(0) + (curOffset.Y(10)
  +cCrop.Y(0))*axisDirY[0](-1) = -10`; `localY = -offsetY = 10`; `scaledY = 10*1 = 10`; `rotY = 10` (rotation=0);
  `dest.Y = round(2+10) = 12`. `dest = (2,12,8,8)` → occupies `y∈[12,20)` — matches the header comment exactly.
- Gap `y∈[10,12)` (exactly 2px) between the two lines — matches the header's claimed "exact 2px gap."
- This audit independently confirms the file's own claimed geometry is the current, actual behavior of
  `DrawString`'s newline branch, not merely internally self-consistent commentary.

### Logic

The four checks (lines 101-106) target: inside line 1, inside line 2 (at the *shifted*, non-8px-offset position),
the gap, and below line 2 — this specific choice of sample points (`y=6`, `y=16`, `y=11`, `y=22`) is exactly what
distinguishes "advanced by `LineSpacing=10`" from "advanced by glyph height `=8`" (an 8px-advance bug would have
placed the second glyph at `y∈[10,18)`, which would fail the `y=16`-inside and `y=11`-gap checks differently than
the correct `y∈[12,20)` placement does) — a genuinely discriminating test design, not merely "renders something."

### Memory/resource lifetime

Single-shot `Initialize()`/`Draw()`, no lifetime concerns.

### C++ correctness

No unsafe casts; consistent `SharpRuntime::charcs`/`Vector3` kerning-tuple usage matching the rest of the batch.

### Performance

N/A.

### Thread safety

N/A.

### Architecture

Correctly isolates the newline/`LineSpacing` variable alone (single glyph, zero spacing, zero kerning bearings) —
consistent with the same incremental-fixture-extension design seen across this batch's SpriteFont sub-suite.

### Maintainability

140 lines, consistent structure/naming with sibling files in this batch.

### Portability

Requires `PresentationMode::NativeBackBuffer` (line 129), same justification as sibling files.

### Robustness

Same PASS/FAIL-per-check, latching `result_`, clean-`Exit()` pattern as the rest of the shard.

### Testing

This file is itself a test, correctly scoped to the newline/LineSpacing feature in isolation.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Does not test a `\r` (carriage return) or `\r\n` sequence, despite `DrawString` having explicit `\r`-skip logic

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `SpriteBatch.cpp:448` (`if (c == u'\r') continue;`) has no exercising test anywhere identified
  in this batch of 8 files.
- Evidence: `DrawString`'s loop explicitly special-cases `'\r'` as a no-op-and-continue (mirroring FNA's own
  `SpriteFont.cs`/`SpriteBatch.cs` treatment of `'\r'` as ignorable, e.g. for Windows-style `"\r\n"` line endings)
  — this test only exercises `"A\nA"` (bare `'\n'`), never `"A\r\nA"`.
- Why it matters: a latent bug specific to `\r` handling (e.g. if a future refactor of the UTF-8 decode loop
  accidentally treated `\r` as an unresolved/unknown character requiring `defaultCharacter_` fallback, or advanced
  `curOffset` for it) would not be caught by any test in this shard's SpriteFont sub-suite as currently written.
- FNA/XNA comparison: FNA's `SpriteFont.cs` `MeasureString` and `SpriteBatch.cs` `DrawString` both have the
  identical bare `if (c == '\r') { continue; }` special case with no distinguishing behavior of their own beyond
  "skip"; this is intentional, correct behavior on both sides — just untested end-to-end on SDL_Renderer.
- Related files: none in this batch cover `\r`; worth checking whether `tests/Microsoft/Xna/Framework/Graphics/
  SpriteBatchTests.cpp` (mock-backend unit tests, out of this batch's scope) already covers it at the logic level.
- Suggested future action (not implemented by this audit): add a `\r\n`-sequence case (even as an addition to this
  file, or a new one) if not already covered by the mock-backend unit tests.

## Cross-File Observations

- The `\n`-branch's `firstInLine = true` reset (line 453) is exactly what makes the *next* glyph after a newline
  re-enter the `std::abs(cKern.X)` branch rather than the plain-addition branch — this test's fixture uses
  `kerning.X=0` for its single glyph, so (per the same reasoning as F1 in the sibling
  `sdlrenderer_spritefont_multiglyph_spacing_test.cpp` report) it cannot distinguish "correctly re-applies the
  first-in-line absolute-value rule after a newline" from "never applies it at all" — consistent with that
  sibling file's own noted gap, not a new defect.

## Missing or Weak Tests

See F1 (untested `\r`/`\r\n` path).

## Positive Findings

- The deliberate choice of `lineSpacing(10) ≠ glyphHeight(8)` is a well-designed discriminator that genuinely
  distinguishes "advances by LineSpacing" from the more naive/wrong "advances by glyph height" implementation —
  independently confirmed by hand-deriving both hypotheses and observing the sample points (`y=16`, `y=11`) are
  chosen exactly where the two hypotheses disagree.
- Consistent, minimal fixture reuse pattern with the rest of the batch.

## Final Assessment

A correct, well-designed discriminating test for the newline/LineSpacing advance behavior, with one legitimate
(if minor) coverage gap around `\r`/`\r\n` handling (F1) shared implicitly with the rest of this batch's SpriteFont
sub-suite.
