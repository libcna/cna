# Audit: src/Microsoft/Xna/Framework/Graphics/PbrEffect.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/PbrEffect.cpp`
- Audit status: AUDITED (full read, 369 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: NOXNA extension, no FNA equivalent — reviewed for internal consistency
- Main related tests: not independently located in this pass

## Purpose
Implements `PbrEffect`'s constructors, `Clone()`, `CacheEffectParameters()`, `OnApply()`, and
`FillGpuDrawParams()`.

## Executive Verdict
Correct and internally consistent with the other stock effects' established conventions
(`EnableDefaultLighting()` reuses the identical key/fill/back light rig; fog computation reuses
the identical FNA-derived formula). One MEDIUM exception-type finding (Detailed Findings). One
notable, correctly-documented deliberate divergence: base color (`DiffuseColor`) is NOT
premultiplied by alpha, unlike every other stock effect in this batch — see Positive Findings.

## Checklist Results
- **`EnableDefaultLighting()` (lines 147-161): reuses the identical key/fill/back light direction/
  color constants as `BasicEffect`/`SkinnedEffect`/`EnvironmentMapEffect`** — internally consistent
  across this entire family, even though there's no FNA source to independently verify it
  against for this NOXNA-only type.
- `OnApply()`'s fog-vector computation reuses the identical FNA-derived formula
  (`scale = 1/(fogStart-fogEnd)`, `worldView.M13/M23/M33/M43`-based vector) as every FNA-backed
  stock effect in this batch.
- `FillGpuDrawParams()` correctly populates all 5 PBR texture slots plus metallic/roughness
  factors and the `pbr` flag.

## Detailed Findings

### MEDIUM — `setLightingEnabledProperty(false)` throws `std::runtime_error` instead of
`System::NotSupportedException`
Line 139-140:
```cpp
void PbrEffect::setLightingEnabledProperty(bool value)
{
    if (!value)
        throw std::runtime_error("PbrEffect does not support setting LightingEnabled to false.");
}
```
Same pattern as `SkinnedEffect`/`EnvironmentMapEffect`/`SkinnedPbrEffect` (see each file's own
report). Even though `PbrEffect` itself is a NOXNA extension with no FNA precedent, this project's
own established sharp-runtime exception-type convention still applies for internal consistency —
a caller catching `System::NotSupportedException` elsewhere in this codebase would not catch this.

## Cross-File Observations
See `SkinnedEffect.cpp`'s own report for the cross-cutting note on this pattern recurring across
all 4 `IEffectLights`-implementing effects that disallow disabling lighting.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The un-premultiplied base-color-alpha handling (lines 309-316, explicitly commented) is a correct,
deliberate, well-explained divergence from this batch's other stock effects: the real glTF
metallic-roughness BRDF keeps albedo and alpha as independent quantities (alpha only affects
blending, never the lit RGB response), unlike XNA's own stock effects, which all premultiply
`DiffuseColor` by `Alpha`. This is the correct behavior for the model being implemented, not an
inconsistency to "fix" into matching the other effects.

## Final Assessment
One MEDIUM finding: `setLightingEnabledProperty(false)` should throw
`System::NotSupportedException`, not `std::runtime_error`.
