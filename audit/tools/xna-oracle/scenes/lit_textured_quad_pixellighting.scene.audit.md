# Audit: tools/xna-oracle/scenes/lit_textured_quad_pixellighting.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/lit_textured_quad_pixellighting.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `BasicEffect` lighting + texturing, per-pixel-lighting path
  (`PreferPerPixelLighting=true`)
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `BasicEffect`'s per-pixel directional lighting shader permutation (a distinct real XNA
shader technique from the default per-vertex path), confirming both permutations match between
real XNA and CNA.

## Executive Verdict
Correct, well-targeted fixture.

## Checklist Results
- `preferperpixellighting=true` present, all other lighting keys identical to
  `lit_textured_quad.scene` (confirmed by comparing both files) — a genuine, isolated single-variable
  comparison.
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Minimal-diff sibling of `lit_textured_quad.scene`, isolating the per-pixel vs. per-vertex lighting
shader permutation as the only variable under test.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Deliberate minimal-diff design against its sibling scene.

## Final Assessment
No findings.
