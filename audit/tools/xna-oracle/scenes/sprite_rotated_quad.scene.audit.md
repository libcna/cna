# Audit: tools/xna-oracle/scenes/sprite_rotated_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/sprite_rotated_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SpriteBatch` rotation + `spriteorigin`; one of the two scenes that actually
  proves the D3D9 half-pixel offset is necessary
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `SpriteBatch.Draw`'s `rotation`/`origin` parameters with a non-trivial (four-color)
texture, giving this scene genuine discriminating power that `sprite_basic_quad.scene`'s single-texel
texture lacks.

## Executive Verdict
Correct, high-value fixture — per `README.md`'s documented mutation-testing account, this scene
(along with `sprite_flipped_quad.scene`) is one of the two in the corpus that actually detects the
D3D9 half-pixel offset: removing that offset caused this scene to diverge from real XNA by
4800/65536 pixels, correctly failing where `sprite_basic_quad.scene`'s single-texel case could not.

## Checklist Results
- `spriterotation`/`spriteorigin` present with non-trivial, non-axis-aligned values (verified by
  reading the actual key/value pairs).
- Four-color texture confirmed distinct from `sprite_basic_quad.scene`'s single-texel texture.
- Confirmed pixel-perfect per `README.md`'s status log (with the half-pixel offset correctly
  present).

## Detailed Findings
None.

## Cross-File Observations
Directly responsible (together with `sprite_flipped_quad.scene`) for confirming the D3D9 half-pixel
offset is genuinely necessary — see `sprite_basic_quad.scene`'s own audit report for the
contrasting non-discriminating case.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Concrete example of a scene with real, demonstrated discriminating power for a genuine backend
correctness concern (not merely plausible-looking coverage).

## Final Assessment
No findings.
