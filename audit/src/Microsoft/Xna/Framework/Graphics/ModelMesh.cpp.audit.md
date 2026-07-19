# Audit: src/Microsoft/Xna/Framework/Graphics/ModelMesh.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/ModelMesh.cpp`
- Audit status: AUDITED (full read, 67 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/ModelMesh.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors, every getter/setter, and `Draw()`.

## Executive Verdict
Correct, and more defensive than FNA's real `Draw()` in a genuinely useful way. `Draw()`'s
per-part loop (lines 43-65) matches FNA's real per-part vertex/index-buffer binding, technique-pass
iteration, and `DrawIndexedPrimitives` argument order exactly (`PrimitiveType::TriangleList,
VertexOffset, 0, NumVertices, StartIndex, PrimitiveCount`), but adds a null/zero-count guard FNA's
real code doesn't have.

## Checklist Results
- `Draw()`'s guard `if (effect == nullptr || part->getPrimitiveCountProperty() <= 0) continue;`
  — confirmed FNA's real `ModelMesh.Draw()` has no null-check on `effect` at all
  (`effect.CurrentTechnique.Passes` would throw `NullReferenceException` for a null `Effect` with
  `PrimitiveCount > 0`); this port's guard is strictly safer.

## Detailed Findings
None.

## Cross-File Observations
Consumed by `Model::Draw()` (audited separately), which sets each effect's `World`/`View`/
`Projection` via `IEffectMatrices` before calling into this `Draw()`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `effect == nullptr` guard is a real, positive safety improvement over FNA's own reference
behavior — a `ModelMeshPart` with no effect set (a real, reachable state for a hand-built part)
renders as a silent no-op here instead of crashing with a null-reference dereference the way FNA's
real code would.

## Final Assessment
No findings.
