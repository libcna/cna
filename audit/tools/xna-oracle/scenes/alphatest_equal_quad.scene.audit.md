# Audit: tools/xna-oracle/scenes/alphatest_equal_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/alphatest_equal_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `AlphaTestEffect.AlphaFunction = CompareFunction.Equal` case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises the `Equal` compare function, one of the 8 `CompareFunction` values `README.md` claims
complete coverage for; specifically targets the `PSAlphaTestEqNe` shader bucket (distinct from the
`LtGt` bucket the `Less`/`Greater`-family scenes exercise).

## Executive Verdict
Correct, well-targeted boundary-value fixture.

## Checklist Results
- `alphafunction=Equal` present, `referencealpha` chosen to exactly match the texture's alpha value
  (verified by the scene's own hand-derived header comment).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Part of the 8-scene `AlphaFunction` family; correctly documented as exercising the distinct
`PSAlphaTestEqNe` bucket alongside `alphatest_notequal_quad.scene`.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Reference alpha chosen precisely to make this a genuine boundary test, not a degenerate
always-true/always-false case.

## Final Assessment
No findings.
