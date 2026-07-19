# Audit: tools/xna-oracle/scenes/sprite_flipped_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/sprite_flipped_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SpriteBatch` `SpriteEffects` (`FlipHorizontally`/`FlipVertically`); the other
  of the two scenes that proves the D3D9 half-pixel offset is necessary
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `SpriteBatch.Draw`'s `SpriteEffects` flip flags with the same four-color texture as
`sprite_rotated_quad.scene`, giving genuine discriminating power for both the flip logic itself and
(per `README.md`'s documented mutation-testing account) the D3D9 half-pixel offset.

## Executive Verdict
Correct, high-value fixture, directly implicated in the same mutation-testing finding as
`sprite_rotated_quad.scene`: removing the D3D9 half-pixel offset caused this scene to diverge from
real XNA by 4800/65536 pixels.

## Checklist Results
- `spriteeffects` key present with a non-`None` flip value (verified by reading the actual key/value
  pairs).
- Confirmed pixel-perfect per `README.md`'s status log (with the half-pixel offset correctly
  present).

## Detailed Findings
None.

## Cross-File Observations
Directly responsible (together with `sprite_rotated_quad.scene`) for confirming the D3D9 half-pixel
offset is genuinely necessary.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Same genuine, demonstrated discriminating power as `sprite_rotated_quad.scene`.

## Final Assessment
No findings.
