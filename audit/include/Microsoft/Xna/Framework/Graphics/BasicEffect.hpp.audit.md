# Audit: include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp`
- Audit status: AUDITED (full read, 377 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/BasicEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Built-in effect supporting optional texturing, vertex coloring, fog, and lighting — the most
commonly used stock XNA effect.

## Executive Verdict
Mostly correct — property shapes, default values, and `EnableDefaultLighting()`'s exact numeric
rig all verified to match FNA precisely (see the paired `.cpp` report). However, this header
reveals (by its absence) a real, confirmed HIGH-severity gap: unlike every sibling stock effect in
this batch (`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`,
`PbrEffect`, `SkinnedPbrEffect`), `BasicEffect` declares **no `EffectParameter*` member fields and
no `CacheEffectParameters()` method at all** — see Detailed Findings.

## Checklist Results
- Doxygen coverage: complete.
- Public field-based `World`/`View`/`Projection`/`VertexColorEnabled` (rather than get/set-only
  properties) matches FNA's own `BasicEffect` which also exposes these as ordinary public
  properties with simple backing fields — the direct-field shape here is a reasonable, low-risk
  simplification given no dirty-flag side effects are needed at the C++ level for these four.
- `SetOwnedTexture()` is correctly `NOXNA`-tagged with a clear rationale (a standalone
  content-loaded effect has no external owner keeping its texture alive, unlike a `Model`'s shared
  texture pool).

## Detailed Findings

### HIGH — `BasicEffect` never populates its own `Effect::Parameters` collection, unlike every
sibling stock effect
Confirmed by direct comparison across all 7 concrete stock effects in this batch:
`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, and
`SkinnedPbrEffect` all declare a private `void CacheEffectParameters()` method and a set of
`EffectParameter*` member pointers (`diffuseColorParam_`, `worldViewProjParam_`, `fogColorParam_`,
etc.), called from their constructor to `Add()` named parameters
(`"DiffuseColor"`, `"WorldViewProj"`, `"FogColor"`, ...) into `getParametersProperty()` — matching
FNA's own `BasicEffect.cs`/`SkinnedEffect.cs`/etc.'s `CacheEffectParameters()`, which populates
`Effect.Parameters["DiffuseColor"]` etc. from the compiled `.fx` bytecode's real parameter table.
**`BasicEffect` alone has neither** — no `EffectParameter*` members, no `CacheEffectParameters()`
declaration, and (confirmed in the paired `.cpp` report) no `AddParam()`/`getParametersProperty().Add(...)`
call anywhere in its constructor. `BasicEffect::getParametersProperty()` therefore returns a
permanently empty `EffectParameterCollection` for every `BasicEffect` instance.

**Failure scenario**: real XNA/FNA code frequently accesses stock-effect parameters generically via
`effect.Parameters["DiffuseColor"].GetValueVector3()` or similar — a real, documented, commonly-used
pattern for tooling, generic effect inspection, or content-pipeline-adjacent code that doesn't know
the concrete effect type ahead of time. Any such caller targeting a `BasicEffect` — the single most
commonly used stock effect in the entire XNA API surface — gets either an empty collection or a
"parameter not found" failure, while the identical call pattern against any of this file's 6 sibling
stock effects (`AlphaTestEffect` through `SkinnedPbrEffect`) succeeds correctly.

**Scope of impact**: confirmed NOT to affect `BasicEffect`'s own actual rendering — the real draw
path (`GraphicsDevice::DrawPrimitives()`, confirmed by direct reading) calls
`currentEffect_->FillGpuDrawParams(p)` directly, which `BasicEffect` overrides correctly and reads
its own private fields (not the `Parameters` collection) — so rendering itself is unaffected. The
gap is specifically in the secondary, XNA-documented `Effect.Parameters[name]` generic-access API
surface.

**Suggested fix** (report-only; no source changes made per this audit's scope): add a
`CacheEffectParameters()` method mirroring the sibling effects' pattern, adding at minimum
`"DiffuseColor"`, `"EmissiveColor"`, `"SpecularColor"`, `"SpecularPower"`, `"EyePosition"`,
`"FogColor"`, `"FogVector"`, `"World"`, `"WorldInverseTranspose"`, `"WorldViewProj"`,
`"ShaderIndex"`, `"Texture"` (matching FNA's real `BasicEffect.cs` parameter set exactly), and an
`OnApply()` implementation populating them (currently a no-op — see the paired `.cpp` report).

## Cross-File Observations
See the paired `.cpp` report for confirmation that `OnApply()` is a complete no-op, consistent with
this header's missing `EffectParameter*` infrastructure — the two findings are two views of the
same single gap.

## Missing or Weak Tests
A test asserting `basicEffect.getParametersProperty()["DiffuseColor"]` (or any named parameter)
is non-null/populated after construction would have caught this; not independently located in this
pass — likely does not exist, given the gap itself would have surfaced it.

## Positive Findings
Every other aspect of this header (property shapes, `IEffectMatrices`/`IEffectFog`/`IEffectLights`
implementation, `SetOwnedTexture()`'s ownership disclosure) is correct.

## Final Assessment
One HIGH finding: `BasicEffect`'s `Effect::Parameters` collection is permanently empty, unlike
every sibling stock effect audited in this batch, breaking the generic `effect.Parameters[name]`
access pattern specifically for the single most commonly used stock effect in the XNA API.
