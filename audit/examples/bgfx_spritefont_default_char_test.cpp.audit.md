# Audit: examples/bgfx_spritefont_default_char_test.cpp

## Metadata

- Source file: `examples/bgfx_spritefont_default_char_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SpriteFont`'s `DefaultCharacter` fallback pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_spritefont_default_char …)` /
  `cna_register_backend_test(NAME Bgfx_SpriteFont_DefaultCharacterFallback …)`,
  `cmake/Tests/BgfxTests.cmake:828-830`).
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::SpriteFont::DefaultCharacter` and
  `SpriteBatch::DrawString`'s unresolved-character fallback behavior.
- FNA reference: `Graphics/SpriteFont.cs` lines 167-180/263-276 — `if (!DefaultCharacter.HasValue) throw
  new ArgumentException(...); index = characterIndexMap[DefaultCharacter.Value];` — CNA's own
  `SpriteBatch.cpp:461-466` (`if (!spriteFont.defaultCharacter_.has_value()) throw std::invalid_argument(...);
  it = spriteFont.characterIndexMap_.find(spriteFont.defaultCharacter_.value());`) matches this exactly
  (`std::invalid_argument` in place of FNA's `ArgumentException`, an acceptable C++ substitution per this
  project's established exception-mapping convention).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp` (constructor/`defaultCharacter_`
  storage), `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`DrawString`, lines 400-497).

## Purpose

4-check pixel test (Task 811, a Bgfx port of Task 427's EasyGL/Vulkan test) proving `SpriteFont`'s
`defaultCharacter` fallback actually renders the fallback glyph, not just avoids throwing or silently rendering
nothing. A two-glyph font is built ('A' White, '?' Red, the latter configured as `defaultCharacter`); the string
drawn is `"Z"`, a character present in neither the font's character list — an unresolvable code point distinct
from both real glyphs. Checks: (1) `DrawString("Z")` with a configured `defaultCharacter` must not throw; (2)
the fallback glyph's own pixel region must read Red ('?' 's color, not 'A' 's); (3)/(4) pixels outside the
glyph's footprint must remain the clear background color, confirming the fallback glyph is drawn at the
position 'Z' would have occupied (not somewhere else, and not oversized).

## Executive Verdict

**Healthy** — traced `SpriteBatch::DrawString`'s unresolved-character branch
(`SpriteBatch.cpp:459-466`) line-by-line against FNA's `SpriteFont.cs` fallback logic and confirmed they match
exactly (throw only when no `defaultCharacter` is configured; otherwise substitute its `characterIndexMap_`
index and proceed with that glyph's real bounds/cropping/kerning), and independently re-derived all 4 expected
check outcomes from the test's own atlas/glyph-table setup.

## Checklist Results

### API / XNA / FNA parity
`SpriteFont` constructor overload taking `glyphBounds`, `cropping`, `characters`, `lineSpacing`, `spacing`,
`kerning`, and `std::optional<SharpRuntime::charcs> defaultCharacter` matches FNA's `SpriteFont(Texture2D,
List<Rectangle>, List<Rectangle>, List<char>, int, float, List<Vector3>, char?)` constructor's parameter shape
and order, including the nullable `char?` → `std::optional<charcs>` mapping.

### Behavioral correctness
- Font setup: 16×8 atlas, left half White ('A', index 0), right half Red ('?', index 1);
  `glyphBounds=[(0,0,8,8),(8,0,8,8)]`, `cropping=[(0,0,8,8),(0,0,8,8)]` (zero offset for both), `characters=
  ['A','?']`, `defaultCharacter='?'`.
- `DrawString(font, "Z", (2,2), White)`: `'Z'` is not in `characterIndexMap_` → per `SpriteBatch.cpp:461-466`,
  since `defaultCharacter_` has a value, `it` is redirected to `'?'`'s index (1), using **that** glyph's
  `cCrop`/`cGlyph`/`cKern` — not a hardcoded "do nothing" or a generic error glyph.
- Reproduced the destination-rectangle computation for this substituted glyph: with `curOffset=(0,0)`
  (first-and-only character), `cCrop=(0,0,8,8)`, `axisDirX[0]=axisDirY[0]=-1` (no `SpriteEffects`), `offsetX =
  0 + (0+0)*(-1) = 0`, `offsetY = 0` ⇒ `localX=localY=0` ⇒ `dest = (position.X+0, position.Y+0, cGlyph.Width=8,
  cGlyph.Height=8) = (2,2,8,8)`. The *source* rectangle passed to `pushSprite` is `cGlyph = (8,0,8,8)` — the
  atlas's own right/Red half, confirming the sampled pixels are genuinely Red, not merely a coincidentally-red
  destination-rectangle fill.
- Verified all 4 checks against this derived geometry: (6,6) is inside `[2,10)×[2,10)` → Red (matches); (0,0)
  and (13,13) are both outside that 8×8 box on a 16×16 backbuffer cleared to Black → Black background (matches
  both); the no-throw check is trivially true since `defaultCharacter_` is set.

### Logic
The test's separate, non-`RunCheck` no-throw probe (lines 116-129) reasonably omits the 20-iteration retry loop
used by the pixel-reading checks, since it never calls `GetBackBufferData` and is therefore unaffected by
Bgfx's documented "first read per frame" readback quirk — a deliberate, correct asymmetry, not an inconsistency.

### Robustness
This test specifically isolates "an unresolvable character falls back to the glyph, not a thrown exception and
not nothing" — a genuinely three-way discrimination (throw vs. silent-no-render vs. correct-fallback-render),
which is a meaningfully stronger test than a bare "DrawString doesn't crash" smoke test.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW defects found in this file.

## Missing or Weak Tests

- The complementary case — `DrawString` with an **unresolvable** character and **no** `defaultCharacter`
  configured, which per both FNA and CNA must throw (`std::invalid_argument` /
  `System.ArgumentException`) — is not exercised in this file. This is a reasonable single-file scope choice
  (the throw path is presumably covered by a dedicated `SpriteFont`/`SpriteBatch` unit test elsewhere in the
  test tree), but it means this specific Bgfx pixel-test file alone does not close the loop on both branches of
  the `if (!defaultCharacter_.has_value())` condition it exercises.

## Positive Findings

- The fallback-glyph destination/source rectangle computation was independently re-derived from
  `SpriteBatch::DrawString`'s actual source and matches the test's expected pixel outcomes exactly — this is
  not a case of the test merely asserting whatever the code happens to currently produce; the geometry is
  small enough to hand-verify from first principles, which this audit did.
- CNA's exception-substitution (`std::invalid_argument` for FNA's `ArgumentException`) at the exact same logical
  point in the algorithm (glyph lookup miss with no fallback configured) is a faithful, intentional mapping,
  not a divergence in when/whether the throw happens.

## Final Assessment

A precise, correctly-derived test for `SpriteFont`'s default-character fallback; production code
(`SpriteBatch::DrawString`) matches FNA's `SpriteFont.cs` fallback logic exactly, and all 4 check outcomes were
independently confirmed against the test's own font/atlas setup.
