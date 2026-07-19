# Audit: tools/xna-oracle/scenes/alphatest_notequal_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/alphatest_notequal_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `AlphaTestEffect.AlphaFunction = CompareFunction.NotEqual` case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises the `NotEqual` compare function, one of the 8 `CompareFunction` values `README.md` claims
complete coverage for; targets the `PSAlphaTestEqNe` shader bucket (shared with
`alphatest_equal_quad.scene`, whose result it should logically invert for the same reference
alpha/texture data).

## Executive Verdict
Correct, well-targeted fixture, and a good cross-check pair with `alphatest_equal_quad.scene`.

## Checklist Results
- `alphafunction=NotEqual` present with the same `referencealpha`/texture alpha values as
  `alphatest_equal_quad.scene` (confirmed by comparing both files), giving a genuine logical-inverse
  discriminator between the two.
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Completes the 8-scene `AlphaFunction` family alongside `alphatest_equal_quad.scene` in the
`PSAlphaTestEqNe` bucket.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Deliberately mirrors `alphatest_equal_quad.scene`'s parameters to create a true logical-inverse
pair, rather than testing `NotEqual` in isolation with unrelated values.

## Final Assessment
No findings.
