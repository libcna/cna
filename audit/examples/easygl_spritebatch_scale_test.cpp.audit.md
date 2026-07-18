# Audit: examples/easygl_spritebatch_scale_test.cpp

## Metadata

- Source file: `examples/easygl_spritebatch_scale_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — SpriteBatch scalar/`Vector2` scale overload pixel test
  ("Task 418")
- File type: single-frame `Game`-subclass pixel-readback executable (hand-rolled `main()`)
- XNA/FNA relevance: exercises `SpriteBatch::Draw(texture, position, sourceRectangle, color,
  rotation, origin, scale, effects, layerDepth)` for both the `float` and `Vector2` scale overloads —
  a real XNA API surface (FNA's `SpriteBatch.cs` has both overloads too).
- Build/registration: `cmake/Tests/EasyGLTests.cmake` → `cna_test_easygl_spritebatch_scale`
  (`EasyGL_SpriteBatch_ScaleOverloads`); also reused verbatim by
  `cmake/Tests/VulkanTests.cmake` → `cna_test_vulkan_spritebatch_scale`.
- Main related production file: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`, specifically
  the two `Draw(texture, Vector2 position, std::optional<Rectangle> sourceRectangle, ...)` overloads
  taking a `float scale` (lines 296-312) and a `Vector2 scale` (lines 314-330).

## Purpose

Verifies that both the scalar-`float` and non-uniform-`Vector2` scale overloads of
`SpriteBatch::Draw` actually resize the drawn sprite's destination rectangle by the given factor(s),
relative to the *source* rectangle's dimensions — not the destination staying at the source's native
size, and not a bug that only applies one axis's factor to both dimensions.

## Executive Verdict

**Healthy.** Both overloads' actual scaling arithmetic in `SpriteBatch.cpp` were read directly and
match the test's expected geometry exactly; the two Draw-2 check points are specifically chosen to
rule out the two most likely non-uniform-scale bugs (X-for-both, Y-for-both), which is good test
design, not just "does it compile."

## Checklist Results

### API / XNA / FNA parity
Confirmed `SpriteBatch::Draw(texture, Vector2 position, std::optional<Rectangle> sourceRectangle,
Color color, float rotation, Vector2 origin, float scale, SpriteEffects effects, float layerDepth)`
(`SpriteBatch.cpp` lines 296-312) computes:
```
dw = src.Width, dh = src.Height  (since sourceRectangle is std::nullopt here, dw/dh = texture w/h)
destRect = Rectangle(position.X, position.Y, dw*scale, dh*scale)
```
and the sibling `Vector2 scale` overload (lines 314-330) computes `dw*scale.X, dh*scale.Y`
independently per axis — confirmed these are two genuinely separate code paths (not one delegating
arithmetic to the other in a way that could silently collapse non-uniform scale), each doing its own
multiplication.

### Behavioral correctness
Re-derived both expected destination rectangles directly from the source: Draw 1 (scalar,
`scale=3.0f`, source texture 20×20, `sourceRectangle=std::nullopt` → `dw=dh=20`):
`destRect = (50,50, 60,60)` → screen `x:[50,110), y:[50,110)` — matches the file's own stated
expectation (lines 12-13). Draw 2 (`Vector2(2.0f,4.0f)`, same 20×20 source): `destRect =
(200,50, 40,80)` → `x:[200,240), y:[50,130)` — matches lines 13-14. Both independently confirmed
against `SpriteBatch.cpp`'s actual arithmetic, not just restated from the comment.

### Logic
Check-point design (lines 107-114) specifically targets 2 plausible non-uniform-scale
implementation bugs: `(220,110)` (inside the *correct* 40×80 box but *outside* a hypothetical
40×40 "scale.X used for both axes" box, since the correct box's bottom edge is `y=130` but the
buggy box's would be `y=90`, and `110>90`) and `(245,70)` (outside the *correct* box's right edge at
`x=240` but *inside* a hypothetical 80×80 "scale.Y used for both axes" box, whose right edge would be
at `x=280`). This is a well-reasoned, code-aware test design — it doesn't just check "is the sprite
roughly the right size," it specifically baits the two most likely copy-paste bugs
(`scaleX` reused for `scaleY` or vice versa) into producing a wrong-color result at a chosen point.

### Memory/resource lifetime
`tex_` (20×20 solid red) constructed once in `Initialize()` — no lifetime issues.

### C++ correctness
`colourMatch` (lines 52-57) correctly widens to `int` before subtracting — no unsigned-underflow risk.

### Performance
N/A — single-frame test, tiny 400-pixel texture upload.

### Thread safety
N/A.

### Architecture
No backend-specific code — pure `Microsoft::Xna::Framework::Graphics` API usage, consistent with the
shared-source-across-backends pattern already confirmed for sibling files in this shard.

### Maintainability
Header comment (lines 1-29) explicitly names the two bugs the check points are designed to catch
(lines 16-18) — unusually explicit about *why* each sample point was chosen, which is exactly the
kind of self-documenting rationale that makes a test auditable without re-deriving intent from
scratch.

### Portability
N/A.

### Robustness
`result_` defaults to `0` (pass) — same minor project-wide pattern noted across this shard (see
Cross-File Observations in sibling reports); not a live risk given this file's single-frame control
flow.

### Testing
This file is itself a test. See Missing or Weak Tests for a residual gap.

## Detailed Findings

No correctness defects found in this file.

## Cross-File Observations

- Same recurring `const_cast<SamplerState*>(&SamplerState::PointClamp)` pattern (line 94) noted in
  the `rotation`/`rotation_golden`/`layerdepth`/`sourcerect` sibling reports — the root cause
  (`SpriteBatch::Begin`'s `samplerState` parameter being non-`const`) lives in `SpriteBatch.hpp`,
  outside this shard.
- Verbatim cross-backend reuse confirmed again here: `cmake/Tests/EasyGLTests.cmake` line 786
  (`cna_test_easygl_spritebatch_scale`) and `cmake/Tests/VulkanTests.cmake` line 758
  (`cna_test_vulkan_spritebatch_scale`) both build this identical source file.

## Missing or Weak Tests

- Only the `std::optional<Rectangle>{}` (whole-texture) source-rectangle path is exercised for both
  scale overloads; a combination of a non-trivial `sourceRectangle` *and* non-uniform scale together
  (i.e., scale applied to a cropped source rect's own width/height, not the whole texture's) is not
  covered by this file — `easygl_spritebatch_sourcerect_test.cpp` (audited separately in this batch)
  covers cropping without scale, and this file covers scale without cropping, but the combination of
  both in one draw call is untested by either.
- Negative/zero scale (e.g. `scale=-1.0f` to mirror via negative scale, or `scale=0.0f` degenerate
  case) is not exercised — FNA does not special-case negative scale in its own `Draw` overloads (it
  flows straight into the same destination-rectangle-width arithmetic), so CNA's behavior for a
  negative destination width is unverified by any test in this shard.

## Positive Findings

- Both scale-overload arithmetic paths independently confirmed correct by direct inspection of
  `SpriteBatch.cpp`, not merely restated from the test's own comment.
- Check-point placement specifically baits the two most likely per-axis-scale implementation bugs —
  a materially stronger test design than a single "is it roughly bigger" sample point.

## Final Assessment

A correct, well-targeted test for both `SpriteBatch::Draw` scale overloads; the destination-rectangle
arithmetic matches the real production code exactly, and the chosen sample points specifically
discriminate the two most plausible non-uniform-scale bugs. No defects found; the main opportunity is
additional coverage combining scale with a cropped source rectangle (currently untested by any single
file in this shard).
