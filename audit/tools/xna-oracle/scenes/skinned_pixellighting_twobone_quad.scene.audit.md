# Audit: tools/xna-oracle/scenes/skinned_pixellighting_twobone_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/skinned_pixellighting_twobone_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SkinnedEffect` + `PreferPerPixelLighting=true`, two-bone case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `SkinnedEffect`'s per-pixel-lighting shader permutation combined with 2-bone skinning.

## Executive Verdict
Correct, well-targeted fixture.

## Checklist Results
- `preferperpixellighting=true` present alongside `weightspervertex=2`, both bone weights distinct
  (verified by reading the actual key/value pairs, consistent with `skinned_twobone_quad.scene`'s
  weight distribution).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Part of the 3-scene `SkinnedEffect` + per-pixel-lighting family anchored by
`skinned_pixellighting_quad.scene`.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Consistent, deliberate cross-product coverage of lighting mode × bone count.

## Final Assessment
No findings.
