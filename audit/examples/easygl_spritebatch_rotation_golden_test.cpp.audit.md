# Audit: examples/easygl_spritebatch_rotation_golden_test.cpp

## Metadata

- Source file: `examples/easygl_spritebatch_rotation_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — SpriteBatch rotation-around-origin golden-image test
- File type: single-frame `PixelTestGame`-subclass executable (`common/PixelTestGame.hpp` helper,
  Task 461/463's shared infrastructure)
- XNA/FNA relevance: exercises `SpriteBatch::Draw`'s rotation/origin pivot semantics, same
  underlying feature as FNA's `SpriteBatch.cs` `GenerateVertexInfo` corner-rotation formula.
- Build/registration: `cmake/Tests/EasyGLTests.cmake` → `cna_test_easygl_spritebatch_rotation_golden`
  (`EasyGL_SpriteBatch_Rotation_Golden`).
- Golden fixture: `examples/golden/easygl_spritebatch_rotation_golden_test.png` — **confirmed present
  on disk** (84 bytes, an 8×8 PNG), so `CompareGoldenImage()` has a real reference to compare against
  rather than silently no-oping.
- Main related production files: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLSpriteBatchBackend::Draw`, lines 1189-1273), `examples/common/PixelTestGame.hpp`
  (`CompareGoldenImage`/`ExpectPixel`).

## Purpose

A "Task 465" golden-image consumer that intentionally re-runs the *exact same scene* as
`easygl_spritebatch_rotation_test.cpp` (Task 417) — a 100×100 texture with a 20×20 red marker in its
top-left, drawn at `(200,150,100,100)` with `origin=(100,100)` (the texture's own bottom-right
corner) rotated 90° — but checks the result via `PixelTestGame::CompareGoldenImage()` against a
checked-in 8×8 reference PNG cropped around the expected marker location, instead of Task 417's 3
hand-picked single-pixel samples.

## Executive Verdict

**Healthy.** The scene setup is a byte-for-byte match of the already-verified Task 417 scene (cross-
checked line-by-line below), the golden fixture genuinely exists and is the right size, and the test
correctly layers a hardcoded cross-check on top of the golden-image comparison rather than trusting
the PNG blindly.

## Checklist Results

### API / XNA / FNA parity
Uses the 8-arg `SpriteBatch::Draw(texture, destRect, srcRect, color, rotation, origin, effects,
layerDepth)` overload — same overload, same parameter values (`Rectangle(200,150,100,100)`,
`Rectangle(0,0,100,100)`, `Color::White`, `MathHelper::PiOver2`, `Vector2(100,100)`,
`SpriteEffects::None`, `0.0f`) as `easygl_spritebatch_rotation_test.cpp`'s Task 417 scene — confirmed
identical via direct comparison of both files' `Draw()` call sites.

### Behavioral correctness
Independently re-derived the rotation math against `EasyGLSpriteBatchBackend::Draw` (lines 1234-1258):
for a source corner `(sx,sy)`, `pNx = (sx-ox)*scaleX`, then rotated by `(cosR,sinR)` and translated
by `(dx,dy)`. With `origin=(100,100)` equal to the source rect's own bottom-right corner and
`scaleX=scaleY=1` (100×100 dest ÷ 100×100 src), the affine map is a pure rotation+translation, so it
applies uniformly to any interior source point, not just the 4 quad corners — meaning the marker's
center `(10,10)` maps through the same formula as the corners. Re-computing: `cornerX=10-100=-90`,
`cornerY=10-100=-90`; at `rotation=PiOver2` (`sin=1,cos=0`): `X'=200+(-90)*0-(-90)*1=290`,
`Y'=150+(-90)*1+(-90)*0=60` — confirms the file's own stated marker location `(290,60)` and the
golden crop region `Rectangle(290-4, 60-4, 8, 8)` = `(286,56,8,8)`, an 8×8 box centered exactly there.

### Logic
Two independent checks are layered (lines 78-82): `ExpectPixel("marker-vs-task417-expected", ...,
kRed, tolerance=60)` — a hardcoded cross-check against Task 417's own derivation, deliberately
independent of whatever is actually baked into the golden PNG — followed by `CompareGoldenImage(...)`
against the checked-in reference. This means a corrupted/stale golden PNG alone would not be enough to
mask a real regression (the hardcoded `ExpectPixel` would still catch it), and conversely a mistaken
hardcoded-expectation typo would still be caught by the golden comparison — a genuinely more robust
design than either check alone, matching the stated "Task 464" rationale in the header comment.

### Memory/resource lifetime
`tex` and `sb` are stack-local values inside `RunTest()` (not heap `unique_ptr` members like most
other files in this shard) — this is fine since `PixelTestGame::RunTest()` runs synchronously to
completion within a single `Draw()` call before `Exit()`; no lifetime hazard.

