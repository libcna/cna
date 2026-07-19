# Audit: tools/xna-oracle/scenes/sprite_sortmode_backtofront_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/sprite_sortmode_backtofront_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SpriteBatch.SpriteSortMode.BackToFront`; one of the three scenes that
  surfaced the real D3D9 sprite Z-clipping bug
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Draws the same two overlapping RED/GREEN sprites as `sprite_sortmode_deferred_quad.scene` under
`SpriteSortMode.BackToFront` (deepest drawn first), confirming depth-based reordering matches real
XNA.

## Executive Verdict
Correct fixture, directly implicated (along with its `deferred`/`fronttoback` siblings) in
surfacing and mutation-verifying the real D3D9 Z-clipping bug documented in
`sprite_sortmode_deferred_quad.scene`'s own audit report.

## Checklist Results
- Same RED/GREEN two-sprite geometry/depth setup as its siblings, only `spritesortmode` differs
  (verified by comparing all three `sprite_sortmode_*` files) — a genuine, isolated single-variable
  comparison.
- Confirmed pixel-perfect per `README.md`'s status log, post-fix, with mutation-testing
  verification (reverting the D3D9 `zFarPlane` fix caused this scene's CTest check to fail).

## Detailed Findings
None currently open.

## Cross-File Observations
See `sprite_sortmode_deferred_quad.scene.audit.md` for the shared Z-clipping bug history all three
`sprite_sortmode_*` scenes contributed to surfacing.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Minimal-diff design (only `spritesortmode` varies across the three sibling scenes) gives clean,
isolated discriminating power.

## Final Assessment
No open findings.
