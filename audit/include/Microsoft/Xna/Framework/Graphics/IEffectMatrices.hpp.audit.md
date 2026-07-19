# Audit: include/Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp`
- Audit status: AUDITED (full read, 61 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/IEffectMatrices.cs`
- Main related tests: not independently located in this pass

## Purpose
Interface for effects that support `World`/`View`/`Projection` matrix transforms.

## Executive Verdict
Correct. Member set matches FNA's `IEffectMatrices` exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Confirmed (via `GraphicsDevice.cpp`'s `ExtractMatrices()` helper, read to resolve a question raised
while auditing `BasicEffect`) that `World`/`View`/`Projection` on any `IEffectMatrices`-implementing
effect are extracted via `dynamic_cast<const IEffectMatrices*>` and passed as separate arguments
directly to the backend's draw call (`DrawPrimitivesEx(..., world, view, proj, ...)`) — a real,
correct, if architecturally different-from-FNA mechanism (FNA precomputes a single
`WorldViewProj` inside each effect's own `OnApply()`; CNA's real rendering pipeline instead
combines the separately-extracted matrices at the backend level, while each effect's own
`OnApply()`-computed `WorldViewProj`/`WorldInverseTranspose` parameters populate the generic
`Effect.Parameters` collection for API-surface parity/inspection, not for driving the actual
draw). Implemented by every stock effect in this batch (`BasicEffect`, `AlphaTestEffect`,
`DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, `SkinnedPbrEffect`,
`ShaderEffect`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Minimal, correct, exact FNA parity.

## Final Assessment
No findings.
