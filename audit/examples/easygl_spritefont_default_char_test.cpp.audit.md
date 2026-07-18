# Audit: examples/easygl_spritefont_default_char_test.cpp

## Metadata

- Source file: `examples/easygl_spritefont_default_char_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SpriteFont`/`SpriteBatch::DrawString` default-character
  fallback pixel test ("Task 427")
- File type: single-frame `Game`-subclass pixel-readback executable (hand-rolled `main()`)
- XNA/FNA relevance: exercises `SpriteBatch::DrawString`'s unresolved-character fallback branch and
  `SpriteFont::DefaultCharacter` — matches FNA's `SpriteBatch.cs` `DrawString`
  (`characterIndexMap.TryGetValue` / `DefaultCharacter.HasValue` branch, `SpriteBatch.cs` lines
  774-789).
- Build/registration: `cmake/Tests/EasyGLTests.cmake` → `cna_test_easygl_spritefont_default_char`
  (`EasyGL_SpriteFont_DefaultChar`); also reused verbatim by
  `cmake/Tests/VulkanTests.cmake` → `cna_test_vulkan_spritefont_default_char`.
- Main related production files: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`
  (`DrawString`, lines 401-506, specifically the fallback branch lines 457-465),
  `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp`/`.hpp`.
- Stated origin: "Direct EasyGL port of Task 693's SDL_Renderer test", building on Task 424's fixture
  pattern.

## Purpose

Confirms that when `SpriteBatch::DrawString` encounters a character not present in the font's
character map (here, `'Z'`, drawn against a font whose only real glyph is `'A'`), it falls back to
rendering the font's configured `DefaultCharacter` glyph (here, `'?'`, styled distinctly in Red) at
the position `'Z'` would have occupied — rather than throwing (since a default character *is*
configured) or silently rendering nothing.

## Executive Verdict

**Healthy.** Independently re-traced the exact `DrawString` fallback branch and the resulting glyph
placement arithmetic; both match the test's expected pixel layout precisely. The test's own "must not
throw" assertion and its 3 pixel checks are mutually reinforcing, not redundant.

## Checklist Results

### API / XNA / FNA parity
Constructs a `SpriteFont` via the constructor taking `(texture, glyphBounds, cropping, characters,
lineSpacing, spacing, kerning, defaultCharacter)` — matches
`include/Microsoft/Xna/Framework/Graphics/SpriteFont.hpp`'s declared constructor and
`SpriteFont.cpp`'s implementation (lines 12-34) parameter-for-parameter. `defaultCharacter` passed as
`std::optional<SharpRuntime::charcs>(u'?')` — matches FNA's `Nullable<char> DefaultCharacter`
semantics (`SpriteFontReader.cs` reads an XNB-serialized nullable char the same way).

### Behavioral correctness
Traced `SpriteBatch::DrawString`'s fallback branch directly (`SpriteBatch.cpp` lines 457-465):
```cpp
auto it = spriteFont.characterIndexMap_.find(c);
if (it == spriteFont.characterIndexMap_.end())
{
    if (!spriteFont.defaultCharacter_.has_value())
        throw std::invalid_argument(...);
    it = spriteFont.characterIndexMap_.find(spriteFont.defaultCharacter_.value());
}
```
For input `'Z'`, `characterIndexMap_` (built from `characters = {u'A', u'?'}`) has no entry, and
`defaultCharacter_` holds `'?'`, so `it` resolves to index 1 (`'?'`'s glyph) — no exception is thrown,
matching the test's own "does not throw" assertion (line 111) and FNA's identical control flow
(`SpriteBatch.cs` lines 774-789, `characterIndexMap[spriteFont.DefaultCharacter.Value]`).

### Logic
Independently re-derived the final glyph placement to verify the test's 3 pixel checks: with
`effects=SpriteEffects::None` (the color-only `DrawString` overload used here delegates to
`rotation=0, origin=Vector2::Zero, scale=(1,1)`), `baseOffset` stays `(0,0)` (no
`MeasureString`-based shift, since that only applies when `effects != None`); for the resolved `'?'`
glyph (`cCrop=(0,0,8,8)`, `cGlyph=(8,0,8,8)`, `kerning=(0,8,0)`), `curOffset.X` after the
first-in-line-bump is `0`, giving `offsetX=offsetY=0`, hence `localX=localY=0` and final
`dest = (round(2+0), round(2+0), round(8*1), round(8*1)) = (2,2,8,8)`. This matches the test's checks:
`(6,6)` (inside `[2,10)×[2,10)`) must be Red (the `'?'` glyph's own color, sampled from atlas region
`(8,0,8,8)` which the test's `Initialize()` fills with Red) — confirmed correct; `(0,0)` and `(13,13)`
(both outside `[2,10)`) must be the Black clear color — confirmed correct.

### Memory/resource lifetime
`atlas_`/`font_` constructed once in `Initialize()`; `font_` holds the `SpriteFont` by value
internally referencing `atlas_` via a `Texture2D` **by-value** member (`SpriteFont::textureValue_` is
a `Texture2D` value, not a reference) — confirmed via `SpriteFont.hpp`'s declared member
`Texture2D textureValue_;` and the constructor's `textureValue_(std::move(texture))` — so `font_`'s
internal texture is a distinct copy/move-constructed `Texture2D`, not a dangling reference to
`atlas_`. `atlas_` itself remains alive for the object's lifetime regardless, so no lifetime hazard
either way.

### C++ correctness
`try { sb_->Begin(); sb_->DrawString(...); sb_->End(); } catch (...) { threw = true; }` (lines 98-109)
— if `DrawString` were to throw (it doesn't, per the trace above), `sb_->End()` would never run,
leaving the `SpriteBatch`'s internal `begun` flag `true` and the EasyGL backend's own `begun` flag
`true` — a state leak that would only matter if this `SpriteBatch` instance were reused for a
subsequent `Begin()` call, which it isn't (the game exits immediately after this single `Draw()`).
Not a live defect in this test given its single-shot control flow, but worth noting for anyone
copy-pasting this try/catch pattern into a longer-lived test.

### Performance
N/A — single-frame test, 16×8 atlas.

### Thread safety
N/A.

### Architecture
No backend-specific code in the test; matches the shared-source-across-backends pattern (this file
is also compiled as `cna_test_vulkan_spritefont_default_char`).

### Maintainability
Header comment (lines 1-22) quotes the actual production fallback branch's shape almost verbatim and
explains why `'Z'` (not merely "any unmapped character") was chosen — good self-documentation
connecting the test scene directly to the code path it exercises.

### Portability
N/A.

### Robustness
`result_` defaults to `0`, matching the shard-wide pattern noted elsewhere in this batch.

### Testing
This file is itself a test. See Missing or Weak Tests for a genuine production-code robustness gap
this test does not (and, by design, cannot) exercise.

## Detailed Findings

No correctness defects found in this file itself. One noteworthy production-code robustness gap this
test's scenario cannot surface (recorded below, not as a defect in this file):

### F1 — `DrawString`'s fallback path has no guard if `DefaultCharacter` itself is absent from the character map

- Severity: N/A to this file (this is a production-code observation, not a defect in the test)
- Confidence: HIGH (traced directly in `SpriteBatch.cpp`)
- Category: cross-file / test-coverage
- Location/symbol: `SpriteBatch::DrawString`, `SpriteBatch.cpp` lines 460-465:
  `it = spriteFont.characterIndexMap_.find(spriteFont.defaultCharacter_.value()); ... const int index
  = it->second;` (line 465, immediately after) — if the configured `defaultCharacter_` is itself not
  a key in `characterIndexMap_` (e.g. a font whose `DefaultCharacter` was set to a glyph that was
  never actually added to its character list — a malformed-but-constructible `SpriteFont`), `it`
  becomes `characterIndexMap_.end()`, and dereferencing `it->second` on line 465 is undefined
  behavior.
- Why it matters: this specific test always configures a `defaultCharacter_` (`'?'`) that **is**
  present in the character list (`characters = {u'A', u'?'}`), so it cannot exercise this path — no
  test in this 8-file batch does either. This is a real, unexercised production-code edge case, not a
  defect in this test file, but is exactly the kind of gap this audit's "Testing" section exists to
  surface.
- FNA/XNA comparison: FNA's own `DrawString` (`SpriteBatch.cs` line 788:
  `index = characterIndexMap[spriteFont.DefaultCharacter.Value];`) has the identical unguarded
  dictionary-indexer pattern — in C# this would throw `KeyNotFoundException` rather than invoke
  undefined behavior, so this is a case where the C++ port's behavior is *more* dangerous than FNA's
  on this specific malformed-input path (a managed-runtime exception vs. C++ UB), which is worth
  flagging for whoever next audits `SpriteBatch.cpp`/`SpriteFont.cpp` directly.
- Suggested future action (not implemented by this audit): flag for the `xna-graphics` /
  `tests-xna-graphics` shard audit of `SpriteBatch.cpp` itself; not actionable from this test-file-only
  audit scope.

## Cross-File Observations

- Verbatim cross-backend reuse confirmed: `cmake/Tests/EasyGLTests.cmake` line 1270
  (`cna_test_easygl_spritefont_default_char`) and `cmake/Tests/VulkanTests.cmake` line 791
  (`cna_test_vulkan_spritefont_default_char`).
- `SpriteFont` declares `friend class SpriteBatch;` (`SpriteFont.hpp` line 119), which is why
  `SpriteBatch::DrawString` can reach `characterIndexMap_`/`glyphData_`/etc. directly — confirmed this
  friendship exists and is the only way this cross-class field access compiles.

## Missing or Weak Tests

- See F1 above: no test (in this file or, per this batch, any sibling) exercises a `SpriteFont` whose
  `DefaultCharacter` is set to a character absent from its own character list — a real, currently
  latent UB path in `SpriteBatch::DrawString`.
- The `try`/`catch` pattern (lines 98-109) only proves *some* exception wasn't thrown; it does not
  distinguish "correctly took the fallback path" from "coincidentally didn't throw for an unrelated
  reason" independently of the subsequent pixel checks — though in practice the pixel checks
  immediately following do close this gap (if the fallback path weren't taken correctly, the pixel
  checks would fail), so this is a minor, largely theoretical concern.

## Positive Findings

- Test scenario and its 3 pixel checks were independently re-derived from the actual
  `DrawString`/`MeasureString` production code and match exactly — this is a genuine, working
  regression test for the fallback-character code path, not a "compiles and renders something" smoke
  test.
- Good choice of an unmapped character (`'Z'`) that is unambiguously outside the font's declared
  alphabet, rather than a character that might coincidentally collide with an implementation detail.

## Final Assessment

A correct, well-targeted regression test for `SpriteFont`'s default-character fallback; its expected
geometry was independently confirmed against the real `DrawString` arithmetic. The most valuable
takeaway from this audit is not a defect in this file but a genuine, currently-untested UB path in the
production `DrawString` fallback logic when `DefaultCharacter` itself is malformed (F1) — worth
surfacing to whoever next audits `SpriteBatch.cpp` directly.
