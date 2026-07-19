# Audit: tests/Microsoft/Xna/Framework/Graphics/BasicEffectTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/BasicEffectTests.cpp` (373 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `BasicEffect.hpp`/`.cpp`, `DirectionalLight.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exhaustive default-value coverage for all 22 `BasicEffect` properties (cross-checked against real
FNA source per the file's own header comment), `EnableDefaultLighting()`'s exact 3-light-rig
constants, `SetOwnedTexture()`'s ownership semantics, and `Clone()`'s full-property-copy contract.

## Executive Verdict
Exceptionally thorough for what it tests — every default value is justified inline with a specific
FNA source citation, and `EnableDefaultLighting()`'s lighting-rig constants are checked to
`1e-6f` precision against literal FNA values. However, **this file has zero test coverage of
`BasicEffect::Parameters`** — the already-confirmed HIGH-severity finding that `BasicEffect` never
populates its own `Effect::Parameters` collection at all (unlike every sibling stock effect).

## Checklist Results
- **Item 5 cross-check (`BasicEffect.Parameters` never populated)**: confirmed via full read —
  no test in this file references `getParametersProperty()`/`Parameters[...]`-style generic
  access anywhere. Every property is tested through `BasicEffect`'s own dedicated typed
  getters/setters (`getWorldProperty()`, `getDiffuseColorProperty()`, etc.), never through the
  generic `EffectParameterCollection` surface real XNA code commonly uses
  (`effect.Parameters["World"].SetValue(...)`) for custom shader interop. **Verdict: MISSES** —
  no test would have caught this gap, since none of them exercise the code path it affects.
- `EnableDefaultLightingSetsAmbientLightColor`/`...KeyLightExactConstants`/
  `...FillLightExactConstants`/`...BackLightExactConstants` all use a tight `1e-6f` epsilon and
  cite the file's own comment explaining why (every value here is a hardcoded FNA literal, not a
  computed approximation) — appropriately strict for this kind of test.
- `CloneCopiesAllProperties` sets essentially every mutable property to a distinct, easily-confused-
  if-swapped value (e.g. `World`/`View`/`Projection` scaled by 2/3/4 respectively) before cloning,
  then verifies both the full copy and post-clone independence in both directions (clone mutation
  doesn't affect original, original mutation doesn't affect clone) — a genuinely rigorous
  isolation test, not just a shallow property-count check.
- `SetOwnedTextureKeepsTextureAliveAndVisibleThroughGetter`/`CloneSharesOwnedTextureOwnership`
  correctly test the `shared_ptr`-based ownership extension (Task XNB-32) this class adds beyond
  the plain non-owning `Texture2D*` setter, including the subtle detail that `Clone()` shares
  (not deep-copies) the owned texture.

## Detailed Findings
None beyond the assigned cross-check item.

## Cross-File Observations
This test suite's own thoroughness (every OTHER property exhaustively covered, cross-checked
against FNA source, to 1e-6 precision) makes the complete absence of any `Parameters`-collection
test more conspicuous — the test author clearly had FNA source in hand while writing this file, so
the gap is more likely an unexamined assumption ("BasicEffect doesn't need `Parameters` since it
has its own typed accessors") than an oversight, though the confirmed production behavior (`Parameters`
staying empty rather than mirroring the typed properties) is still a real divergence from every
sibling stock effect and from real XNA's documented generic-access contract.

## Missing or Weak Tests
A test asserting `fx.getParametersProperty()["DiffuseColor"]` (or equivalent) returns a valid,
non-null `EffectParameter*` reflecting the same value as `getDiffuseColorProperty()` would have
caught the confirmed gap immediately.

## Positive Findings
This is one of the most rigorously FNA-cross-checked test files encountered in this batch — every
default value and every lighting-rig constant is justified by a specific citation to FNA's real
`BasicEffect.cs`/`EffectHelpers.cs`/`DirectionalLight.cs` source, and two real historical bugs
(Task 361's `VertexColorEnabled`/`DirectionalLight0.Enabled` defaults) are explicitly locked in by
name.

## Final Assessment
One confirmed miss (Item 5): zero test coverage for `BasicEffect.Parameters`, the exact code path
the already-confirmed HIGH-severity production finding affects.
