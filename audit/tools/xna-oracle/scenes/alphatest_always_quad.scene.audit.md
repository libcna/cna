# Audit: tools/xna-oracle/scenes/alphatest_always_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/alphatest_always_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `AlphaTestEffect.AlphaFunction = CompareFunction.Always` case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises the `Always` compare function (every pixel passes regardless of alpha), one of the 8
`CompareFunction` values `README.md` claims complete coverage for.

## Executive Verdict
Correct, well-targeted boundary-value fixture.

## Checklist Results
- `alphafunction=Always` present and matches `README.md`'s documented mapping to the
  `PSAlphaTestLtGt` shader bucket (per the scene's own header comment, cross-checked against
  `AlphaTestEffect.cs`).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Part of the 8-scene `AlphaFunction` family anchored by `alphatest_quad.scene`.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Correctly documents which real shader bucket it exercises.

## Final Assessment
No findings.