### C++ correctness
`std::vector<Color> pixels(100*100, kBlue)` + a nested loop overwriting the top-left 20×20 to `kRed`
— straightforward, bounds-safe (loop bounds `x,y < 20` strictly inside the 100×100 allocation).

### Performance
N/A — single-frame test; 10,000-pixel `SetData` call is trivial at this scale.

### Thread safety
N/A.

### Architecture
Good demonstration of `PixelTestGame`'s intended reuse pattern (Task 461/463): this file has no
hand-rolled `main()`/`Game` boilerplate — it only implements `RunTest()` and lets
`CNA::Examples::RunPixelTest<SpriteBatchRotationGoldenTest>()` do the rest, exactly per the helper's
own documented usage example.

### Maintainability
Header comment explicitly cites which prior task/file this scene is reused from (Task 417,
`easygl_spritebatch_rotation_test.cpp`) and why the golden-image crop is 8×8 with a 4px margin
("comfortably clear of any rasterization edge effects") — a specific, justified magic number rather
than an arbitrary one.

### Portability
N/A.

### Robustness
`CompareGoldenImage` itself (in `PixelTestGame.hpp`, not this file) validates the golden image's
width/height match the requested region before comparing pixel data (`PixelTestGame.hpp` lines
178-185) — this file's `Rectangle(286,56,8,8)` region is confirmed consistent with the actual PNG's
dimensions (an 8×8 file, matches).

### Testing
This file is itself a test. Its main testing-completeness gap is noted below.

## Detailed Findings

No correctness defects found. One test-design observation:

### F1 — Golden image and hardcoded cross-check use the same tolerance and largely the same region, offering limited independent signal beyond Task 417 itself

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: lines 78-82 (`ExpectPixel`/`CompareGoldenImage` calls)
- Evidence: both checks use `tolerance=60` (reusing "Task 417's own tolerance" per the header
  comment) and both are centered on the same `(290,60)` point — the golden image is a superset (8×8
  region vs. a single pixel) but at the same generous tolerance, so it mostly re-confirms the same
  single fact (marker relocated correctly) rather than adding a materially different assertion (e.g.
  a tighter tolerance, or a check of the marker's edge/shape rather than just its interior color).
- Why it matters: minor — this is an intentional, documented design choice (the file says as much),
  not an oversight; a tighter-tolerance golden comparison would catch subtler sub-pixel rendering
  regressions (anti-aliasing, rounding) that this test's generous tolerance would currently miss.
- FNA/XNA comparison: N/A — this is CNA-side test infrastructure, not an XNA API surface.
- Suggested future action (not implemented by this audit): consider a second, tighter-tolerance golden
  region if sub-pixel rotation-rounding regressions become a real concern for this feature.

## Cross-File Observations

- This file and `easygl_spritebatch_rotation_test.cpp` (Task 417) are deliberately near-duplicates by
  design (golden-image *consumer* vs. the original hand-picked-pixel test) — auditing them together
  makes it easy to confirm neither has silently drifted from the other; confirmed here that the scene
  setup is identical.
- Demonstrates `PixelTestGame`'s intended future direction (per that header's own comment: "no
  existing examples/*.cpp file has been modified to use it... future single-frame pixel tests can opt
  in") — this file is one of a presumably small number of adopters; worth checking during a later
  cross-cutting pass how many of the ~570 example tests in this project could be mechanically
  migrated to this helper vs. how many remain hand-rolled.

## Missing or Weak Tests

- No test in this shard appears to exercise `CompareGoldenImage`'s own `CNA_UPDATE_GOLDEN` regeneration
  path or its width/height-mismatch failure branch (`PixelTestGame.hpp` lines 178-185) — that is
  arguably an infrastructure-file concern rather than this test file's, but is worth flagging since
  this file is one of the only present consumers exercising the helper end-to-end.

## Positive Findings

- Genuinely re-derives and confirms the same rotation math independently rather than trusting the
  reused Task 417 scene blindly.
- Layers a hardcoded cross-check on top of the golden-image comparison specifically so neither a bad
  PNG nor a bad hardcoded constant alone can produce a false pass — a deliberate, well-reasoned
  redundancy (Task 464 pattern per its own comment).
- Golden PNG fixture confirmed present and correctly sized on disk, not a dangling reference to a
  missing file.

## Final Assessment

A correctly-derived, well-designed golden-image regression test that faithfully reuses an
already-verified rotation scene and adds a second, independent layer of verification on top of it. No
functional defects found; the one note (F1) is a minor test-strength observation, not a bug.
