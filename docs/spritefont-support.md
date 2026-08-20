# SpriteFont Support and Limitations

Phase 48 (`plans/plan_graphics.md` Tasks 421–430) audited `SpriteFont` against FNA's own `SpriteFont.cs`
and pixel-verified `SpriteBatch::DrawString` rendering on both SDL_Renderer (Tasks 690–694, an
earlier session) and EasyGL (Tasks 424–429, this session). This document summarizes the findings,
the content-loading model, and known limitations, and closes the phase.

---

## 1. API surface audit (Task 421)

`SpriteFont`'s property surface (`Characters`, `DefaultCharacter`, `LineSpacing`, `Spacing`) matches
FNA exactly, both in type and behavior. One real gap was found and fixed:

- **`MeasureString(StringBuilder)` was entirely missing** (Task 421 found it, Task 423 fixed it —
  see §3). FNA has both `MeasureString(string)` and `MeasureString(StringBuilder)`; CNA had only the
  `string` overload, even though `SpriteBatch::DrawString` already had full `StringBuilder` support.

## 2. `MeasureString(string)` (Task 422)

Already comprehensively tested before this phase: empty string, single/multiple characters with and
without `Spacing`, height driven by the tallest glyph's cropping height, multi-line `\n`
accumulation, a lone interior `\r`, and unknown-character behavior both with and without a
`DefaultCharacter` configured. Task 422 re-verified the whole algorithm line-by-line against FNA and
added 3 further edge cases: consecutive `\r`s, a leading `\n`, and a trailing `\n`. The last is a
non-obvious FNA behavior worth calling out explicitly: **a trailing newline adds a full second,
empty line's height** (`Y = 2×LineSpacing`, not `1×`) — the `\n` handler's own height-add and the
loop's unconditional final height-add both fire. A caller assuming "N lines of text = N×LineSpacing"
will be subtly wrong for any string ending in `\n`.

## 3. `MeasureString(StringBuilder)` (Task 423)

Added as a one-line forward to `MeasureString(text.ToString())`, mirroring the same convention
`SpriteBatch::DrawString`'s own `StringBuilder` overloads already use. FNA keeps a fully duplicated
algorithm for its `StringBuilder` overload purely to avoid `string`-conversion GC garbage — not a
concern in C++, so CNA does not duplicate the algorithm.

## 4. Pixel-verified `DrawString` rendering (Tasks 424–429 / 690–694)

Every one of the following was independently pixel-verified on **both** SDL_Renderer and EasyGL,
using the same hand-built single/two-glyph fixtures (see §5) and the same discriminating-power
methodology (sabotage the relevant shared code, confirm the exact predicted check fails, restore,
reconfirm):

| Behavior | SDL_Renderer | EasyGL |
|---|---|---|
| Single glyph at a known position | ✅ Task 690 | ✅ Task 424 |
| Multiple glyphs with `Spacing` + kerning | ✅ Task 691 | ✅ Task 425 |
| `\n` advances by `LineSpacing` (not glyph height) | ✅ Task 692 | ✅ Task 426 |
| Unknown-character → `DefaultCharacter` fallback | ✅ Task 693 | ✅ Task 427 |
| `SpriteEffects` flip (mirrors glyph sequence, not just each glyph's own texture) | ✅ fixed Task 694 | ✅ Task 428 |
| Rotation / origin / scale | ✅ Task 694 | ✅ Task 429 |

**One real, previously-undiscovered bug was found and fixed** (Task 694, in the shared,
renderer-agnostic `SpriteBatch.cpp` — affects every renderer, not just SDL_Renderer):
`DrawString` previously forwarded `SpriteEffects` straight to the per-glyph draw call, which only
flips that glyph's own texture sampling in place. The glyph **sequence and position** were never
mirrored at all — FNA's real algorithm measures the whole string up front and shifts `origin` by the
measured size on the mirrored axis, so later characters end up earlier on screen. Fixed by porting
FNA's `axisDirectionX/Y`/`axisIsMirroredX/Y` lookup-table algorithm (re-derived in terms of CNA's own
existing sign convention rather than copying FNA's internal variables verbatim). This closes the
entire single/multi-glyph/newline/default-char/effects/rotation-scale pixel-verification range on
both renderers (Tasks 424–429 mirroring 690–694).

## 5. Content-loading model and testing convention

CNA has **no XNB content pipeline**. FNA's `SpriteFont` constructor is `internal`, invoked only by
the content pipeline's `SpriteFontReader`; CNA exposes the equivalent constructor publicly with
`CNAEXT`, since there is no reader to invoke it on the caller's behalf. Any application (or a future
content-reader implementation) must build the glyph/cropping/kerning tables itself.

Every SpriteFont test in this project — from the original SDL_Renderer pass (Tasks 690–694) through
this phase's EasyGL pass (Tasks 424–429) — follows the same established convention: **hand-build a
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

| Feature | SDL_Renderer | EasyGL | Vulkan | Bgfx |
|---|---|---|---|---|
| Property surface (`Characters`/`DefaultCharacter`/`LineSpacing`/`Spacing`) | ✅ (shared C++) | ✅ (shared C++) | ✅ (shared C++) | ✅ (shared C++) |
| `MeasureString(string)` | ✅ Task 422 | ✅ (shared C++) | ✅ (shared C++) | ✅ (shared C++) |
| `MeasureString(StringBuilder)` | ✅ Task 423 (fixed) | ✅ (shared C++) | ✅ (shared C++) | ✅ (shared C++) |
| Single-glyph placement | ✅ Task 690 | ✅ Task 424 | — not pixel-verified | — not pixel-verified |
| Multi-glyph spacing/kerning | ✅ Task 691 | ✅ Task 425 | — not pixel-verified | — not pixel-verified |
| Newline (`LineSpacing`) | ✅ Task 692 | ✅ Task 426 | — not pixel-verified | — not pixel-verified |
| Default-character fallback | ✅ Task 693 | ✅ Task 427 | — not pixel-verified | — not pixel-verified |
| `SpriteEffects` flip (single-axis) | ✅ fixed Task 694 | ✅ Task 428 | ✅ (shared fix) | ✅ (shared fix) |
| `SpriteEffects` flip (combined, both axes) | ❌ not representable | ❌ not representable | ❌ not representable | ❌ not representable |
| Rotation / origin / scale | ✅ Task 694 | ✅ Task 429 | — not pixel-verified | — not pixel-verified |

Legend: ✅ verified working · ❌ confirmed not implemented/not representable · — not yet exercised
by a dedicated pixel test on that renderer (the underlying logic is shared C++, so it very likely
already works, but has not been independently pixel-verified there the way SDL_Renderer/EasyGL now
are).

## Open, tracked follow-up work

- **Vulkan/Bgfx SpriteFont pixel-verification**: neither renderer has its own dedicated
  single-glyph/multi-glyph/newline/default-char/effects/rotation-scale pixel-test pass the way
  SDL_Renderer (Tasks 690–694) and EasyGL (Tasks 424–429) now do. The underlying `DrawString`
  logic is entirely shared C++, so this is a confidence/coverage gap rather than a known bug, but
  it has not been closed the same rigorous way for these 2 renderers.
- **Combined `SpriteEffects` flip** (§7): a genuine, minor API-completeness gap versus FNA's
  `[Flags]` enum. Not scoped to this phase; would need `SpriteEffects` operator overloads plus a
  4th `axisDir`/`axisIsMirrored` table entry in `SpriteBatch.cpp` if ever prioritized.

This closes Phase 48 (`plans/plan_graphics.md` Tasks 421–430) in full.
