# Audit: src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
- Audit status: AUDITED (full read, 212 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/BasicEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `BasicEffect`'s constructors, `Clone()`, `OnApply()`, `FillGpuDrawParams()`,
`EnableDefaultLighting()`, and every property getter/setter.

## Executive Verdict
`EnableDefaultLighting()` and `FillGpuDrawParams()` are both correct and carefully cross-checked
against FNA — see Checklist Results. However, `OnApply()` (line 46-49) is a complete no-op,
confirming the HIGH finding raised in the paired `.hpp` report: this class never populates its own
`Effect::Parameters` collection at all.

## Checklist Results
- **`EnableDefaultLighting()` (lines 186-205): verified numerically identical to FNA's
  `EffectHelpers.EnableDefaultLighting()`** — every light direction/diffuse/specular constant and
  the ambient color (`{0.05333332f, 0.09882354f, 0.1819608f}`) match FNA's real values exactly,
  digit-for-digit.
- **`FillGpuDrawParams()` (lines 51-141): the `SkinnedEffect`/`EnvironmentMapEffect`-style
  "ambient pre-folded into emissive, diffuse+emissive baked together when lighting is disabled"
  logic (lines 65-75) correctly mirrors FNA's real `EffectHelpers.SetMaterialColor()`** — verified
  line-by-line: when lighting is disabled, `forwardedDiffuse = diffuseColor_ + emissiveColor_`
  (matching FNA's `diffuse.X = (diffuseColor.X + emissiveColor.X) * alpha` disabled-lighting
  branch); when enabled, `emissiveColor_` is added separately on the lit path only (matching FNA's
  `emissive.X = (emissiveColor.X + ambientLightColor.X * diffuseColor.X) * alpha` — though note
  CNA's own comment explicitly discloses it "folds ambient into that multiply instead of FNA's
  pre-baked... uniform" at the shader level, a numerically-equivalent restructuring, not a
  behavior change).
- Directional-light gating (lines 85-107) correctly zeroes each light's GPU-facing diffuse/specular
  when disabled, with an inline comment correctly explaining this reproduces a side effect of FNA's
  real `DirectionalLight.Enabled` setter that this port's simpler `DirectionalLight` type doesn't
  have built in.
- `World`/`View`/`Projection` are NOT populated into `GpuDrawParams` here (only `World`, via
  `worldColMajor`) — confirmed, via reading `GraphicsDevice::DrawPrimitives()`, that `View`/
  `Projection` are correctly extracted separately (`ExtractMatrices()`, via `dynamic_cast<const
  IEffectMatrices*>`) and passed directly to the backend draw call — not a gap, a different but
  correct mechanism (see `IEffectMatrices.hpp`'s own audit report for the full explanation).

## Detailed Findings

### HIGH (see paired `.hpp` report for the full writeup) — `OnApply()` is a complete no-op
```cpp
void BasicEffect::OnApply()
{
    // SetCurrentEffect is now called by Effect::Apply() after OnApply() returns.
}
```
(lines 46-49). Every sibling stock effect's `OnApply()` in this batch (`AlphaTestEffect`,
`DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, `SkinnedPbrEffect`)
recomputes and uploads `WorldViewProj`/fog-vector/material-color into their own cached
`EffectParameter*` members here — `BasicEffect` has none to populate, since it never calls
`CacheEffectParameters()`/`AddParam()` in the first place (confirmed: no such method exists in this
file or its header). This is one gap, visible from both files: the header lacks the
infrastructure, and this file's `OnApply()` correspondingly has nothing to do.

## Cross-File Observations
See the paired `.hpp` report for the full HIGH finding writeup and suggested fix shape.

## Missing or Weak Tests
See the paired `.hpp` report.

## Positive Findings
`EnableDefaultLighting()`'s exact numeric fidelity to FNA and the correct, carefully-explained
material-color/lighting-gating logic in `FillGpuDrawParams()` demonstrate this file's rendering
path (as opposed to its `Effect.Parameters` API-surface path) is genuinely well-verified against
FNA.

## Final Assessment
One HIGH finding (shared with the paired `.hpp` report): `OnApply()`'s no-op body confirms
`BasicEffect` never populates its `Effect::Parameters` collection.
