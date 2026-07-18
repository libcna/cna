# Audit: examples/sdlrenderer_spritefont_default_char_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritefont_default_char_test.cpp` (161 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteFont.DefaultCharacter` fallback pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_spritefont_default_char` /
  `SDL_Renderer_SpriteFont_DefaultChar`, `cmake/Tests/SdlRendererTests.cmake:187-189`), introduced by
  `18e5e012`/`4555b9f4` ("test(Task 693): verify SpriteFont default character fallback on SDL_Renderer").
- XNA/FNA relevance: direct — `SpriteFont.DefaultCharacter`, the unresolved-character branch of
  `SpriteBatch.DrawString`.
- FNA reference: `Graphics/SpriteFont.cs`/`SpriteBatch.cs`: `if (!characterIndexMap.TryGetValue(c, out index)) {
  if (!DefaultCharacter.HasValue) throw new ArgumentException(...); index = characterIndexMap[DefaultCharacter.
  Value]; }`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`DrawString`, lines 457-464),
  `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp` (constructor, `defaultCharacter_` storage, lines 12-34).

## Purpose

`SdlSpriteFontDefaultCharTest` (lines 62-154) builds a two-glyph font — `'A'` (white, the font's only "real"
character) and `'?'` (red, the configured `defaultCharacter`) — then draws the string `"Z"`, a character in
*neither* the font's character list (unlike a merely-unassigned-but-declared glyph). Since `'Z'` is unresolvable
and a `defaultCharacter` *is* configured, `DrawString` must render `'?'`'s glyph (red) at the position `'Z'` would
have occupied, rather than throwing or silently rendering nothing.

## Executive Verdict

**Healthy**, with one real, non-self-disclosed test-coverage gap (F1): the two glyphs share numerically identical
kerning/cropping metrics in this fixture, so the test cannot distinguish "the fallback correctly reuses the
resolved index for kerning/cropping too" from "the fallback only correctly picks the right *source rectangle*
while some other code path still used `'Z'`'s (nonexistent) metrics."

## Checklist Results

### API / XNA / FNA parity

