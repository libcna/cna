# SpriteFont Support and Limitations

`SpriteFont` against FNA's `SpriteFont.cs` and, where they differ, measured Microsoft XNA 4.0;
`SpriteBatch::DrawString` rendering is pixel-verified on EasyGL.

---

## 1. API surface

`SpriteFont`'s property surface (`Characters`, `DefaultCharacter`, `LineSpacing`, `Spacing`) and
both `MeasureString(string)` and `MeasureString(StringBuilder)` overloads match the classic XNA
shape.

Where Microsoft and FNA differ, CNA follows Microsoft (`SOFTWARE-353`): the internal content
constructor preserves an invalid default character, the public `DefaultCharacter` setter rejects
it, and terminal lookup failures throw `ArgumentException` naming `character`.

## 2. `MeasureString(string)`

Tests cover the empty string, single/multiple characters with and without `Spacing`, height driven
by the tallest glyph's cropping height, multi-line `\n` accumulation, a lone interior `\r`,
consecutive `\r`s, a leading `\n`, a trailing `\n`, and unknown characters with and without a
`DefaultCharacter`.

**A trailing newline adds a full second, empty line's height** (`Y = 2×LineSpacing`, not `1×`):
the `\n` handler's height-add and the loop's unconditional final height-add both fire. "N lines of
text = N×LineSpacing" is wrong for any string ending in `\n`.

## 3. `MeasureString(StringBuilder)`

A one-line forward to `MeasureString(text.ToString())`, the same convention as
`SpriteBatch::DrawString`'s `StringBuilder` overloads. FNA duplicates the algorithm only to avoid
`string`-conversion GC garbage, which is not a concern in C++.

## 4. Pixel-verified `DrawString` rendering

Each behavior below is pixel-verified on EasyGL with the hand-built single/two-glyph fixtures of §5
(registered as `EasyGL_SpriteFont_<suffix>`):

| Behavior | EasyGL | Test suffix |
|---|---|---|
| Single glyph at a known position | ✅ | `SingleGlyph` |
| Multiple glyphs with `Spacing` + kerning | ✅ | `MultiGlyphSpacing` |
| `\n` advances by `LineSpacing` (not glyph height) | ✅ | `Newline` |
| Unknown-character → `DefaultCharacter` fallback | ✅ | `DefaultChar` |
| `SpriteEffects` flip (mirrors glyph sequence, not just each glyph's own texture) | ✅ | `EffectsFlip` |
| Rotation / origin / scale | ✅ | `EffectsRotationScale` |

**Flip contract.** `DrawString` mirrors the glyph **sequence and position**, not only each
glyph's texture sampling: as in FNA's `axisDirectionX/Y`/`axisIsMirroredX/Y` tables, the whole
string is measured up front and `origin` is shifted by the measured size on the mirrored axis, so
later characters end up earlier on screen. This is shared, renderer-agnostic code in
`SpriteBatch.cpp`. For horizontally flipped glyphs with asymmetric cropping CNA follows Microsoft
XNA rather than FNA (TQ-020, "SpriteFont horizontal-flip cropping" in
[`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md#measured-xna-over-fna-decisions)).

## 5. Content-loading model and testing convention

FNA's `SpriteFont` constructor is `internal`, invoked by its `SpriteFontReader`. CNA's XNB content
pipeline has a `SpriteFontReader` too, and CNA also exposes the constructor publicly with `CNAEXT`
for callers that build glyph/cropping/kerning tables directly.

SpriteFont tests **hand-build a minimal fixture** through that constructor rather than loading a
real font asset: a small solid-color atlas texture (e.g. an 8×8 or 16×8 block) with 1–2 glyphs,
explicit `glyphBounds`/`cropping` rectangles, and hand-picked `kerning`
(`Vector3(leftBearing, width, rightBearing)`) values, so the expected pixel output can be derived
by hand.

## 6. Character encoding

FNA's `SpriteFont` is keyed by `char` (a UTF-16 code unit). CNA's `String` alias is `std::string`
(UTF-8), so `SpriteFont`/`SpriteBatch` decode each string via `CNA::Internal::DecodeUtf8CodePoint`:
a code point is decoded per glyph lookup, advancing a byte index in place. Constraints:

- Only **Basic Multilingual Plane** code points are supported as glyph keys (`charcs` is a 16-bit
  code unit) — a SpriteFont atlas can only hold BMP characters.
- **Invalid or truncated UTF-8 sequences decode to `'?'`** rather than throwing or silently
  skipping, and the byte index always advances by at least one byte, so malformed input can never
  cause an infinite loop.

## 7. Combined `SpriteEffects` flip

FNA's `SpriteEffects` is a `[Flags]` enum — `FlipHorizontally | FlipVertically` is a valid runtime
value (3) without a named member, and FNA's `axisDirectionX/Y` tables have a 4th entry for it.
`SpriteEffects.hpp` defines `|`, `&`, `|=` and `&=`, so callers can compose the two named flags.
`SpriteBatch::DrawString` masks to the two meaningful low bits and uses four-entry X/Y direction
and mirror tables; value 3 mirrors the complete text block on both axes. The
`SpriteBatchDrawStringSpriteEffectsTest` cases require the combined value to match the horizontal
X result, match the vertical Y result and differ from `None`; the `SpriteEffectsOperatorsTest`
cases cover all four operators.

## Support matrix

| Feature | EasyGL |
|---|---|
| Property surface (`Characters`/`DefaultCharacter`/`LineSpacing`/`Spacing`) | ✅ (shared C++) |
| `MeasureString(string)` | ✅ (shared C++) |
| `MeasureString(StringBuilder)` | ✅ (shared C++) |
| Single-glyph placement | ✅ |
| Multi-glyph spacing/kerning | ✅ |
| Newline (`LineSpacing`) | ✅ |
| Default-character fallback | ✅ |
| `SpriteEffects` flip (single-axis) | ✅ |
| `SpriteEffects` flip (combined, both axes) | ✅ |
| Rotation / origin / scale | ✅ |
