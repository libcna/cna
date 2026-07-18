# Audit: examples/sdlrenderer_sample_bouncing_sprite_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_sample_bouncing_sprite_test.cpp` (157 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — sample 2 of 5 in the "Task 730" minimal-sample series
  (CMake registration: `cmake/Tests/SdlRendererTests.cmake:427`)
- XNA/FNA relevance: exercises a real multi-frame `Update()`+`Draw()` game loop driving `SpriteBatch::Draw`'s
  `destinationRectangle`, proving gameplay-shaped code (not an isolated single-call check) renders correctly.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`,
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::Draw(texture, destRect, srcRect, color)`).
- Git provenance: `8205ce40`/`85d7cbe1` "feat(Task 730): port 5 minimal 2D-only samples..." — confirmed real.

## Purpose

`SdlBouncingSpriteSample` moves a 4x4 white sprite by a fixed per-`Update()` velocity, reflecting the velocity
component whenever the sprite's position would exceed the 16x16 backbuffer's bounds. After 5 `Update()` calls
it renders the sprite at its final position and reads back one pixel to confirm the sprite is genuinely drawn
there. A standalone `ComputeExpectedPosition()` function (lines 52-65) is provided specifically, per its own
comment, "so the test doesn't just check whatever `Update()` produced against itself."

## Executive Verdict

**Mostly healthy** — the end-to-end render pipeline claim (sprite moves and is drawn at wherever `Update()`
placed it, across 5 real frames) is genuinely verified. However, the file's own stated methodology for
avoiding self-referential testing does not actually achieve that goal: `ComputeExpectedPosition()` (lines
52-65) is a byte-for-byte duplicate of `Update()`'s own clamp/reflect formula (lines 101-108), not an
independent derivation — see F1.

## Checklist Results

### Purpose
Correctly placed; velocity/bounce logic is ordinary example-game content (not an XNA API), so no XNA parity
question applies to the physics itself — only to the `SpriteBatch`/`GraphicsDevice` calls surrounding it,
which are standard and correctly used.

### Behavioral correctness
Hand-traced both `Update()` (lines 97-109) and `ComputeExpectedPosition()` (lines 52-65) with
`kStartX=2, kStartY=2, kVelX=3, kVelY=2, kBackbufferSize=16, kSpriteSize=4` (`maxPos=12`), 5 iterations:
- i=0: x=5,y=4
- i=1: x=8,y=6
- i=2: x=11,y=8
- i=3: x=14→clamp 12, vx=-3; y=10
- i=4: x=12-3=9; y=10+2=12 (not >12, no clamp)

Final expected position `(9,12)` — confirmed by hand-derivation, matching what both functions would produce
identically (since they are the same code, see F1). The "genuinely bounced" check
(`expectedX != kStartX + kVelX*kFrameCount` i.e. `9 != 17`) correctly holds.

### Logic
See F1 for the central logic-duplication concern.

### C++ correctness
No unsafe casts; `pass_`/`fail_` counters and `check()` helper are consistent with the other files in this
batch.

### Testing
The final pixel-readback check (line 133) reads `Rectangle(expectedX+1, expectedY+1, 1, 1)` = `(10,13,1,1)`,
inside the sprite's actual `[9,13)×[12,16)` footprint — a genuine test that `SpriteBatch`/`SdlGraphicsBackend`
rendered the sprite at the coordinates `Update()` computed (this part is real and would catch a coordinate-
mapping or off-by-one rendering bug). What it does *not* test, contrary to the file's own header claim, is
described in F1.

## Detailed Findings

### F1 — `ComputeExpectedPosition()` is not actually independent of `Update()`'s formula; it is the same clamp/reflect logic copy-pasted, so the position-match check cannot catch a regression in the shared bounce formula itself

- Severity: LOW
- Confidence: HIGH (both functions were read side-by-side; they are line-for-line identical in structure)
- Category: test-coverage / correctness-of-test-claim
- Location/symbol: `ComputeExpectedPosition()` (lines 52-65) vs. `Update()` (lines 101-108)
- Evidence: the header comment (lines 16-18) states: *"The exact expected final position is independently
  computed in `ComputeExpectedPosition()` below (mirroring the same bounce formula) and compared against the
  sprite's actual rendered position via pixel readback."* Reading both functions:
  ```
  // ComputeExpectedPosition (lines 59-64)
  x += vx; y += vy;
  if (x < 0) { x = 0; vx = -vx; }
  if (x > maxPos) { x = maxPos; vx = -vx; }
  if (y < 0) { y = 0; vy = -vy; }
  if (y > maxPos) { y = maxPos; vy = -vy; }
  ```
  ```
  // Update (lines 102-107)
  spriteX_ += velX_; spriteY_ += velY_;
  if (spriteX_ < 0)      { spriteX_ = 0;      velX_ = -velX_; }
  if (spriteX_ > maxPos) { spriteX_ = maxPos; velX_ = -velX_; }
  if (spriteY_ < 0)      { spriteY_ = 0;      velY_ = -velY_; }
  if (spriteY_ > maxPos) { spriteY_ = maxPos; velY_ = -velY_; }
  ```
  These are the identical algorithm, not an independent re-derivation ("mirroring the same bounce formula" is
  an accurate self-description; "independently computed" in the same paragraph overstates it).
- Why it matters: the final assertion (`px` at `expectedX+1,expectedY+1` is white) only proves that
  `SpriteBatch`/`SdlGraphicsBackend` correctly render a sprite at whatever coordinates `Update()`'s formula
  (however it's written) produces — it cannot detect a bug *in* the clamp/reflect thresholds themselves (e.g.
  an accidental `>=` vs `>`, or a missing Y-axis clamp), because any such bug would be present in both
  `Update()` and `ComputeExpectedPosition()` identically (the developer would have to introduce it in one
  place and forget the other for this test to actually fail on that class of bug). This does not undermine the
  file's core, legitimate claim (the SDL_Renderer rendering pipeline correctly places a sprite driven by a real
  multi-frame game loop) — it only means the specific "independently computed" framing in the header comment
  is inaccurate, and a reader could mistake this for a stronger physics-correctness guarantee than the test
  actually provides.
- FNA/XNA comparison: N/A — bounce physics is example content, not an XNA API surface.
- Related files: none outside this file.
- Suggested future action (not implemented by this audit): either reword the header comment to accurately
  describe this as a rendering-pipeline consistency check (drop "independently computed"), or genuinely
  decouple `ComputeExpectedPosition()`'s implementation from `Update()`'s (e.g. compute via a closed-form
  reflection formula) so a shared-logic bug could actually be caught.

## Cross-File Observations

- Shares the same fixed-per-`Update()`-increment determinism convention and `PresentationMode::NativeBackBuffer`
  requirement with every other file in this batch.

## Missing or Weak Tests

See F1 — no additional coverage gap beyond the self-referential formula concern already described.

## Positive Findings

- The multi-frame (5 real `Update()`+`Draw()` cycles) methodology genuinely exercises repeated-call behavior
  that the narrower Task 667-729 single-Draw()-call tests (referenced in this file's own header) do not cover.
- The "genuinely bounced" guard (`expectedX != kStartX + kVelX*kFrameCount`) correctly ensures the test
  scenario actually forces a clamp/reflect event within the 5 frames, rather than trivially passing on a
  straight-line trajectory.

## Final Assessment

A solid end-to-end rendering-pipeline test whose only issue is an overstated methodology claim in its own
header comment (F1) — the underlying `Update()`→`Draw()`→pixel-readback chain it exercises is correctly
implemented and correctly verified for what it actually tests.
