# Audit: examples/demo_2d/src/Game1.cpp

## Metadata
- Source file: `examples/demo_2d/src/Game1.cpp` (342 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_2d` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises `SpriteBatch::Draw` (all overload families: dest-rect, dest-rect
  with rotation/origin/effects/layerDepth), `SamplerState` (Linear/PointClamp/LinearWrap and a
  hand-built `Mirror` state), `BlendState::AlphaBlend`, `SpriteSortMode::Deferred`
- Related production code: `SpriteBatch.cpp`/`SpriteFont.cpp` (already audited this session — the
  `SpriteEffects` 4th-flag-combination out-of-bounds-read HIGH finding lives there, not here)

## Purpose
Implements the flyer-spawning/update/draw loop and the WebGPU 2D validation scene (5 sprites in
one `Begin`/`End` batch exercising `SpriteEffects::FlipHorizontally`/`FlipVertically` plus rotation/
origin/tint, followed by 3 more batches each with a different `SamplerState` to visibly distinguish
clamp/wrap/mirror address-mode behavior on a deliberately-out-of-`[0,1]` UV rectangle).

## Executive Verdict
Correct. `~Game1()` deletes `spriteBatch` exactly once and nulls it — clean, matching the header's
raw-pointer ownership. `DrawWebGpu2DValidationScene()`'s calls only ever pass
`SpriteEffects::None`/`FlipHorizontally`/`FlipVertically` — never the combined
`FlipHorizontally|FlipVertically` 4th value, so this file does not itself trigger the
already-confirmed `axisDirX`/`axisDirY` 3-vs-4-entry out-of-bounds table read in
`SpriteBatch.cpp`/`SpriteFont.cpp` (that finding remains specific to code paths that do construct
the combined flag, e.g. `examples/sdlgpu_2d_test.cpp:126`).

## Checklist Results
- `LoadContent()` wraps the `SoundEffect` load in `try {...} catch (...) {...}` with an explicit
  comment ("Audio might fail in some environments, ignore for graphics demo") — a deliberately
  narrow, justified catch-all for a genuinely optional dependency in a demo whose real purpose is
  graphics, not audio.
- `UpdateFlyers()`'s erase-remove-if / respawn-to-`minFlyers` / probabilistic-spawn-to-`maxFlyers`
  sequence is internally consistent and bounded — no unbounded growth risk.
- `KeepFlyerInsideBounds()` correctly reflects velocity (`vx = -vx`) on every axis independently,
  not just clamping position — physically sensible bounce behavior.

## Detailed Findings
None.

## Cross-File Observations
The deliberately out-of-`[0,1]` `repeated` rectangaged UV rectangle (`Rectangle{0, 0, bounds.Width
* 2, bounds.Height}`) is a good complement to the confirmed `SamplerState.AddressW`/address-mode
findings already audited in the `xna-graphics` shard — this file is a real, visual, human-driveable
exercise of the same clamp/wrap/mirror address modes, not just a unit test assertion.

## Missing or Weak Tests
Not applicable — this is itself a manual/visual-validation demo, not a unit-tested production file.

## Positive Findings
Correct raw-pointer ownership (`delete spriteBatch` in the destructor) and a well-justified, narrow
`catch (...)` around the one genuinely-optional dependency (audio) in an otherwise graphics-focused
demo.

## Final Assessment
No findings.
