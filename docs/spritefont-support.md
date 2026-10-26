# SpriteFont Support and Limitations

Phase 48 (graphics Tasks 421–430) audited `SpriteFont` against FNA's own `SpriteFont.cs`
and pixel-verified `SpriteBatch::DrawString` rendering on EasyGL (Tasks 424–429). This document
summarizes the findings, the content-loading model, and known limitations, and closes the phase.

---

## 1. API surface audit (Task 421)

`SpriteFont`'s property surface (`Characters`, `DefaultCharacter`, `LineSpacing`, `Spacing`) matches
the classic XNA shape. The final Software parity challenge subsequently found one intentional
Microsoft/FNA behavior difference (`SOFTWARE-353`): Microsoft's internal content constructor
preserves an invalid default character, its public setter rejects it, and terminal lookup failures
name `character`; CNA now follows Microsoft. The original API-surface pass found this separate gap:

- **`MeasureString(StringBuilder)` was entirely missing** (Task 421 found it, Task 423 fixed it —
  see §3). FNA has both `MeasureString(string)` and `MeasureString(StringBuilder)`; CNA had only the
  `string` overload, even though `SpriteBatch::DrawString` already had full `StringBuilder` support.

## 2. `MeasureString(string)` (Task 422)

Already comprehensively tested before this phase: empty string, single/multiple characters with and
without `Spacing`, height driven by the tallest glyph's cropping height, multi-line `\n`
accumulation, a lone interior `\r`, and unknown-character behavior both with and without a
`DefaultCharacter` configured. Task 422 compared the algorithm against FNA; `SOFTWARE-353` later
recovered Microsoft's distinct invalid-default and `ArgumentException("character")` behavior and
made that behavior authoritative. Task 422 also added 3 further edge cases: consecutive `\r`s, a
leading `\n`, and a trailing `\n`. The last is a
non-obvious FNA behavior worth calling out explicitly: **a trailing newline adds a full second,
empty line's height** (`Y = 2×LineSpacing`, not `1×`) — the `\n` handler's own height-add and the
loop's unconditional final height-add both fire. A caller assuming "N lines of text = N×LineSpacing"
will be subtly wrong for any string ending in `\n`.

## 3. `MeasureString(StringBuilder)` (Task 423)

Added as a one-line forward to `MeasureString(text.ToString())`, mirroring the same convention
`SpriteBatch::DrawString`'s own `StringBuilder` overloads already use. FNA keeps a fully duplicated
algorithm for its `StringBuilder` overload purely to avoid `string`-conversion GC garbage — not a
concern in C++, so CNA does not duplicate the algorithm.

## 4. Pixel-verified `DrawString` rendering (Tasks 424–429)

Every one of the following was pixel-verified on EasyGL, using the same hand-built single/two-glyph fixtures (see §5) and the same discriminating-power
methodology (sabotage the relevant shared code, confirm the exact predicted check fails, restore,
reconfirm):

