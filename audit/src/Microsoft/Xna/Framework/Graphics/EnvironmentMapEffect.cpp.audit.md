# Audit: src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
- Audit status: AUDITED (full read, 474 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/StockEffects/EnvironmentMapEffect.cs`,
  `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `EnvironmentMapEffect`'s constructors, `Clone()`, `CacheEffectParameters()`,
`OnApply()`, `FillGpuDrawParams()`, and `EnableDefaultLighting()`.

## Executive Verdict
Rendering-path fidelity to FNA is correct and thorough (see Checklist Results), matching this
batch's other stock effects. One MEDIUM exception-type finding (Detailed Findings).

## Checklist Results
- **`EnableDefaultLighting()` (lines 180-199): numerically identical to FNA's
  `EffectHelpers.EnableDefaultLighting()`**, matching `BasicEffect`/`SkinnedEffect`'s already-verified
  values exactly.
- **`OnApply()`/`FillGpuDrawParams()` material-color computation (emissive pre-folded with
  ambient*diffuse, lines 367-371 and 423-426) matches FNA's `EffectHelpers.SetMaterialColor()`
  exactly**, and — like `SkinnedEffect` — never touches `GpuDrawParams::ambientColor`, consistent
  with the same confirmed "ambient pre-folded into emissive" convention.
- Task 890's own inline comment (lines 428-432) correctly documents summing all 3 directional
  lights (not just `DirectionalLight0`) — confirmed the loop actually does this for all three
  lights (lines 433-449).
- Shader-index formula (`!fogEnabled→+1, fresnelEnabled→+2, specularEnabled→+4, oneLight→+8`)
  matches FNA exactly.

## Detailed Findings

### MEDIUM — `setLightingEnabledProperty(false)` throws `std::runtime_error` instead of
`System::NotSupportedException`
Line 172-174:
```cpp
void EnvironmentMapEffect::setLightingEnabledProperty(bool value)
{
    if (!value)
        throw std::runtime_error("EnvironmentMapEffect does not support setting LightingEnabled to false.");
}
```
FNA's real `IEffectLights.LightingEnabled` explicit-interface setter throws
`NotSupportedException("EnvironmentMapEffect does not support setting LightingEnabled to
false.")` for this exact case — matching message text, wrong exception type. Should be
`System::NotSupportedException`. Same pattern as `SkinnedEffect`/`PbrEffect`/`SkinnedPbrEffect`
(see each file's own report) — 4 instances of the identical miss across this batch.

## Cross-File Observations
See `SkinnedEffect.cpp`'s own report for the cross-cutting note on this pattern recurring across
all 4 `IEffectLights`-implementing effects that disallow disabling lighting.

## Missing or Weak Tests
A test asserting `setLightingEnabledProperty(false)` throws specifically
`System::NotSupportedException` would have caught this; not independently located in this pass.

## Positive Findings
Task 890's directional-light-summing fix and the material-color computation are both verified
correct against FNA.

## Final Assessment
One MEDIUM finding: `setLightingEnabledProperty(false)` should throw
`System::NotSupportedException`, not `std::runtime_error`.
