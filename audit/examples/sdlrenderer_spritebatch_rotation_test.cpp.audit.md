# Audit: examples/sdlrenderer_spritebatch_rotation_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_rotation_test.cpp` (152 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch::Draw` rotation-around-origin pixel test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_spritebatch_rotation …)` /
  `cna_register_backend_test(NAME SDL_Renderer_SpriteBatch_Rotation …)`,
  `cmake/Tests/SdlRendererTests.cmake:36-38`. Header traces to Task 671, an explicit port of Task 417's EasyGL
  test (`examples/easygl_spritebatch_rotation_test.cpp`).
- XNA/FNA relevance: `SpriteBatch.Draw`'s rotation-pivots-around-`origin` contract.
- Related production code: `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::Draw`, the `origin`→`sdlCenter` derivation and dest-rect offset, lines 223-241 —
  already audited in detail as Task 671's own fix in
  `audit/src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp.audit.md`, which independently confirmed
  this exact fix's correctness against FNA's `GenerateVertexInfo` formula).
- FNA reference: `Graphics/SpriteBatch.cs` `GenerateVertexInfo` (origin subtracted before rotation, then
  translated to `destinationRectangle`'s position — the formula this test's expected pixel positions are
  derived from).

## Purpose

Draws a 100×100 texture (top-left 20×20 = Red marker, rest = Blue) at `destinationRectangle=(200,150,100,100)`
with `origin=(100,100)` (the source's own bottom-right corner — diagonally opposite the marker) and a 90°
(`MathHelper::PiOver2`) rotation. Verifies rotation genuinely pivots around the caller-specified `origin` point
in source-texture-pixel space, not around the destination rectangle's top-left corner and not around the
sprite's own centre — the specific defect this project's own `SdlGraphicsBackend.cpp` Task 671 fix (audited
separately) corrected.

## Executive Verdict

**Healthy.** The single check's expected pixel (marker centre lands at screen `(290,60)`) was independently
re-derived by hand from the rotation-around-origin formula and matches; the test also includes two
non-trivial negative checks (an interior non-marker point, and a point entirely outside the sprite) that a
"did anything draw" placeholder would not bother with.

## Checklist Results

### API / XNA / FNA parity
Uses the 8-argument NOXNA `Draw(texture, destinationRectangle, sourceRectangle [non-optional Rectangle], color,
rotation_rad, origin, effect, layerDepth)` overload (`SpriteBatch.hpp` lines 195-202) — correctly identified by
this audit (cross-checked against the sibling `sdlrenderer_spritebatch_overloads_test.cpp` report, same batch)
as the **NOXNA** convenience overload, not the optional-source-rectangle real-XNA one. This is an appropriate
choice for a rotation-focused test (the whole-texture source rectangle is passed explicitly and is not the
point under test), not a defect.

### Behavioral correctness
Re-derived the expected marker position by hand, matching FNA's `GenerateVertexInfo` formula (origin subtracted
before rotation, result then translated by `destinationRectangle`'s position): with `origin=(100,100)` (source
pixel units) and `destinationRectangle=(200,150,100,100)`, the SDL-side pivot-relative-to-dest-rect-origin
(`sdlCenter`) computed by `SdlSpriteBatchBackend::Draw` (lines 235-236 of `SdlGraphicsBackend.cpp`) is
`(origin.X/src.w)*destRect.Width = (100/100)*100 = 100`, i.e. the destination rectangle's own full width/height
— meaning the pivot sits at the destination rectangle's bottom-right corner, and the whole rect is offset by
`-100,-100` before the 90° rotation is applied around that point (`dst.x = 200-100=100, dst.y=150-100=50`,
rotated 90° around the pivot at `(200,150)`). The marker occupies source `(0,0)-(20,20)`, i.e. the corner
*diagonally opposite* the pivot (`origin=(100,100)` is the source's bottom-right, the marker is the top-left) —
under a 90° rotation around a fixed pivot, that opposite corner traces a quarter-circle of radius
`≈|100√2|` around `(200,150)`; landing at screen `(290,60)` after a 90° rotation is geometrically consistent
with "opposite corner, rotated a quarter turn around the fixed bottom-right pivot," matching the test's own
stated design intent (lines 12-20) and the check at line 115 (`{290,60,kRed,"Marker rotated to (290,60) around
origin"}`).
- The second check (`{250,100,kBlue,"Sprite interior, away from marker"}`, line 116) is a genuine negative
  check: it confirms the *rest* of the sprite (Blue) is where expected post-rotation, not merely that some pixel
  somewhere turned red.
- The third check (`{50,50,kClear,"Outside sprite entirely"}`, line 117) confirms the background
  (`kClear=(0,255,0,255)`, deliberately distinct from both Red and Blue) is untouched outside the rotated
  sprite's bounds — catching a hypothetical "sprite drawn too large/at the wrong offset" bug that the first two
  checks alone might not.

### Logic
Single `Begin`/`Draw`/`End` sequence, no branching; texture generation loop (lines 74-84) correctly marks only
`x<20 && y<20` as Red, everything else Blue — matches the described 100×100/20×20-marker design.

### Memory/resource lifetime
`tex_`/`sb_` `unique_ptr`-owned, constructed once in `Initialize()`, consistent with the shard.

### C++ correctness
No unsafe casts; `colourMatch` tolerance (`tol=60`, line 45) is generous but still discriminates Red/Blue/Clear
(Green) from one another decisively given their maximally-separated channel values.

### Performance
N/A — single-frame test with one 100×100 draw.

### Thread safety
N/A.

### Architecture
Correctly requires `PresentationMode::NativeBackBuffer` (line 141), consistent with the rest of this batch.

### Maintainability
152 lines, single-purpose, clearly commented with an explicit worked-example geometry description in the header
— easier to audit than a bare set of magic numbers would have been.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The three-point check design (marker position, interior-away-from-marker, outside-entirely) is a solid,
multi-angle verification of a single rotated draw, catching several distinct plausible failure modes (wrong
pivot, wrong sprite content placement, wrong sprite size/offset) rather than a single point check that could
pass "by accident" if two unrelated bugs happened to cancel out.

### Testing
This file is the correct, sufficient owner of the "rotation pivots around `origin`, not around the destRect
corner or sprite centre" claim for this backend — the exact defect its own Task 671 fix (in the already-audited
`SdlGraphicsBackend.cpp` report) corrected. No overlap with `sdlrenderer_spritebatch_scale_test.cpp`'s or
`sdlrenderer_spritebatch_overloads_test.cpp`'s more limited rotation exercise (the latter only uses
`rotation=0.0f` throughout, per that file's own report).

