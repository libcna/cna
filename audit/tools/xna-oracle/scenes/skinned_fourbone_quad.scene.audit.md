# Audit: tools/xna-oracle/scenes/skinned_fourbone_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/skinned_fourbone_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SkinnedEffect.WeightsPerVertex = 4` case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `SkinnedEffect`'s 4-bone weighted-blend vertex format with genuinely distinct, non-trivial
weights across all four `(boneindex, boneweight)` pairs, confirmed against FNA's real weighted-sum
`Skin()` formula via hand-derived math in the scene's own header comment.

## Executive Verdict
Correct, well-derived fixture with genuine discriminating power (all four weights non-zero and
distinct, unlike `skinned_quad.scene`'s single-bone case).

## Checklist Results
- `weightspervertex=4` present; all four bone matrices/weights distinct (verified by reading the
  actual key/value pairs) — a genuine test of the full weighted-sum formula, not a degenerate
  single-dominant-weight case.
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Completes the 3-scene `WeightsPerVertex` family (1/2/4) alongside `skinned_quad.scene` (1, now
correctly documented) and `skinned_twobone_quad.scene` (2).

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Genuinely non-degenerate weight distribution gives real discriminating power for the full
4-bone weighted-sum formula.

## Final Assessment
No findings.
