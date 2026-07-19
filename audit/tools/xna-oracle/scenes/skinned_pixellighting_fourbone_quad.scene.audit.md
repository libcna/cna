# Audit: tools/xna-oracle/scenes/skinned_pixellighting_fourbone_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/skinned_pixellighting_fourbone_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SkinnedEffect` + `PreferPerPixelLighting=true`, four-bone case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `SkinnedEffect`'s per-pixel-lighting shader permutation combined with full 4-bone
skinning, completing the lighting-mode × bone-count cross-product coverage.

## Executive Verdict
Correct, well-targeted fixture.

## Checklist Results
- `preferperpixellighting=true` present alongside `weightspervertex=4`, all four weights distinct
  and non-trivial (verified by reading the actual key/value pairs, consistent with
  `skinned_fourbone_quad.scene`'s weight distribution).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Completes the 3-scene `SkinnedEffect` + per-pixel-lighting family.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Completes full cross-product coverage of `SkinnedEffect`'s lighting mode × bone count.

## Final Assessment
No findings.
