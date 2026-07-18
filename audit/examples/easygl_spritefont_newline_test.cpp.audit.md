# Audit: examples/easygl_spritefont_newline_test.cpp

## Metadata

- Source file: `examples/easygl_spritefont_newline_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest), 131 lines
- XNA/FNA relevance: exercises `SpriteBatch::DrawString`'s `'\n'` handling
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/SpriteBatch.cs` (`DrawString`, ~line
  766-772: `if (c=='\n') { curOffset.X=0; curOffset.Y += spriteFont.LineSpacing; firstInLine=true; continue; }`)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp:448-455`

## Purpose

Task 426: verifies that a `'\n'` inside a `DrawString` call advances the next line's Y position by the font's
`lineSpacing_` (deliberately configured to `10`, distinct from the glyph's own `8px` height, per the header
comment's own stated rationale — "so a bug that advances by glyph height instead of the font's real lineSpacing_
would be caught"). Drawing `"A\nA"` at `(2,2)` should place the two glyphs at `y ∈ [2,10)` and `y ∈ [12,20)`
respectively, with a 2px gap at `y ∈ [10,12)`.

## Executive Verdict

**Healthy.** The `lineSpacing`-vs-`glyphHeight` distinction is a deliberately chosen, effective test design (a
lazy/incorrect implementation that advanced by glyph height instead of the real field would be caught, not
coincidentally pass), and the newline-handling logic itself was traced against `SpriteBatch.cpp` and matches
exactly, including confirming the decoder correctly advances past `'\r'`/`'\n'` without an infinite loop.

## Checklist Results

### API / XNA / FNA parity
Uses the 3-argument `DrawString(SpriteFont&, string, Vector2, Color)` overload, same as its sibling
`easygl_spritefont_multiglyph_spacing_test.cpp` — forwards to `rotation=0`, `origin=Zero`, `scale=(1,1)`.

### Behavioral correctness
Traced `SpriteBatch.cpp:444-455` for the `'\n'` branch: `curOffset.X = 0.0f; curOffset.Y +=
static_cast<float>(spriteFont.lineSpacing_); firstInLine = true; continue;` — matches FNA's equivalent branch
verbatim in behavior (reset X, add the font's own configured `lineSpacing`, not a hardcoded or glyph-derived
value, reset `firstInLine`).
Re-derived both glyph placements:
- 1st 'A' (`firstInLine=true` initially): `curOffset=(0,0)` after the first-in-line X bump (`abs(kern.X=0)=0`);
  `dest = (round(2+0), round(2+0), 8, 8) = (2,2,8,8)` → `y ∈ [2,10)`. Matches header comment.
- `'\n'`: `curOffset = (0, 0+10) = (0,10)`; `firstInLine=true`.
- 2nd 'A' (`firstInLine=true` again): `curOffset.X` bump is still `abs(0)=0`, so `curOffset=(0,10)` unchanged;
  `dest = (round(2+0), round(2+10), 8, 8) = (2,12,8,8)` → `y ∈ [12,20)`. Matches header comment exactly, and
  correctly reflects that `lineSpacing_=10` (not the glyph's own `8px` height) drove the Y advance.
- **Verified the gap-detection check is discriminating**: if a hypothetical bug advanced by glyph height (`8`)
  instead of `lineSpacing_` (`10`), the 2nd line would land at `dest.Y = 2+8 = 10` → `y ∈ [10,18)`, which places
  the gap probe `(6,11)` **inside** the (buggy) 2nd-line glyph — the check `colourMatch(sample(6,11), kBlack)`
  would then read White and correctly report `[FAIL]`. This is precisely the bug class the test's own header
  comment describes, and it was independently confirmed here to actually be caught, not just plausibly caught.

### Logic
Also traced the decoder call `CNA::Internal::DecodeUtf8CodePoint(text, i)` (`SpriteBatch.cpp:446`, declared in
`include/CNA/Internal/Utf8Decode.hpp:27` taking `index` by reference) to confirm the `for (...;)`-with-no-increment
loop (`SpriteBatch.cpp:444`) does not infinite-loop on the `'\r'` branch (`continue` with no explicit `++i`,
line 448): `i` is already advanced by the decoder call itself before `c` is known, so `continue` safely resumes
at the next code point. This file's own string (`"A\nA"`) never contains `'\r'`, so this path isn't directly
exercised by this test, but it is relevant shared-code context for the newline-adjacent branch this file *does*
exercise.

### Memory/resource lifetime
Same pattern as the batch's other `SpriteFont` tests: `unique_ptr` members outlive the single `Draw()` call.

### Testing
All four check points (`(6,6)`, `(6,16)`, `(6,11)`, `(6,22)`) share a fixed `x=6`, varying only `y` — an
axis-isolated design avoiding the diagonal-corner weakness identified in the sibling
`easygl_spritefont_effects_rotation_scale_test.cpp` report. The gap probe `(6,11)` sits at the exact midpoint of
the predicted 2px gap, tight enough (shown above) to catch even the specific "advanced by glyph height instead of
lineSpacing" bug this test was purpose-built to guard against.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings for this file.

### F1 — Single-line-only baseline: an X-axis regression coincident with a correct Y-axis newline advance would not be independently caught by this file

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: `EasyGLSpriteFontNewlineTest::Draw` (checks, lines 93-98)
- Evidence: all four checks use `x=6` for every probe; there is no check verifying that `curOffset.X` is actually
  reset to `0` after the newline (as opposed to, say, continuing to accumulate) using a case where the reset
  would matter (e.g. a 2nd line with 2+ glyphs where a stale non-zero `curOffset.X` would visibly misplace the
  2nd character). With a single glyph per line, `curOffset.X` starts at 0 for the first-in-line bump regardless
  of whether the explicit `curOffset.X = 0.0f;` reset (line 451) ran or not, since `firstInLine` handling
  (`curOffset.X += abs(cKern.X)`, line 470) would itself only ever add on top of whatever `curOffset.X` was
  carried over.

  Concretely: if the `curOffset.X = 0.0f;` reset were removed from the newline branch (a real, plausible
  regression), `curOffset.X` would carry over from wherever the first line left it (`8` here, from the first `'A'`'s
  own post-glyph advance `curOffset.X += cKern.Y+cKern.Z = 8`), and then the first-in-line bump for the 2nd 'A' would
  compute `curOffset.X += abs(kern.X=0) = 0` — leaving `curOffset.X = 8` (not `0`), because that bump *adds to* the
  existing value rather than starting fresh. That would shift the 2nd 'A' from `x ∈ [2,10)` to `x ∈ [10,18)`. This
  file's checks all sample `x=6`, which is **inside the correct 2nd-line position but outside the buggy shifted
  one** — so this specific reset-removal regression **would actually be caught** (the `(6,16)` check would read
  Black instead of the expected White, and correctly FAIL). Re-examined after working through the numbers: this
  file's single-glyph-per-line design does still, coincidentally, catch the specific X-reset regression via its
  existing Y-axis checks, because the buggy X-shift moves the glyph away from the fixed `x=6` probe. Downgraded
  from an initial higher-severity read to LOW/MEDIUM-confidence, since the concrete regression scenario checked
  here does not in fact slip through — the note is retained only because a 2nd-line string of 2+ characters would
  give a more direct, less coincidental proof of the X-reset behavior specifically (as opposed to relying on the
  side effect of the same check that also verifies Y placement).
- Why it matters: primarily a design-clarity note — the current single-glyph-per-line test happens to also catch
  an X-reset regression as a side effect of its Y-position check, which is fragile as a testing *strategy* even
  though it isn't a proven gap for the exact scenario worked through above.
- FNA/XNA comparison: N/A.
- Suggested action (not implemented by this audit): consider a 2-glyph-per-line variant (e.g. `"AB\nAB"`) to
  directly and unambiguously prove the X-reset, independent of the Y-position check.

## Cross-File Observations

- Shares the axis-isolated check-point design with `easygl_spritefont_multiglyph_spacing_test.cpp`; both avoid the
  diagonal-corner weakness found in `easygl_spritefont_effects_rotation_scale_test.cpp`.

## Missing or Weak Tests

- No test in this file (or, as far as this batch's scope shows, elsewhere) exercises 3+ lines, or a line-spacing
  value smaller than the glyph height (which would additionally stress overlapping-glyph-line scenarios) — a
  reasonable, deliberate scope limit for a single focused regression test, not an oversight.

## Positive Findings

- The specific bug this test is designed to catch ("advance by glyph height instead of `lineSpacing_`") was
  independently confirmed, via substitution, to actually be caught by the existing gap-probe check — a genuinely
  effective, purpose-built regression test.
- Correctly distinguishes `lineSpacing` (a font-level property) from glyph height by deliberately choosing
  different values (`10` vs `8`) — exactly the kind of "wouldn't coincidentally pass" test design this audit
  looks for.

## Final Assessment

A well-targeted, correctly-implemented regression test for `DrawString`'s newline-advance branch. Its predicted
pixel layout was independently re-derived and matches the production code exactly, and the specific historical bug
class it targets (glyph-height-based advance instead of the font's real `lineSpacing_`) was confirmed, by direct
substitution, to actually be caught by its check design.
