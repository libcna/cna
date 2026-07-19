# Audit: include/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp`
- Audit status: AUDITED (full read, 287 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/AlphaTestEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Built-in effect supporting alpha testing against a reference value with a configurable
`CompareFunction`.

## Executive Verdict
Correct. Property/method shape matches FNA's `AlphaTestEffect` exactly (`AlphaFunction`,
`ReferenceAlpha`, `VertexColorEnabled`, `Texture`, plus `IEffectMatrices`/`IEffectFog`).

## Checklist Results
- Doxygen coverage: complete.
- Correctly and completely populates its own `Effect::Parameters` collection via
  `CacheEffectParameters()` (contrast with the confirmed HIGH gap in the sibling `BasicEffect` —
  see that file's own audit report).

## Detailed Findings
None.

## Cross-File Observations
Confirmed (via the paired `.cpp` report) that this file's `alphaFunction`-to-`alphaTest` vec4
translation matches FNA's real switch-case formula exactly, term-for-term, for every
`CompareFunction` value.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Full, correct `Effect::Parameters` population — the pattern `BasicEffect` alone is missing in this
batch.

## Final Assessment
No findings.
