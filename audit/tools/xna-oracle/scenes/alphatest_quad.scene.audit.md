# Audit: tools/xna-oracle/scenes/alphatest_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/alphatest_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `AlphaTestEffect` baseline scene (default `AlphaFunction`)
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Establishes the baseline `AlphaTestEffect` case (default `CompareFunction` and `ReferenceAlpha`),
against which the 7 other `alphatest_*.scene` files each vary exactly one `AlphaFunction` value.

## Executive Verdict
Correct, minimal baseline case.

## Checklist Results
- Keys match `README.md`'s documented `AlphaTestEffect` table (`alphafunction`,
  `referencealpha`).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Anchors the 8-scene `AlphaFunction` family (`alphatest_always_quad.scene` through
`alphatest_notequal_quad.scene`); each sibling scene's own header comment documents which of the
real two shader buckets (`PSAlphaTestLtGt` vs. `PSAlphaTestEqNe`) it exercises, cross-checked
against `AlphaTestEffect.cs`'s real FNA source in this pass.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Clean baseline for the family's boundary-value coverage.

## Final Assessment
No findings.
