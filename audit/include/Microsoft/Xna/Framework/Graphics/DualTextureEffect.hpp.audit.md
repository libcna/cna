# Audit: include/Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp`
- Audit status: AUDITED (full read, 277 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/DualTextureEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Built-in effect supporting two-layer multitexturing (`Texture`, `Texture2`) with optional fog and
vertex color.

## Executive Verdict
Correct. Property/method shape matches FNA's `DualTextureEffect` exactly.

## Checklist Results
- Doxygen coverage: complete.
- Correctly and completely populates its own `Effect::Parameters` collection via
  `CacheEffectParameters()`.
- `SetOwnedTexture()`/`SetOwnedTexture2()` correctly mirror `BasicEffect`'s ownership pattern for
  both texture slots independently.

## Detailed Findings
None.

## Cross-File Observations
Has no `IEffectLights` surface (matching real XNA's `DualTextureEffect`, which doesn't implement
it either) — no lighting-related exception-type call site to check, unlike `SkinnedEffect`/
`EnvironmentMapEffect`/`PbrEffect`/`SkinnedPbrEffect`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Full, correct `Effect::Parameters` population.

## Final Assessment
No findings.
