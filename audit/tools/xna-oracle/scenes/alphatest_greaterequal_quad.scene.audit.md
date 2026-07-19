# Audit: tools/xna-oracle/scenes/alphatest_greaterequal_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/alphatest_greaterequal_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `AlphaTestEffect.AlphaFunction = CompareFunction.GreaterEqual` case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises the `GreaterEqual` compare function, one of the 8 `CompareFunction` values `README.md`
claims complete coverage for; targets the `PSAlphaTestLtGt` shader bucket.

## Executive Verdict
Correct, well-targeted boundary-value fixture.

## Checklist Results
- `alphafunction=GreaterEqual` present with a `referencealpha` chosen at the exact texture alpha
  boundary (verified in the scene's own header comment), making this a genuine `>=` vs `>` boundary
  discriminator against `alphatest_greater`-style behavior.
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Part of the 8-scene `AlphaFunction` family.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Boundary value chosen deliberately at the equality point, not comfortably inside the pass/fail
region — genuine discriminating power.

## Final Assessment
No findings.
