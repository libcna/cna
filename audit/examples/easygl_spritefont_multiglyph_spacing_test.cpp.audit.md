# Audit: examples/easygl_spritefont_multiglyph_spacing_test.cpp

## Metadata

- Source file: `examples/easygl_spritefont_multiglyph_spacing_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration/pixel-readback test
- File type: C++ executable test (`Game` subclass, no gtest), 141 lines
- XNA/FNA relevance: exercises `SpriteBatch::DrawString(SpriteFont&, string, Vector2, Color)` (3-argument
  overload) and the shared `SpriteFont`/`DrawString` inter-character spacing/kerning-advance logic
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/SpriteBatch.cs` (`DrawString`, ~line
  791-852, the `curOffset.X += spriteFont.Spacing + cKern.X` / `curOffset.X += cKern.Y + cKern.Z` advance logic)
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp:444-505`

## Purpose

Task 425: verifies the horizontal-advance math for a two-glyph string `"AB"` drawn with a two-glyph font ('A'
White, 'B' Green, both 8×8, `kerning=(0,8,0)`, `spacing=4.0f`) at `position=(2,2)`. The header comment
pre-computes: 'A' at `dest=(2,2,8,8)`, then `curOffset.X += 8` (glyph width) after 'A', then `curOffset.X += 4`
(spacing) before 'B' → 'B' at `dest=(14,2,8,8)`, leaving an exact 4px gap at `x ∈ [10,14)`.

## Executive Verdict

**Healthy.** The advance-math prediction was independently re-derived from `SpriteBatch.cpp` and matches exactly;
the check-point design (fixed `y=6`, five distinct `x` probes at the two glyph interiors, the gap midpoint, and
both outer margins) is well-targeted and was verified by direct calculation to actually discriminate a spacing
regression (e.g. `spacing=0` or `spacing=2` instead of `4`), not merely "render something."

## Checklist Results

### API / XNA / FNA parity
Uses the 3-argument `DrawString(SpriteFont&, string, Vector2, Color)` overload (`SpriteBatch.hpp`/`.cpp:378-385`),
which forwards to the full overload with `rotation=0`, `origin=Vector2::Zero`, `scale=(1,1)` — matches FNA's own
overload-forwarding chain.

### Behavioral correctness
Independently re-derived both glyph destinations against `SpriteBatch.cpp:444-505`:
- 'A' (first-in-line): `curOffset.X += abs(cKern.X=0) = 0`; `offsetX = 0 (origin) + (0+0)*(-1) = 0`; `localX =
  -0 = 0`... actually with the 3-arg overload `axisDirX[0]=-1` and `origin=0`, `offsetX=0`, `localX=0`,
  `scaledX=0`; `dest.X = round(2+0) = 2`. `dest.Width = round(8*1) = 8` → `x ∈ [2,10)`. Matches header comment.
- After 'A': `curOffset.X += cKern.Y(8) + cKern.Z(0) = 8`.
- 'B' (not first-in-line): `curOffset.X += spacing(4) + cKern.X(0) = 12`; `offsetX = 0 + (12+0)*(-1) = -12`;
  `localX = 12`; `dest.X = round(2+12) = 14`. `dest.Width = 8` → `x ∈ [14,22)`. Matches header comment exactly.
- **Verified the gap-detection check is discriminating**, not just descriptive: if `spacing` were regressed to
  `0`, 'B' would advance to `curOffset.X=8` → `dest.X = round(2+8) = 10` → `x ∈ [10,18)`, which would place the gap
  probe `(12,6)` **inside** the (buggy) Green 'B' glyph — the test's `check(colourMatch(px,{12,6}, kBlack))`
  would then read Green and correctly report `[FAIL]`. If `spacing` were regressed to `2` instead of `4`,
  `dest.X = round(2+10) = 12` → `x ∈ [12,20)` — the gap probe at exactly `x=12` becomes 'B's own left edge, still
  correctly flips the check to FAIL. This test genuinely proves `spacing=4`, not merely "some positive spacing."

### Logic
The atlas construction (lines 71-77, `16×8`, left half White for 'A', right half Green for 'B') and the
`glyphBounds`/`cropping`/`kerning` vectors (lines 79-82) are internally consistent and match the
`SpriteFont::SpriteFont` constructor's expected "one entry per glyph, same length across all four vectors"
contract (`SpriteFont.hpp:33`).

### Memory/resource lifetime
Same pattern as the sibling files in this batch: `unique_ptr` members outlive the single `Draw()` call; no
dangling-pointer risk. `Texture2D` copy into `SpriteFont`'s constructor is a cheap shared-backend copy (verified
in the sibling report for `easygl_spritefont_effects_rotation_scale_test.cpp`; applies identically here).

### Testing
All five check points (`(6,6)`, `(18,6)`, `(12,6)`, `(0,6)`, `(26,6)`) share a fixed `y=6`, varying only `x` — this
avoids the diagonal-corner weakness identified in the sibling `..._rotation_scale_test.cpp` report entirely; each
check independently isolates an X-axis claim. The gap probe at `(12,6)` sits at the exact midpoint of the
predicted 4px gap (`x ∈ [10,14)`), which — as shown above — is tight enough to catch even a 2px spacing
regression, not just a total-removal regression.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. Two minor observations below.

### F1 — `colourMatch`'s tol=40 would tolerate a partially-blended edge pixel as a false pass, though none of this test's probes are near enough an edge for that to matter here

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage / robustness
- Location/symbol: `colourMatch` (lines 43-48); check points (lines 102-108)
- Evidence: every check point in this file sits at least 2px from the nearest glyph edge (the tightest is the gap
  probe at `x=12`, 2px from both 'A's right edge at `x=10` and 'B's left edge at `x=14`), so point-sampling with
  no anti-aliasing (this is an unrotated, axis-aligned, nearest-neighbor-style glyph blit) means there is no
  partial-coverage pixel at any probed location in the current, correct implementation.
- Why it matters: purely a note for future test additions in this family — a probe placed exactly on a glyph edge
  (0px margin) combined with `tol=40` could mask a 1-pixel-off boundary if the backend ever introduces
  edge-antialiasing; not a defect in this file today.
- FNA/XNA comparison: N/A — this is a test-design observation, not a parity question.
- Suggested action: none needed for this file; flagged only as context for any future test in this family that
  narrows the margin.

## Cross-File Observations

- This file's check-point design (fixed non-varying axis, probe placed at the exact midpoint of the
  gap/feature being asserted) is the same rigorous pattern used by `easygl_spritefont_newline_test.cpp` (also in
  this batch) and stands in useful contrast to the diagonal-corner weakness found in
  `easygl_spritefont_effects_rotation_scale_test.cpp`.

## Missing or Weak Tests

- Does not exercise `spacing` combined with a non-default `origin`/`scale`/`rotation` (covered separately, without
  spacing being multi-value, by the rotation/scale sibling test) — a combined test would strengthen confidence
  that spacing composes correctly with those transforms, but this is a reasonable, deliberate scope split, not an
  oversight.

## Positive Findings

- The test's own header comment's pre-computed expected values were independently re-derived and match the actual
  `SpriteBatch::DrawString` implementation exactly.
- The check design was verified, via explicit substitution of plausible regressed `spacing` values, to actually
  discriminate correct from incorrect behavior — a genuinely load-bearing regression test, not a "compiles and
  doesn't crash" placeholder.

## Final Assessment

A well-designed, correctly-targeted regression test whose predicted pixel layout was independently confirmed
against the production `DrawString` advance-math, and whose check points were shown by direct calculation to
actually catch plausible spacing regressions (including partial ones, not just complete removal).