The constructor's final argument, `std::optional<SharpRuntime::charcs>(u'?')` (line 93), maps to FNA's own
`SpriteFont` constructor's `char? defaultCharacter` parameter and `DefaultCharacter` property — `std::optional`
is the correct, idiomatic C++ mapping for a C# nullable value type here (consistent with the project's general
practice of using `std::optional<T>` for C# `T?`). The resolution logic this test exercises
(`SpriteBatch.cpp:457-464`) is byte-for-byte structurally identical to FNA's `SpriteFont.cs`/`SpriteBatch.cs`
`TryGetValue`-then-fallback-to-`DefaultCharacter` pattern, including throwing (here `std::invalid_argument`,
matching FNA's `ArgumentException`) only when *no* `defaultCharacter_` is configured — this test correctly
exercises the "IS configured" branch (asserts no throw), while a hypothetical sibling test would need to exist for
the "NOT configured, must throw" branch (not found anywhere in this batch of 8 files — see F2).

### Behavioral correctness

Re-derived by hand (`effects=None`, `origin=(0,0)`, `scale=(1,1)`, `position=(2,2)`): `'Z'` is looked up in
`characterIndexMap_`, misses, falls back to `characterIndexMap_.find(defaultCharacter_.value())` → resolves to
index 1 (`'?'`). `kerning_[1]=(0,8,0)`, `cropping_[1]=(0,0,8,8)`, `glyphData_[1]=(8,0,8,8)` (the atlas's red
half). `curOffset.X` (firstInLine): `+= abs(0) = 0`. `offsetX = 0 + (0+0)*(-1) = 0`; `dest.X = round(2+0) = 2`;
`dest.Y = round(2+0) = 2`; `dest.Width = round(cGlyph.Width(8)*1) = 8`. `dest = (2,2,8,8)`. The check at `(6,6)`
(inside `[2,10)`) correctly expects red — this audit independently confirms the fallback glyph's *source
rectangle* (the visually-distinguishing half of the atlas) is correctly resolved to `'?'`'s red region, not
`'A'`'s white one, and lands at the position `'Z'` would have occupied had it existed.

### Logic

The "does not throw" check (lines 105-120, wrapping `Begin`/`DrawString`/`End` in a try/catch) correctly targets
the specific claim in the file's own purpose statement — that a *configured* `defaultCharacter` must prevent the
`std::invalid_argument` throw that would otherwise occur for an unresolvable character.

### C++ correctness

`std::optional<SharpRuntime::charcs>(u'?')` (line 93) and `std::vector<SharpRuntime::charcs>` for the two-entry
character list are both used correctly; no unsafe casts.

### Memory/resource lifetime

Single-shot `Initialize()`/`Draw()`, no lifetime concerns.

### Robustness

Checks pixels both inside the fallback glyph and at two distinct outside-corner points (lines 122-127) — same
positive four/three-point boundary-checking pattern used across this batch's sibling SpriteFont tests.

### Testing

This file is itself a test. See F1/F2 for two coverage gaps discovered by independent re-derivation.

## Detailed Findings

No CRITICAL/HIGH findings. One MEDIUM-adjacent (scored LOW given production code is independently confirmed
correct elsewhere) test-coverage finding and one LOW finding:

### F1 — Test cannot independently confirm the fallback index also drives kerning/cropping (metrics), only the glyph's colour/source-rect selection

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `kerning = { Vector3(0,8,0), Vector3(0,8,0) }` (line 90), `cropping = { Rectangle(0,0,8,8),
  Rectangle(0,0,8,8) }` (line 88) — both glyphs (`'A'` at index 0, `'?'` at index 1) share *identical* kerning and
  cropping values; only `glyphData_` (the atlas source rect, used purely for texture sampling/colour) differs
  between the two indices.
- Evidence: `DrawString`'s fallback (`SpriteBatch.cpp:463`) resolves `it = characterIndexMap_.find(
  defaultCharacter_.value())`, then uses `it->second` for *all four* of `kerning_[index]`, `croppingData_[index]`,
  `glyphData_[index]` (lines 465, 467, 478-479) — a hypothetical bug that correctly swapped only the glyph's
  *source rectangle* to `'?'`'s (giving the right colour) while still using stale/wrong `kerning_`/`croppingData_`
  values from some other index (or from `'Z'`'s nonexistent, uninitialized slot) would be invisible to this test,
  because both real indices happen to carry the same kerning/cropping numbers.
- Why it matters: this test's own claim ("must fall back to '?' 's glyph ... at the position 'Z' would have
  occupied") is only partially independently verified — the *position* half of that claim is verified only up to
  the coincidence that both glyphs share identical layout metrics, not because the test forces a scenario where a
  metrics mix-up would produce a visibly different (and therefore detectable) destination rect.
- FNA/XNA comparison: N/A — this is a test-authoring observation, not an XNA/FNA behavior question; the resolved-
  index-drives-everything design in `DrawString` itself was independently confirmed correct by inspection
  (single `index` variable feeds all three lookups, `SpriteBatch.cpp:465-479`), so this is a "the test happens not
  to be able to prove it" finding, not "the code is wrong."
- Related files: none — the fix (if desired) is local: give `'A'` and `'?'` distinct kerning/cropping values (e.g.
  a different left-bearing) so a metrics mix-up would shift the destination rect measurably.
- Suggested future action (not implemented by this audit): give the two glyphs distinct kerning/cropping values
  in a future revision of this fixture to make the metrics-resolution claim independently falsifiable.

### F2 — No sibling test in this batch exercises the "unresolved character AND no `defaultCharacter` configured" throw path

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `SpriteBatch.cpp:460-462` (`if (!spriteFont.defaultCharacter_.has_value()) throw std::
  invalid_argument(...)`) has no exercising test identified anywhere in this batch's 8 files.
- Evidence: every `SpriteFont` constructed across this entire batch either passes `std::optional<charcs>(
  std::nullopt)` as `defaultCharacter` (the other 5 SpriteFont files) while only ever drawing characters that
  *are* in that font's character list (so the "unresolved + no fallback" throw path is simply never reached), or
  (this file) passes a real fallback character and only tests the "IS configured" branch.
- Why it matters: the `std::invalid_argument` throw path is real, user-visible XNA-parity behavior (matches FNA's
  own `ArgumentException`) and currently has zero pixel/behavioral coverage in the SDL_Renderer shard as far as
  this batch shows; a regression that silently swallowed the exception, or changed its type, would go undetected
  here (the mock-backend `tests/Microsoft/Xna/Framework/Graphics/SpriteFontTests.cpp`/`SpriteBatchTests.cpp`, if
  they exist, are out of this batch's scope and were not inspected).
- FNA/XNA comparison: FNA's own `SpriteFont.cs`/`SpriteBatch.cs` throw `ArgumentException` for exactly this case;
  CNA's `std::invalid_argument` is a reasonable C++ mapping (an `std::logic_error` subclass), consistent with the
  project's general exception-mapping conventions.
- Related files: none in this batch; worth checking the `tests-xna-graphics` shard for `SpriteFont`/`SpriteBatch`
  unit-level coverage of this specific throw.
- Suggested future action (not implemented by this audit): none within this batch's scope; flagged for
  cross-referencing against the `tests-xna-graphics` shard's own audit.

## Cross-File Observations

- Shares the "no `defaultCharacter`" gap (F2) with all 5 other SpriteFont files in this batch — every one of them
  constructs its font with `std::optional<charcs>(std::nullopt)` and never draws an unresolvable character, so
  none of the 6 SpriteFont pixel tests in this shard currently exercises the throw path at all.

## Missing or Weak Tests

See F1 (metrics-mixup-detection gap, this file specifically) and F2 (no-defaultCharacter-throw gap, shared across
the whole batch).

## Positive Findings

- Correctly targets a genuinely distinct failure mode ("silently render nothing" or "throw despite a configured
  fallback") from the sibling tests in this batch, and the file's own header comment explicitly frames this as
  matching the project's established "must not silently misrender" convention (citing Tasks 675/676/681).
- The "does not throw" assertion is a real, meaningful behavioral check (not just "compiles"), independently
  confirmed against the actual `DrawString` resolution logic.

## Final Assessment

A correctly-targeted fallback test whose core claim (fallback selects the right glyph, at the right position, and
does not throw) was independently re-derived and confirmed against production code — weakened only by two
honestly-scoped coverage gaps (F1: can't detect a metrics-specific mixup because both glyphs share identical
metrics; F2: no test anywhere in this batch exercises the no-defaultCharacter throw path).
