# Audit: examples/bgfx_spritebatch_scale_test.cpp

## Metadata

- Source file: `examples/bgfx_spritebatch_scale_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SpriteBatch::Draw`'s scalar and `Vector2` scale overloads pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_spritebatch_scale …)` / `cna_register_backend_test(NAME
  Bgfx_SpriteBatch_Scale …)`, `cmake/Tests/BgfxTests.cmake:774-776`).
- XNA/FNA relevance: direct — `SpriteBatch.Draw(Texture2D, Vector2, Rectangle?, Color, float, Vector2, float,
  SpriteEffects, float)` (scalar scale) and its `Vector2 scale` sibling overload.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`
  (scalar-scale overload, lines 296-312; `Vector2`-scale overload, lines 314-330).

## Purpose

5-check pixel test (Task 805, a Bgfx port of Task 418's EasyGL/Vulkan test) proving both scale overloads compute
the destination rectangle correctly, and — critically for the `Vector2` overload — that the X and Y scale
factors are applied to the *correct* axis independently (a 20×20 solid-Red texture scaled by `(2.0, 4.0)` must
produce a 40×80 destination, not 40×40 or 80×80). Draw 1 (scalar `3.0f`) at `(50,50)` → expected 60×60 at
x:[50,110), y:[50,110). Draw 2 (`Vector2(2,4)`) at `(200,50)` → expected 40×80 at x:[200,240), y:[50,130). The
file's own comment explains its two "just past edge" check points are deliberately chosen to fail differently
under 2 plausible bugs (using only `scale.X` for both axes → edge at y=90 instead of 130; using only `scale.Y`
for both axes → edge at x=280 instead of 240).

## Executive Verdict

**Healthy** — traced both overloads in `SpriteBatch.cpp` and confirmed the destination-rectangle math exactly
matches the test's hand-derived expectations, with the scalar and `Vector2` overloads sharing (correctly)
extremely similar code (`dw*scale`/`dh*scale` vs. `dw*scale.X`/`dh*scale.Y`).

## Checklist Results

### API / XNA / FNA parity
Both overloads exercised here (`Draw(tex, position, optional<Rect>, color, rotation, origin, scalarScale,
effects, depth)` and its `Vector2 scale` counterpart) match FNA's two corresponding `SpriteBatch.Draw` overloads
— FNA's scalar-scale overload is itself documented as a convenience wrapper around the `Vector2` one
(`new Vector2(scale, scale)`), and this audit confirmed CNA's own scalar overload (`SpriteBatch.cpp:296-312`)
independently computes `dw*scale`/`dh*scale` rather than delegating — a harmless implementation-detail
divergence (not incorrect: `dw*scale == dw*Vector2(scale,scale).X` always), noted but not flagged as a defect.

### Behavioral correctness
- Both textures use `sourceRectangle = std::optional<Rectangle>{}` (unset) ⇒ `SpriteBatch.cpp:305-307`/
  `323-325` falls back to the full texture bounds (`Rectangle(0,0,w,h)`, `dw=w=20, dh=h=20`).
- Draw 1: `pushSprite` destination = `Rectangle(50, 50, static_cast<int>(20*3.0f), static_cast<int>(20*4.0f
  or 3.0f))` → `(50,50,60,60)`. Exact integer result (`20*3.0f=60.0f`, no truncation ambiguity) — matches the
  test's expected x:[50,110), y:[50,110).
- Draw 2: destination = `(200, 50, static_cast<int>(20*2.0f), static_cast<int>(20*4.0f))` = `(200,50,40,80)` —
  exact, matches expected x:[200,240), y:[50,130).
- Verified the 5 check points against these two rectangles: (100,100) is inside Draw 1's rect → Red; (115,80)
  is past Draw 1's right edge (x=110) at y=80 (inside Draw 1's y-range) → Green (background); (220,110) is
  inside Draw 2's rect (x:[200,240), y:[50,130)) → Red, and is specifically *outside* what an X-for-both-axes
  bug would produce (that bug's rect would be `(200,50,40,40)`, whose y-range `[50,90)` excludes y=110 — so
  this check point would read background Green under that bug, correctly discriminating it); (245,70) is past
  Draw 2's real right edge (x=240) at y=70 (inside both the correct and buggy y-ranges) → Green, and is
  specifically *inside* what a Y-for-both-axes bug would produce (that bug's rect would be `(200,50,80,80)`,
  whose x-range `[200,280)` includes x=245 — so this check point would incorrectly read Red under that bug,
  correctly discriminating it); (50,250) is far from both sprites → Green.
- Both discriminating bugs the file's own comment names are independently confirmed to actually fail at the
  chosen check points, not merely asserted.

### C++ correctness
`static_cast<intcs>(dw * scale)` truncates toward zero; for this test's exact integer inputs (20×3, 20×2, 20×4)
there is no fractional truncation to worry about, so no rounding-direction ambiguity affects this specific test
(a general concern for non-integer scale factors, but out of scope for this file's chosen values).

### Testing
The 5 checks are proportionate and specifically designed as a differential test against 2 named plausible bugs
— genuinely stronger than a bare "does it render something" smoke test.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW defects found in this file.

## Missing or Weak Tests

- Non-integer scale factors (e.g. `1.5f`) that would exercise the `static_cast<intcs>` truncation-vs-rounding
  behavior are not covered by this file (nor, per a quick check, elsewhere for the `Vector2` overload
  specifically) — a minor gap, since FNA's own reference behavior for a non-integer scaled destination
  dimension is itself just float geometry rasterized by the GPU, not a byte-precise C# integer computation, so
  this is a low-value gap rather than a parity risk.
- Rotation combined with non-uniform scale is not exercised in this file (each of the sibling
  rotation/scale/sourcerect tests isolates one parameter at a time) — reasonable given the project's evident
  pattern of one-concern-per-file pixel tests in this shard.

## Positive Findings

- The two discriminating "just past edge" check points were independently verified (by this audit, not merely
  taken from the comment) to actually distinguish the correct implementation from both of the two specific
  bugs the file names — a genuinely strong test design, not a boilerplate rectangle-contains check.
- Both overloads' destination-rectangle math was traced end-to-end in `SpriteBatch.cpp` and found to produce
  exactly the rectangles the test expects, with no discrepancy.

## Final Assessment

A well-targeted, deliberately adversarial test against two specific plausible implementation bugs for the
non-uniform `Vector2` scale overload; production code and test expectations agree exactly in every traced case.
