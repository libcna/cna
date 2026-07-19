# Audit: include/Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp`
- Audit status: AUDITED (full read, 133 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA extension, no FNA equivalent (glTF morph-target/blend-shape support;
  real XNA 4.0 has no morph-target concept at all)
- Main related tests: not independently located in this pass

## Purpose
Per-vertex position/normal deltas for every morph target on a `ModelMeshPart`, the current blend
weights, and an optional time-varying weight-animation track (glTF's "weights" animation channel);
plus the pure blend computation and re-upload helper functions.

## Executive Verdict
Correct, well-documented NOXNA extension. `MorphWeightTrackEXT`'s doc comment explicitly and
correctly justifies why this is a separate, independent timeline from `AnimationPlayer`/
`SkinningData`'s bone-track timeline: glTF's "weights" animation channel targets a mesh-instance
node directly, not a skeleton joint, so it has no bone index to key against.

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` tagging: correctly applied throughout (entire file has no real XNA API surface).
- `MorphTargetDataEXT` correctly overrides `GetTypeName()` (verified in the `.cpp`).

## Detailed Findings
None.

## Cross-File Observations
`MorphTargetDataEXT`'s doc comment explicitly mirrors `SkinningData`'s own established precedent
(attached via a real XNA `Tag` property rather than a dedicated new property) — consistent, reused
design convention across both NOXNA animation-data extensions in this shard.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The design-tradeoff note (CPU-side re-blend + full vertex-buffer re-upload rather than a GPU
vertex-shader morph technique) is explicitly justified as "works unchanged with every existing
effect/shader on every graphics backend" — a deliberate, disclosed simplicity-over-throughput
choice, not an unexamined default.

## Final Assessment
No findings.
