# Audit: include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp`
- Audit status: AUDITED (full read, 194 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA extension, no FNA equivalent (explicitly disclosed: "not part of the
  XNA 4.0 API. CNA extension.")
- Main related tests: not independently located in this pass

## Purpose
GLSL-source-based custom effect loaded from vertex/fragment shader strings, for game code that
needs a hand-authored shader beyond the stock effect set.

## Executive Verdict
Correct, and a well-designed NOXNA extension with real, load-bearing design rationale disclosed
throughout its own doc comments (Task 1079/1081, `plans/plan_graphics.md` Task 863) rather than
undocumented additions.

## Checklist Results
- Doxygen coverage: complete, entire class correctly `NOXNA`-tagged (both at the class level and
  on every member).
- `Clone()`'s doc comment (lines 148-160) explicitly and correctly explains a deliberate deviation
  from every other concrete `Effect` subclass in this shard: it recompiles a fresh backend program
  from the same GLSL source rather than sharing the original's compiled program object, since
  `ShaderEffect` uniquely owns a per-instance `std::unique_ptr<IEffectBackend>` (unlike stock
  effects, whose GPU pipelines are cached globally by state) — reference-counted sharing is
  explicitly flagged as out of scope rather than silently doing something different from what a
  reader might expect.
- Implements `IEffectMatrices` (Task 1079's own doc comment correctly explains this lets a
  `ShaderEffect` drive a real 3D draw call the same way stock effects do, with `World`/`View`/
  `Projection` extracted via `GraphicsDevice::ExtractMatrices()` — confirmed consistent with this
  session's own reading of that mechanism while auditing `IEffectMatrices.hpp`).
- `SetTexture()` overloads for `Texture2D`/`TextureCube`/`Texture3D` are each separately dated to a
  specific task (1081, plans/plan_graphics.md 863) — a genuinely incremental, well-tracked feature
  history rather than everything landing at once undocumented.

## Detailed Findings
None.

## Cross-File Observations
`GetTypeName()` (confirmed in the paired `.cpp` report) returns `"CNA.ShaderEffect"` rather than a
`Microsoft.Xna.Framework.Graphics.*`-namespaced name — correct and appropriate given this type has
no real .NET namespace to report (it's a CNA-only extension, not a port of a real XNA type).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exemplary NOXNA-extension documentation: every deviation from the stock-effect pattern is
individually explained with its own rationale, not just marked `NOXNA` and left unexplained.

## Final Assessment
No findings.
