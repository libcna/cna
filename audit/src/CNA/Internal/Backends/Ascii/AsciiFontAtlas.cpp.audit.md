# Audit: src/CNA/Internal/Backends/Ascii/AsciiFontAtlas.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Ascii/AsciiFontAtlas.cpp`
- Audit status: AUDITED
- Subsystem: `backend-ascii` shard
- File type: C++ implementation (115 lines)
- Related header/implementation: `include/CNA/Internal/Backends/Ascii/AsciiFontAtlas.hpp` (same shard)
- XNA/FNA relevance: `BuildAsciiFontAtlas()` constructs a real `Microsoft::Xna::Framework::Graphics::SpriteFont`
  via its public constructor — worth cross-checking that constructor's exact parameter contract when the
  `xna-graphics`/`SpriteFont` area is audited, though the call site here looks internally consistent.
- Graphics backend relevance: builds the hand-authored 8×8 glyph bitmap atlas the Ascii backend uses both
  internally (`BuildAsciiFontAtlasImageData()`, no `GraphicsDevice` needed) and for external diagnostic use
  (`BuildAsciiFontAtlas()`, wrapped as a real `SpriteFont`).
- FNA reference: N/A (hand-authored NOXNA asset, explicitly not a vendored font per its own "design decision 4"
  comment, avoiding a licensing dependency).
- Main related tests: `examples-tests-ascii` (6 files, including a dedicated `AsciiFontAtlasTest`/
  `ascii_fontatlas_test.cpp` per the manifest — not yet audited).

## Purpose

Defines 10 hand-authored 8×8 monochrome bitmap glyphs (a luminance-ordered ramp, `" .:-=+*#%@"`) plus one solid
white "background fill" slot, and two ways to consume them: a raw `ImageData` builder (used internally by the
backend itself, no `GraphicsDevice` dependency) and a `SpriteFont`-wrapping builder (for external/diagnostic use).

## Executive Verdict

**Healthy.** Both builders are internally consistent with each other and with the header's documented atlas
layout; the one property the code's own comment claims is verified elsewhere (`AsciiFontAtlasTest`) — strictly
increasing pixel-count coverage across the ramp — was spot-checked here too (see Behavioral correctness) and
holds.

## Checklist Results

### Behavioral correctness
Cross-checked the claim in the `kGlyphBitmaps` comment ("pixel-count coverage verified strictly increasing... 0, 2,
4, 6, 12, 20, 24, 40, 48, 60 out of 64") by counting set bits in each of the 10 hand-authored rows directly: `' '`
(all-zero rows) = 0; `'.'` (one row `0x18`=2 bits) = 2; `':'` (two rows of `0x18`) = 4; `'-'` (one row `0x7E`=6
bits) = 6; `'='` (two rows `0x7E`) = 12; `'+'` (`0x18,0x18,0x7E,0x7E,0x18,0x18`= 2+2+6+6+2+2) = 20; `'*'`
(`0x42,0x24,0x18,0x7E,0x7E,0x18,0x24,0x42` = 2+2+2+6+6+2+2+2) = 24; `'#'` (`0x66×4 + 0xFF×2` = 4×4+8×2) = 32...

**Correction after re-counting**: `'#'`'s bitmap is `{0x66,0x66,0xFF,0x66,0x66,0xFF,0x66,0x66}` = six rows of
`0x66` (4 bits each = 24) + two rows of `0xFF` (8 bits each = 16) = 40, matching the comment's claimed value
exactly. `'%'` (`0xFF,0x66,0xFF,0x66,0xFF,0x66,0xFF,0x66` = four `0xFF` (8 each=32) + four `0x66` (4 each=16)) = 48,
matching. `'@'` (`0x7E,0xFF×6,0x7E` = 6+8×6+6=60), matching. **All 10 values independently re-derived and confirmed
exact** — the comment's claim is accurate, not just asserted.

### Logic
`BuildAsciiFontAtlasImageData()`'s solid-fill slot (line 70: `(g == kAsciiSolidGlyphIndex) ? 0xFF :
kGlyphBitmaps[g][y]`) correctly reads all-set bits for every row of the extra slot rather than indexing into
`kGlyphBitmaps` (which only has `kAsciiGlyphRampLength` rows, not `kAsciiAtlasGlyphCount`) — verified this doesn't
read past the `kGlyphBitmaps` array bounds for `g == kAsciiSolidGlyphIndex` (the ternary short-circuits to the
literal `0xFF` without touching `kGlyphBitmaps` at all in that branch).

### C++ correctness
No unsafe indexing found; `GetAsciiGlyphBitmap()` validates `rampIndex` range and throws `std::out_of_range`
otherwise (lines 45-48) rather than silently returning garbage.

### Memory/resource lifetime / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness
No issues found; small, self-contained, one-time-construction utility code.

### Testing
Not independently assessed against actual test content (queued for `examples-tests-ascii`), but the code's own
claim about pixel-count strictly-increasing coverage was independently re-derived and confirmed here regardless
of what that test does.

## Detailed Findings

None.

## Cross-File Observations

`BuildAsciiFontAtlas()`'s `SpriteFont` constructor call (lines 111-113) passes `kerning.emplace_back(0.0f,
static_cast<float>(kAsciiGlyphWidth), 0.0f)` for every glyph — worth cross-checking `SpriteFont`'s own kerning
triple convention (left-bearing, width, right-bearing) against FNA's `SpriteFont.cs` when the `xna-graphics` shard
reaches `SpriteFont.cpp`, to confirm `(0, width, 0)` is the intended "no extra bearing, exactly `glyphWidth` wide"
monospace convention rather than an accidental omission of some other expected value.

## Missing or Weak Tests

Not independently assessed (queued for `examples-tests-ascii`).

## Positive Findings

The pixel-count-strictly-increasing claim in the source comment was independently re-derived bit-by-bit and
confirmed exactly correct — a genuine, verifiable correctness property of the hand-authored glyph data, not just
an assertion.

## Final Assessment

No issues found; correct, small, well-verified hand-authored asset-generation code.
