# Audit: tools/xna-oracle/scenes/sprite_sortmode_fronttoback_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/sprite_sortmode_fronttoback_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SpriteBatch.SpriteSortMode.FrontToBack`; one of the three scenes that
  surfaced the real D3D9 sprite Z-clipping bug
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Draws the same RED/GREEN sprite pair under `SpriteSortMode.FrontToBack`, additionally varying
insertion order relative to its siblings to prove the reorder is genuinely depth-driven rather than
an artifact of draw-call insertion order.

## Executive Verdict
Correct fixture, directly implicated (along with its `deferred`/`backtofront` siblings) in
surfacing and mutation-verifying the real D3D9 Z-clipping bug.

## Checklist Results
- Same two-sprite depth setup as its siblings, with insertion order deliberately varied (confirmed
  by comparing against the other two `sprite_sortmode_*` files) — a genuine test that sorting is
  depth-driven, not insertion-order-driven.
- Confirmed pixel-perfect per `README.md`'s status log, post-fix, with mutation-testing
  verification.

## Detailed Findings
None currently open.

## Cross-File Observations
See `sprite_sortmode_deferred_quad.scene.audit.md` for the shared Z-clipping bug history.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Deliberately varies insertion order versus its siblings — a genuinely more rigorous test than simply
repeating the same insertion order with a different sort-mode flag.

## Final Assessment
No open findings.
