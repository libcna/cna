# Audit: examples/bgfx_spritebatch_sourcerect_test.cpp

## Metadata

- Source file: `examples/bgfx_spritebatch_sourcerect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SpriteBatch::Draw`'s `sourceRectangle` cropping pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_spritebatch_sourcerect …)` /
  `cna_register_backend_test(NAME Bgfx_SpriteBatch_SourceRectangleCropping …)`,
  `cmake/Tests/BgfxTests.cmake:783-785`).
- XNA/FNA relevance: direct — `SpriteBatch.Draw(Texture2D, Rectangle, Rectangle?, Color)`'s `sourceRectangle`
  cropping semantics.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (lines 343-354),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`SubmitSprite`'s UV computation, lines 1443-1446).

## Purpose

3-check pixel test (Task 806, a Bgfx port of Task 419's EasyGL/Vulkan test) proving `sourceRectangle` correctly
crops which texel region of the texture is sampled, rather than being ignored (which would stretch the whole
texture into the destination instead of just the selected cell). A 20×20 texture is split into a 2×2 grid of
10×10 solid-color cells (Red/Blue/Magenta/Yellow); only the top-right (Blue) cell is selected via
`sourceRectangle=(10,0,10,10)` and stretched into a 50×50 destination — if cropping works, the *entire* rendered
sprite must be uniformly Blue. The file's own comment explains its 2 interior check points (near the
destination's top-left and bottom-right corners) are chosen so a "no cropping" bug would produce 2 independently
distinguishable wrong colors (Red at the first point, Yellow at the second) rather than one.

## Executive Verdict

**Healthy** — traced the `Draw(texture, destRect, optional<Rect> src, color)` overload
(`SpriteBatch.cpp:343-354`) and `SubmitSprite`'s UV computation and confirmed the cropping math is correct and
that both of the test's discriminating check points genuinely distinguish "cropped correctly" from "not
cropped" as claimed.

## Checklist Results

### API / XNA / FNA parity
`Draw(texture, destinationRectangle, sourceRectangle, color)` — this 4-argument overload (no rotation/origin/
effects/depth) matches FNA's own simplified `SpriteBatch.Draw(Texture2D, Rectangle, Rectangle?, Color)`
overload exactly.

### Behavioral correctness
- `SpriteBatch.cpp:343-354`: `sourceRectangle.has_value()` is true here (`Rectangle(10,0,10,10)` explicitly
  passed, not `std::nullopt`), so `src = sourceRectangle.value()` is used directly — no accidental fallback to
  the full texture.
- `SubmitSprite`'s UV computation (`BgfxGraphicsBackend.cpp:1443-1446`): `u1 = 10/20 = 0.5, v1 = 0/20 = 0,
  u2 = (10+10)/20 = 1.0, v2 = (0+10)/20 = 0.5` — i.e. the UV rectangle `[0.5,1.0]×[0,0.5]`, which for this
  texture's 2×2 grid is precisely the top-right (Blue) cell, with no bleed into any neighboring cell (the UV
  bounds land exactly on cell boundaries at 0.5, matching the texture's exact 10px/20px cell grid).
- Verified the 3 check points: (105,105) and (145,145) both fall inside the 50×50 destination rect at
  (100,100) — both must read Blue if cropping works. Under the "no cropping" bug (stretching the whole 20×20
  texture into the 50×50 destination), UV (0,0) [texture's own top-left, Red] would map to destination corner
  (100,100) — so (105,105), 5px from that corner, would read Red instead — confirming the file's own claimed
  discriminator. Symmetrically, UV (1,1) [texture's own bottom-right, Yellow] would map to (150,150), so
  (145,145) would read Yellow under that same bug — confirming the second discriminator independently. (200,50)
  is well outside the sprite entirely → Green background, a basic containment sanity check.
- Since the selected top-right cell (and, under the bug, the whole-texture stretch) samples entirely from
  interior points of solid-color 10×10/20×20 regions (not near any internal seam), `SamplerState::PointClamp`'s
  point-filtering (already independently confirmed correct for this backend in the sibling
  `bgfx_sprite_effects_test.cpp` report) is not even strictly necessary for this specific test's correctness —
  bilinear filtering would not blur across a seam this far from the check points either — but its use is still
  the right, more rigorous default for a texture-cropping test.

### Robustness
`colourMatch`'s `tol=60` distinguishes Blue/Red/Yellow/Green pairwise without ambiguity (each pair differs by
at least 255 in some channel), so no accidental-pass-by-tolerance risk.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW defects found in this file.

## Missing or Weak Tests

- A `sourceRectangle` that does **not** align to texture-integer pixel boundaries (e.g. crops a rectangle whose
  edges fall mid-texel) is not exercised in this file or any close sibling in this shard — a low-priority gap
  since FNA's own sourceRectangle contract is itself pixel-integer, not sub-pixel.
- The zero-width/zero-height `sourceRectangle` edge case (an empty crop) is not covered here; `SubmitSprite`
  does guard against it (`if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0) return;`,
  `BgfxGraphicsBackend.cpp:1436-1439`) but this specific file does not exercise that guard — reasonable, since
  this file's stated purpose is cropping-correctness, not degenerate-input robustness, and that guard likely has
  its own dedicated coverage elsewhere in the shard.

## Positive Findings

- Both of the file's named discriminating check points were independently verified by this audit to actually
  distinguish the correct cropped result from the specific "no cropping" bug it names — not just asserted by
  the comment.
- The chosen `sourceRectangle` lands exactly on a texel-grid boundary (0.5 in normalized UV, corresponding
  exactly to the 10px/20px cell edge), eliminating any possibility of the test itself introducing sampling
  ambiguity at its own crop boundary.

## Final Assessment

A precise, correctly-designed cropping test whose two discriminating check points were independently confirmed
to genuinely catch the "sourceRectangle ignored" failure mode from two different angles; production code and
test expectations agree exactly.
