# Audit: src/Microsoft/Xna/Framework/Graphics/Effect.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/Effect.cpp`
- Audit status: AUDITED (full read, 89 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/Effect.cs` (architecturally
  unrelated internals — see paired `.hpp` report)
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors, `Apply()`, `Dispose(bool)`, and all property accessors.

## Executive Verdict
Correct. `Apply()` correctly guards against use-after-dispose
(`if (isDisposed_) throw System::ObjectDisposedException(...)`) before calling `OnApply()`, then
notifies the device of the newly-current effect — a reasonable, safe sequencing.

## Checklist Results
- Uses `System::ObjectDisposedException` (not a raw `std::` exception) for the disposed-check in
  `Apply()` — correctly follows this project's established exception-type convention.
- The compiled-bytecode constructor correctly and exclusively throws
  `System::NotImplementedException` (not a raw `std::` exception either) — consistent, correct
  exception-type usage throughout this file.

## Detailed Findings
None.

## Cross-File Observations
Every concrete stock effect audited in this batch (`BasicEffect`, `AlphaTestEffect`,
`DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, `SkinnedPbrEffect`,
`SpriteEffect`, `ShaderEffect`) correctly overrides `GetTypeName()` with the exact
`"Microsoft.Xna.Framework.Graphics.<ClassName>"` pattern this base class establishes.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, consistent use of sharp-runtime exception types throughout — a good example for the
pattern this audit has flagged as inconsistently applied elsewhere in this same shard (see
`SkinnedEffect.cpp`, `EnvironmentMapEffect.cpp`, `PbrEffect.cpp`, `SkinnedPbrEffect.cpp`'s own
reports).

## Final Assessment
No findings.
