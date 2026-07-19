# Audit: include/Microsoft/Xna/Framework/Graphics/IEffectLights.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/IEffectLights.hpp`
- Audit status: AUDITED (full read, 74 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/IEffectLights.cs`
- Main related tests: not independently located in this pass

## Purpose
Interface for effects that support up to three directional lights plus ambient color
(`AmbientLightColor`, `DirectionalLight0/1/2`, `LightingEnabled`, `EnableDefaultLighting()`).

## Executive Verdict
Correct. Member set matches FNA's `IEffectLights` exactly.

## Checklist Results
No issues found. `getDirectionalLight0/1/2Property()` correctly return non-const references
(matching FNA's `DirectionalLight0` being a live, mutable object a caller can further configure
via `effect.DirectionalLight0.Direction = ...`, not a value snapshot).

## Detailed Findings
None.

## Cross-File Observations
Implemented by `BasicEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`,
`SkinnedPbrEffect` — all audited in this same batch. `SkinnedEffect`/`EnvironmentMapEffect`/
`PbrEffect`/`SkinnedPbrEffect` all correctly implement `setLightingEnabledProperty(false)` as a
throw (matching FNA's real explicit-interface-implementation constraint that these effects always
require lighting) — see each concrete class's own `.cpp` report for the exception-type used.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Minimal, correct, exact FNA parity.

## Final Assessment
No findings.
