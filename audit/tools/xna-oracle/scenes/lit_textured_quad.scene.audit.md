# Audit: tools/xna-oracle/scenes/lit_textured_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/lit_textured_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `BasicEffect` lighting + texturing, vertex-lighting path
  (`PreferPerPixelLighting=false`)
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `BasicEffect`'s combined per-vertex directional lighting + texturing shader path (one
light, `ambientcolor`/`light0*` keys), the default lighting mode.

## Executive Verdict
Correct, well-targeted fixture.

## Checklist Results
- `lighting=true`, `light0enabled=true`, `ambientcolor`/`light0diffuse`/`light0direction` all
  present and match `README.md`'s documented defaults/semantics.
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Direct per-vertex-lighting counterpart to `lit_textured_quad_pixellighting.scene` (identical scene
data, differing only in `PreferPerPixelLighting`); together these confirm both `BasicEffect`
lighting shader permutations match.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Minimal-diff pairing with its per-pixel-lighting sibling isolates exactly the one variable under
test.

## Final Assessment
No findings.
