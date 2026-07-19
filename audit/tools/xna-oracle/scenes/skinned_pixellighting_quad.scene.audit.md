# Audit: tools/xna-oracle/scenes/skinned_pixellighting_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/skinned_pixellighting_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SkinnedEffect` + `PreferPerPixelLighting=true`, single-bone case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises the combination of `SkinnedEffect`'s per-pixel-lighting shader permutation with
single-bone skinning, confirmed distinct from `skinned_quad.scene`'s default per-vertex-lighting
permutation.

## Executive Verdict
Correct, well-targeted fixture.

## Checklist Results
- `preferperpixellighting=true` present alongside `weightspervertex=1` (explicit, matching the
  documented fix already present in `skinned_quad.scene`).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Anchors the 3-scene `SkinnedEffect` + per-pixel-lighting family alongside
`skinned_pixellighting_twobone_quad.scene` and `skinned_pixellighting_fourbone_quad.scene`,
confirming the per-pixel-lighting shader permutation is exercised across all 3
`WeightsPerVertex` values, not just the default per-vertex-lighting permutation.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Correctly combines two independent effect-property axes (lighting mode × bone count) rather than
only ever varying one at a time.

## Final Assessment
No findings.
