# Audit: tools/xna-oracle/scenes/alphatest_never_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/alphatest_never_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `AlphaTestEffect.AlphaFunction = CompareFunction.Never` case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises the `Never` compare function (every pixel fails, quad entirely invisible against the
clear color), one of the 8 `CompareFunction` values `README.md` claims complete coverage for.

## Executive Verdict
Correct fixture, and a good discriminator: a hypothetical broken alpha-test implementation that
always passes would be trivially caught here (an unexpectedly-visible quad against a distinct clear
color).

## Checklist Results
- `alphafunction=Never` present and matches `README.md`'s documented `PSAlphaTestLtGt` mapping.
- Confirmed pixel-perfect (i.e. output is exactly the clear color, quad fully discarded) per
  `README.md`'s status log.

## Detailed Findings
None.

## Cross-File Observations
Part of the 8-scene `AlphaFunction` family; the natural complement to `alphatest_always_quad.scene`.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Good "opposite extreme" boundary case alongside `Always`.

## Final Assessment
No findings.