| Behavior | EasyGL |
|---|---|
| Single glyph at a known position | ✅ Task 424 |
| Multiple glyphs with `Spacing` + kerning | ✅ Task 425 |
| `\n` advances by `LineSpacing` (not glyph height) | ✅ Task 426 |
| Unknown-character → `DefaultCharacter` fallback | ✅ Task 427 |
| `SpriteEffects` flip (mirrors glyph sequence, not just each glyph's own texture) | ✅ Task 428 |
| Rotation / origin / scale | ✅ Task 429 |

**One real, previously-undiscovered bug was found and fixed** (Task 694, in the shared,
renderer-agnostic `SpriteBatch.cpp` — affects every renderer):
`DrawString` previously forwarded `SpriteEffects` straight to the per-glyph draw call, which only
flips that glyph's own texture sampling in place. The glyph **sequence and position** were never
mirrored at all — FNA's real algorithm measures the whole string up front and shifts `origin` by the
measured size on the mirrored axis, so later characters end up earlier on screen. Fixed by porting
FNA's `axisDirectionX/Y`/`axisIsMirroredX/Y` lookup-table algorithm (re-derived in terms of CNA's own
existing sign convention rather than copying FNA's internal variables verbatim). This closes the
entire single/multi-glyph/newline/default-char/effects/rotation-scale pixel-verification range
(Tasks 424–429).

## 5. Content-loading model and testing convention

CNA now has an XNB content pipeline and a `SpriteFontReader`. FNA's `SpriteFont`
constructor is `internal`, invoked by the reader; CNA also exposes the
constructor publicly with `CNAEXT` for callers that build glyph/cropping/kerning
tables directly. The tests below use that direct construction path because
they predate the XNB reader.

Every SpriteFont test in this project follows the same established convention: **hand-build a
minimal fixture** rather than loading a real font asset. A typical fixture is a small solid-color
atlas texture (e.g. an 8×8 or 16×8 block) with 1–2 glyphs, explicit `glyphBounds`/`cropping`
rectangles, and hand-picked `kerning` (`Vector3(leftBearing, width, rightBearing)`) values chosen so
the expected pixel output can be derived by hand and independently checked against the render.

## 6. Character encoding

FNA's `SpriteFont` is keyed by `char` (a UTF-16 code unit, matching .NET's native string
representation). CNA's `String` alias is `std::string` (UTF-8), so `SpriteFont`/`SpriteBatch`
decode each string via `CNA::Internal::DecodeUtf8CodePoint`: a code point is decoded per glyph
lookup, advancing a byte index in place. This is a CNA-specific architectural adaptation, not a bug
— but it comes with real, documented constraints:

- Only **Basic Multilingual Plane** code points are supported as glyph keys (`charcs` is a 16-bit
  code unit) — a SpriteFont atlas can only ever hold BMP characters, matching what a `char16_t` key
  can represent.
- **Invalid or truncated UTF-8 sequences decode to `'?'`** rather than throwing or silently
  skipping, and the byte index always advances by at least one byte, so malformed input can never
  cause an infinite loop.

## 7. Known limitation: no combined `SpriteEffects` flip

FNA's `SpriteEffects` is a `[Flags]` enum — `FlipHorizontally | FlipVertically` is a valid runtime
value (3) even without a named member for it, and FNA's own `axisDirectionX/Y` tables have a 4th
entry to handle it. **CNA's `SpriteEffects` is a plain `enum class` with no bitwise operators
defined** (`include/Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp`), so a combined value can
only be constructed via an explicit `static_cast<SpriteEffects>(3)` — there is no ergonomic way for
calling code to request "flip both axes" the way FNA callers can. `SpriteBatch::DrawString`'s own
`axisDirX`/`axisDirY`/`axisIsMirroredX`/`axisIsMirroredY` tables (added by Task 694) deliberately
only have 3 entries (indices 0–2) for exactly this reason — index 3 is unreachable through any
normal calling convention and was not worth including speculatively. This is a genuine, minor API
gap versus FNA, not something this phase's tasks were scoped to fix; flagged here for visibility
rather than silently left undocumented.

## Support matrix

| Feature | EasyGL |
|---|---|
| Property surface (`Characters`/`DefaultCharacter`/`LineSpacing`/`Spacing`) | ✅ (shared C++) |
| `MeasureString(string)` | ✅ Task 422 (shared C++) |
| `MeasureString(StringBuilder)` | ✅ Task 423 (shared C++) |
| Single-glyph placement | ✅ Task 424 |
| Multi-glyph spacing/kerning | ✅ Task 425 |
| Newline (`LineSpacing`) | ✅ Task 426 |
| Default-character fallback | ✅ Task 427 |
| `SpriteEffects` flip (single-axis) | ✅ Task 428 |
| `SpriteEffects` flip (combined, both axes) | ❌ not representable |
| Rotation / origin / scale | ✅ Task 429 |

Legend: ✅ verified working · ❌ confirmed not implemented/not representable.

## Open, tracked follow-up work

- **Combined `SpriteEffects` flip** (§7): a genuine, minor API-completeness gap versus FNA's
  `[Flags]` enum. Not scoped to this phase; would need `SpriteEffects` operator overloads plus a
  4th `axisDir`/`axisIsMirrored` table entry in `SpriteBatch.cpp` if ever prioritized.

This closes Phase 48 (graphics Tasks 421–430) in full.