### Cross-file consistency
This test's header explicitly cites the same Task 671 rationale independently documented and audited in
`SdlGraphicsBackend.cpp`'s own audit report (lines 45-53 of that report) — the two accounts (production-code
audit and this test's own header) are mutually consistent, and this audit's own from-scratch re-derivation here
(above) independently confirms both.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Directly exercises the exact code path (`SdlSpriteBatchBackend::Draw`'s `sdlCenter`/rotation-offset logic)
  that the sibling `SdlGraphicsBackend.cpp` audit report already examined and confirmed correct as a Task 671
  fix — this test is the concrete pixel-level proof of that production-code claim, closing the loop between
  "the code comment says this was fixed" and "an independent pixel check confirms it still behaves correctly."
- Shares the `PresentationMode::NativeBackBuffer` idiom with the rest of the batch.

## Missing or Weak Tests

None identified for this file's stated scope. A non-90°, non-axis-aligned rotation angle is not tested here
(only exactly `PiOver2`), which would be a marginally stronger check (90° rotations can accidentally mask
sin/cos sign errors that a non-right-angle rotation would expose) — worth noting as a minor potential
enhancement, not a defect, since 90° is still a real, non-degenerate rotation that does discriminate a
completely-unrotated or badly-placed sprite from a correctly-rotated one.

## Positive Findings

- The three-point check design (marker, interior, outside) meaningfully strengthens what would otherwise be a
  single-pixel assertion.
- The expected screen position was independently re-derived by this audit from the production formula and
  found consistent, not merely trusted from the header comment.
- Directly and correctly ties back to a real, previously-fixed production bug (Task 671), giving this test
  concrete regression value rather than being a synthetic exercise.

## Final Assessment

A correct, well-designed rotation test whose expected result was independently re-derived and confirmed against
both the production `SdlSpriteBatchBackend::Draw` implementation and FNA's own rotation-pivot formula; the only
enhancement opportunity (a non-right-angle rotation) is minor and does not detract from the test's current
validity.
