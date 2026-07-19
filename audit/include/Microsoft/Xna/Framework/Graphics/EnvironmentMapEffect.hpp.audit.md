# Audit: include/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp`
- Audit status: AUDITED (full read, 401 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/EnvironmentMapEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Built-in effect applying cube-map environment mapping (`EnvironmentMap`, `EnvironmentMapAmount`,
`EnvironmentMapSpecular`, `FresnelFactor`) with optional fog and lighting.

## Executive Verdict
Correct property/method shape, matching FNA's `EnvironmentMapEffect` exactly. See the paired
`.cpp` report for a MEDIUM finding: `setLightingEnabledProperty(false)` throws a raw
`std::runtime_error` instead of this project's own `System::NotSupportedException`.

## Checklist Results
- Doxygen coverage: complete.
- Correctly and completely populates its own `Effect::Parameters` collection via
  `CacheEffectParameters()`.
- `SetOwnedTexture()`/`SetOwnedEnvironmentMap()` correctly mirror `BasicEffect`'s ownership pattern
  for both the diffuse texture and the cube map independently.

## Detailed Findings
None new in this header (see `.cpp` report).

## Cross-File Observations
Shares the identical `setLightingEnabledProperty(false)`-throws-the-wrong-exception-type pattern
with `SkinnedEffect`, `PbrEffect`, and `SkinnedPbrEffect` (all four `IEffectLights`-implementing
effects that disallow disabling lighting) — see each file's own `.cpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Full, correct `Effect::Parameters` population.

## Final Assessment
No new findings in this header; see the paired `.cpp` report for the MEDIUM exception-type
finding.
