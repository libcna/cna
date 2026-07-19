# Audit: tests/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffectTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffectTests.cpp` (398 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `EnvironmentMapEffect.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exhaustive default-value/setter/Clone coverage for `EnvironmentMapEffect`, including its
FNA-documented hard-locked `LightingEnabled=true` (throws if set `false`) and
`EnableDefaultLighting()`'s exact 3-light-rig constants.

## Executive Verdict
Correct and thorough; not directly relevant to any of the 10 assigned cross-check items.
`SetLightingEnabledFalseThrows` correctly tests a genuinely unusual real XNA quirk (this specific
stock effect's `IEffectLights.LightingEnabled` setter throws for `false`, unlike every other stock
effect where it's a plain settable bool) — a real, easy-to-miss divergence from the "typical" stock
effect shape that this file correctly captures.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Shares the `SetOwnedTexture`/`Clone`-shares-ownership pattern with sibling stock-effect test files,
extended to both `Texture` and `EnvironmentMap` (a `TextureCube*`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly captures the unusual "LightingEnabled=false throws" quirk specific to this one stock
effect, rather than assuming every stock effect shares an identical lighting-toggle contract.

## Final Assessment
No findings.
