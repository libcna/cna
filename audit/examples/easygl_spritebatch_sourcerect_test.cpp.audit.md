# Audit: examples/easygl_spritebatch_sourcerect_test.cpp

## Metadata

- Source file: `examples/easygl_spritebatch_sourcerect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — SpriteBatch source-rectangle cropping pixel test
  ("Task 419")
- File type: single-frame `Game`-subclass pixel-readback executable (hand-rolled `main()`)
- XNA/FNA relevance: exercises `SpriteBatch::Draw(texture, destinationRectangle, sourceRectangle,
  color)`'s cropping behavior — a core XNA `SpriteBatch` feature.
- Build/registration: `cmake/Tests/EasyGLTests.cmake` → `cna_test_easygl_spritebatch_sourcerect`
  (`EasyGL_SpriteBatch_SourceRectangleCropping`); also reused verbatim by
  `cmake/Tests/VulkanTests.cmake` → `cna_test_vulkan_spritebatch_sourcerect`.
- Main related production files: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (the 4-arg
  `Draw(texture, destinationRectangle, sourceRectangle, color)` overload, lines 235-244),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLSpriteBatchBackend::Draw`, UV computation lines 1213-1219).

## Purpose

Verifies that `SpriteBatch::Draw`'s `sourceRectangle` parameter genuinely crops which region of the
texture is sampled (and that region is then stretched to fill the destination rectangle), rather
than always sampling and stretching the whole texture regardless of what `sourceRectangle` says. Uses
a 20×20 texture split into a 2×2 grid of 10×10 solid-color quadrants (Red/Blue/Magenta/Yellow) and
selects only the top-right (Blue) quadrant via `sourceRectangle=(10,0,10,10)`, stretched into a 50×50
destination rectangle.

## Executive Verdict

**Healthy.** The 4-arg `Draw` overload passes `sourceRectangle` straight through to `pushSprite`
unmodified (confirmed by direct inspection), and the backend's UV computation
(`sourceRectangle.X/texW` etc.) correctly derives normalized texture coordinates from exactly that
rectangle — the test's premise and its two check points are both correct and appropriately
discriminating.

## Checklist Results

### API / XNA / FNA parity
Confirmed `SpriteBatch::Draw(const Texture2D&, const Rectangle& destinationRectangle, const
Rectangle& sourceRectangle, Color color)` (`SpriteBatch.cpp` lines 235-244) forwards `sourceRectangle`
unmodified into `pushSprite(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero,
SpriteEffects::None, 0.0f)` — no default-to-whole-texture fallback logic exists on this particular
overload's path (unlike the `std::optional<Rectangle>` overloads elsewhere in the same file, which do
have such a fallback for a `std::nullopt` source rect) — appropriate, since this overload's parameter
is a non-optional `const Rectangle&`.

