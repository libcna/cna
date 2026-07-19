# Audit: tools/xna-oracle/scenes/skinned_twobone_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/skinned_twobone_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SkinnedEffect.WeightsPerVertex = 2` case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `SkinnedEffect`'s 2-bone weighted-blend vertex format, the middle case between
`skinned_quad.scene`'s single-bone case and `skinned_fourbone_quad.scene`'s full 4-bone case.

## Executive Verdict
Correct, well-derived fixture.

## Checklist Results
- `weightspervertex=2` present; both bone matrices/weights distinct and non-trivial (verified by
  reading the actual key/value pairs), giving genuine 2-bone-blend discriminating power.
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Part of the 3-scene `WeightsPerVertex` family (1/2/4).

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Fills the middle case of the 1/2/4 progression, not just the two extremes.

## Final Assessment
No findings.
