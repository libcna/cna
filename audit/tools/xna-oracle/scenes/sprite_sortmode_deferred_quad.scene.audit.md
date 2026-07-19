# Audit: tools/xna-oracle/scenes/sprite_sortmode_deferred_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/sprite_sortmode_deferred_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SpriteBatch`/`SpriteSortMode.Deferred` baseline, part of the D9-93 multi-sprite
  sort-mode corpus
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Draws two overlapping sprites (RED and GREEN, different depths) under `SpriteSortMode.Deferred`
(draw-call order preserved, no depth reordering), establishing the baseline this scene's
`backtofront`/`fronttoback` siblings are contrasted against.

## Executive Verdict
Correct fixture, and directly responsible for surfacing a real, previously-undetected D3D9 backend
bug (see Cross-File Observations) — genuinely valuable, not just a passive baseline.

## Checklist Results
- `spritebatchmode`/`spritedraw` entries (insertion order, depth, color per sprite) match
  `README.md`'s documented `SpriteDrawEntry` key semantics.
- Confirmed pixel-perfect per `README.md`'s status log, post-fix.

## Detailed Findings
None currently open.

## Cross-File Observations
This scene (together with `sprite_sortmode_backtofront_quad.scene` and
`sprite_sortmode_fronttoback_quad.scene`) surfaced a real, previously-undetected D3D9 bug:
`D3D9SpriteBatchBackend::BuildMatrixTransformEXT`'s projection used
`CreateOrthographicOffCenter(0,W,H,0,0,zFarPlane=1)`, giving `Z'=-layerDepth` — outside Direct3D 9's
valid `[0,1]` clip-space Z range for any `layerDepth > 0`, silently clipping the GREEN sprite away
entirely regardless of sort mode (all three scenes rendered identically, RED-only, before the fix).
Fixed with `zFarPlane=-1`. Per `README.md`, this fix was mutation-tested: reverting it caused the
corresponding CTest checks to fail, confirming the fix (not a coincidental pass) is what makes these
scenes correct.

## Missing or Weak Tests
N/A — data fixture; the actual pass/fail assertion lives in the consuming CTest (out of this
shard's file list).

## Positive Findings
A concrete example of this oracle tool catching a real, previously-unknown production bug — direct
evidence the infrastructure delivers value, not just theoretical assurance.

## Final Assessment
No open findings. Historically significant: this scene helped discover and mutation-verify a real
D3D9 Z-clipping bug fix.
