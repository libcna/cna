# Audit: tools/xna-oracle/scenes/sprite_basic_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/sprite_basic_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SpriteBatch` baseline scene; also the scene central to a documented
  mutation-testing methodology finding about the D3D9 half-pixel offset
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
The corpus's simplest `SpriteBatch` baseline: a single sprite drawn with a 1×1-texel texture,
default sampler/rotation/origin/effects.

## Executive Verdict
Correct as a baseline, but with a documented, non-obvious limitation worth being aware of when
reasoning about this scene's coverage claims: because its texture is a single texel, this scene is
structurally incapable of detecting the classic D3D9 half-pixel offset bug (there is nothing
adjacent to shift between). `README.md` documents this as a real mutation-testing finding: removing
the half-pixel offset entirely from `D3D9SpriteBatchBackend` left this scene's output unchanged
(pixel-perfect even with the offset absent — a false-positive "this proves correctness" trap), while
`sprite_rotated_quad.scene`/`sprite_flipped_quad.scene` (both using a four-color texture) correctly
diverged by 4800/65536 pixels once the offset was removed — confirming the offset is genuinely
necessary and that this scene alone cannot prove it.

## Checklist Results
- Sprite keys (`spritedestrect`, default sampler/rotation/origin) match `README.md`'s documented
  table.
- Confirmed pixel-perfect per `README.md`'s status log, both with and without the half-pixel offset
  (a documented non-discriminating case for that specific concern, not a defect in this scene's
  actual assertions).

## Detailed Findings
None. (The half-pixel-offset non-discrimination is a documented scope limitation of this one scene,
not a defect — the corpus correctly compensates with `sprite_rotated_quad.scene`/
`sprite_flipped_quad.scene`.)

## Cross-File Observations
Establishes the `SpriteBatch` baseline the rest of the sprite-family scenes build on; its own
documented blind spot for the half-pixel offset is precisely why the rotated/flipped sibling scenes
exist and use a multi-texel texture.

## Missing or Weak Tests
N/A — data fixture; the half-pixel-offset gap is filled by sibling scenes, not by this file.

## Positive Findings
The project's own transparency in documenting this scene's specific discriminating-power blind spot
(rather than claiming false completeness) is a strong positive signal of rigor.

## Final Assessment
No findings. Documented, correctly-compensated-for scope limitation regarding the D3D9 half-pixel
offset specifically.