### Behavioral correctness
Traced the UV derivation in `EasyGLSpriteBatchBackend::Draw` (lines 1213-1216):
`u1 = sourceRectangle.X / texW`, `u2 = (sourceRectangle.X + sourceRectangle.Width) / texW` (and the
`v` analogues) — for this test's `sourceRectangle=(10,0,10,10)` on a 20×20 texture, `u1=0.5, u2=1.0,
v1=0.0, v2=0.5`, i.e. exactly the texture's top-right quadrant (Blue, per the test's own color
layout) — confirming the crop selects the intended quadrant, not a shifted or inverted one.

### Logic
The two check points (lines 121-122) are specifically chosen to rule out the "no cropping, whole
texture stretched" bug via two *independent, differently-colored* tells: sampling near the
destination rectangle's top-left corner `(105,105)` would show the texture's own top-left cell (Red)
under that bug instead of the correct Blue, and sampling near the bottom-right corner `(145,145)`
would show the texture's own bottom-right cell (Yellow) instead of Blue. Because the two false
readings under the bug are two different, distinct wrong colors (not both wrong-in-the-same-way),
this test could not "accidentally pass" if cropping were subtly broken (e.g. cropped from the wrong
axis) — it is a more informative check than a single center-point sample.

### Memory/resource lifetime
`tex_` (20×20, one `SetData` call at `Initialize()`) — standard, no lifetime concerns.

### C++ correctness
The nested `for` loop building the 2×2 grid (lines 81-93) correctly computes `right = x>=10` and
`bottom = y>=10`, assigning colors in an if-chain (`Red` default, then overridden by
`!bottom&&right→Blue`, `bottom&&!right→Magenta`, `bottom&&right→Yellow`) — verified the 4 conditions
are mutually exclusive and exhaustive over the 2×2 grid, so every pixel gets exactly one color
assignment (no overlap, no gap) — confirmed by manual truth-table check:
`(bottom,right) = (F,F)→Red(default,unmodified), (F,T)→Blue, (T,F)→Magenta, (T,T)→Yellow`.

### Performance
N/A — single-frame test, 400-pixel `SetData` call.

### Thread safety
N/A.

### Architecture
No backend-specific code in the test itself — matches the shared-source-across-backends pattern
already confirmed for sibling files in this shard (this exact file is also compiled into a Vulkan
CTest target).

### Maintainability
Header comment (lines 1-27) explicitly explains why *two* check points (not one) are used and what
specific bug each independently rules out — good self-documentation for future maintainers.

### Portability
N/A.

### Robustness
`result_` defaults to `0` — same minor project-wide pattern as sibling files in this shard.

### Testing
This file is itself a test. See Missing or Weak Tests.

## Detailed Findings

No correctness defects found in this file.

## Cross-File Observations

- Same recurring `const_cast<SamplerState*>(&SamplerState::PointClamp)` pattern (line 108) as the
  `rotation`/`rotation_golden`/`scale`/`layerdepth` sibling files in this shard — root cause lives in
  `SpriteBatch.hpp`'s non-`const` `samplerState` parameter, outside this shard's scope.
- Confirmed verbatim cross-backend reuse again: `cmake/Tests/EasyGLTests.cmake` line 792
  (`cna_test_easygl_spritebatch_sourcerect`) and `cmake/Tests/VulkanTests.cmake` line 763
  (`cna_test_vulkan_spritebatch_sourcerect`).
- The production code path this test exercises (`EasyGLSpriteBatchBackend::Draw`'s UV derivation,
  lines 1208-1212) has its own comment explicitly noting no `[0,1]` clamp is applied — intentionally
  matching FNA's own unclamped division so a `sourceRectangle` extending past texture bounds produces
  out-of-range UVs governed by the bound `SamplerState`'s address mode (the classic XNA
  scrolling-background technique). This test's `sourceRectangle` stays fully in-bounds, so it does not
  exercise that specific documented behavior — see Missing or Weak Tests.

## Missing or Weak Tests

- No test in this file (or, as far as this file's own scope goes, apparently elsewhere in this
  8-file batch) exercises a `sourceRectangle` that extends past the texture's bounds to confirm the
  documented unclamped-UV/address-mode-governed behavior mentioned in
  `EasyGLSpriteBatchBackend::Draw`'s own comment (lines 1208-1212) — that specific, intentionally
  XNA-faithful edge case has no direct pixel-level regression test visible in this batch.
- As noted in the `scale` test's own report, cropping (`sourceRectangle`) and non-uniform `scale`
  together are not exercised by any single file in this shard — each is tested independently but not
  in combination.

## Positive Findings

- UV-derivation math independently confirmed correct by direct inspection of the backend's actual
  texture-coordinate computation, not merely restated from the test's comment.
- Two-point check design specifically chosen to produce two *differently*-colored failure signatures
  under the most likely regression ("no cropping"), which is a stronger design than a single
  pass/fail sample.

## Final Assessment

A correct, well-designed source-rectangle-cropping regression test whose premise and check points
both hold up against direct inspection of the actual UV-derivation code. No defects found; the
out-of-bounds/address-mode-governed sourceRectangle behavior documented in the backend remains
untested by this file (and, per this batch, by its siblings too).
