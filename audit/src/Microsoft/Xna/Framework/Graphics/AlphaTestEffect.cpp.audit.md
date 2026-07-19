# Audit: src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
- Audit status: AUDITED (full read, 387 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/AlphaTestEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `AlphaTestEffect`'s constructors, `Clone()`, `CacheEffectParameters()`, `OnApply()`, and
`FillGpuDrawParams()`.

## Executive Verdict
Correct, and a strong example of exact FNA-formula fidelity duplicated correctly across two
independent computation sites (`OnApply()`'s `EffectParameter`-based path and
`FillGpuDrawParams()`'s `GpuDrawParams`-based path).

## Checklist Results
- **The `alphaTest` vec4 computation (`OnApply()` lines 243-298 and `FillGpuDrawParams()` lines
  339-379) is verified identical to FNA's real switch-case formula**, for every one of the 8
  `CompareFunction` values (`Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Equal`/`NotEqual`/
  `Never`/`Always`) — reference/threshold values, `Z`/`W` pass/fail weights, and the `Equal`/
  `NotEqual` `eqNe` flag (driving the `isEqNe_`-gated shader-index bit) all match FNA's real
  `AlphaTestEffect.OnApply()` term-for-term. Both computation sites (the `EffectParameter` path and
  the `GpuDrawParams` path) independently reproduce the identical formula — confirmed consistent
  with each other, not just with FNA.
- Shader-index formula (`!fogEnabled→+1, vertexColorEnabled→+2, isEqNe→+4`) matches FNA exactly.
- `CacheEffectParameters()` correctly populates `Effect::Parameters` (`DiffuseColor`, `AlphaTest`,
  `FogColor`, `FogVector`, `WorldViewProj`, `ShaderIndex`) — matching FNA's real parameter set,
  unlike the sibling `BasicEffect`'s confirmed gap.

## Detailed Findings
None.

## Cross-File Observations
This file has no `IEffectLights` surface (real XNA's `AlphaTestEffect` doesn't implement it either)
and so has no `setLightingEnabledProperty` exception-type call site to check — contrast with
`SkinnedEffect`/`EnvironmentMapEffect`/`PbrEffect`/`SkinnedPbrEffect`, all of which have the
confirmed `std::runtime_error`-instead-of-`NotSupportedException` pattern.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact, double-verified (two independent code paths) FNA fidelity for the entire alpha-test
formula — a genuinely well-ported file.

## Final Assessment
No findings.
