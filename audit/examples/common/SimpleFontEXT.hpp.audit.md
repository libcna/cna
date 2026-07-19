# Audit: examples/common/SimpleFontEXT.hpp

## Metadata
- Source file: `examples/common/SimpleFontEXT.hpp` (208 lines)
- Audit status: AUDITED
- Subsystem: `examples-common` shard
- File type: shared header-only helper, used by multiple example demos
- XNA/FNA relevance: builds a real `Microsoft::Xna::Framework::Graphics::SpriteFont` instance for
  demo use — not itself an XNA type
- Related production code: `SpriteFont.hpp`/`.cpp` (audited this session as part of the
  `xna-graphics` shard, where a HIGH-severity default-character-fallback UB finding was confirmed)

## Purpose
Builds a real, readable 5x7 dot-matrix bitmap `SpriteFont` covering printable ASCII 32-126, shared
across many demos, replacing an older, confirmed-broken per-demo "block font" (every character
rendered as an identical uniform rectangle, discovered unreadable by an independent audit).

## Executive Verdict
Correct, and specifically confirmed **not** to reproduce the HIGH-severity `SpriteFont`/
`SpriteBatch` default-character-fallback UB finding from this session's `xna-graphics` shard audit:
`defaultCharacter` is set to `' '` (space, ASCII 32, line 206), which is always present in the
`chars` vector this same function builds (covering `32 + glyphIndex` for every `glyphIndex` in
`[0, 95)`, i.e. exactly ASCII 32-126) — the fallback path is always resolvable, so the confirmed
`unordered_map::end()` dereference cannot occur through any font built by this helper.

## Checklist Results
- `bounds` (passed as `SpriteFont`'s `glyphData`) correctly encodes each glyph's actual atlas
  source rect and destination-draw size — the doc comment (lines 195-199) explicitly and
  accurately explains this is exactly the field the old broken block-font got wrong (using an
  identical `(0,0,1,1)` rect for every character regardless of shape).
- The glyph-atlas-building double loop (lines 158-180) correctly maps each bit of each 5-bit-wide
  row to a lit/unlit pixel, with the documented bit-order convention (`bit 4 = leftmost column,
  bit 0 = rightmost`) — spot-checked several glyphs (e.g. `'A'` at index 33: rows
  `0x0E,0x11,0x11,0x1F,0x11,0x11,0x11` correctly trace a recognizable capital-A shape when each
  row's 5 bits are read MSB-to-LSB as columns left-to-right).
- `kGlyphAdvance = 6` (5px glyph + 1px natural gap) is correctly reflected in the per-glyph
  `kerning` entry (`Vector3(0.0f, 6.0f, 0.0f)`), matching `SpriteFont`'s documented kerning-field
  semantics (left-bearing, width, right-bearing) confirmed in this session's `xna-graphics` shard
  audit of `SpriteFont.cpp`'s `MeasureString`.

## Detailed Findings
None.

## Cross-File Observations
This is the shared helper flagged for exactly this check in
`examples/demo_net_avatar_sync/src/SyncGame.cpp.audit.md` (and likely used by several other demos
audited this session, e.g. `demo_gamer_roster_hud`/`demo_friends_and_gamercard`/etc. used their own
local, structurally-similar-but-independent `MakeSimpleFont()` copies rather than this shared
helper — worth noting this shard's helper and those per-demo copies are NOT the same code, though
both happen to independently choose a safe `defaultCharacter`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The doc comment's explicit rationale for making this a *shared* header ("the old convention is
exactly what let the uniform-rectangle bug spread silently across every demo that copied it; a
real font table is too large and too easy to desync to duplicate 8 times") reflects a genuinely
good engineering lesson learned and correctly applied.

## Final Assessment
No findings. Confirmed safe against the `SpriteFont` default-character-fallback UB finding.
