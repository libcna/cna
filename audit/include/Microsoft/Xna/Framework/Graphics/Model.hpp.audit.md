# Audit: include/Microsoft/Xna/Framework/Graphics/Model.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/Model.hpp`
- Audit status: AUDITED (full read, 130 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Model.cs`
- Main related tests: not independently located in this pass

## Purpose
A basic 3D model with per-mesh parent bones — the top-level container for `ModelBone`/`ModelMesh`
hierarchies loaded via the content pipeline.

## Executive Verdict
Correct. The 3-argument constructor matches FNA's real `internal Model(GraphicsDevice, List<ModelBone>,
List<ModelMesh>)` constructor's shape. The 4-argument overload (explicit per-mesh parent bones +
root bone index) is a well-documented `NOXNA` addition for hand-built models — FNA's own equivalent
construction always goes through `ModelReader` (the content pipeline), which has no public
equivalent entry point; this port's overload fills that gap for programmatically-constructed
models while preserving the 3-argument constructor's original leniency (an empty bones vector
always leaves `Root` null, regardless of `rootBoneIndex`).

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` tagging: correctly applied to both constructors and `setOwnedResources` (FNA's real
  `Model` constructor is `internal`, only ever called by `ModelReader`; this port exposes
  equivalent public construction as a documented, deliberate extension).
- `Bones`/`Meshes`/`Root`/`Tag` getters match FNA's real property shapes.

## Detailed Findings
None in this header (see the paired `.cpp` report for exception-type findings in the
implementation).

## Cross-File Observations
`getRootProperty()`'s FNA counterpart (`Root`) has an `internal set` FNA never uses from within
`Model.cs` itself (only `ModelReader` sets it) — this port's 4-argument constructor is the
equivalent public path.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The 4-argument constructor's doc comment (lines 38-56) explicitly documents its leniency contract
(empty `meshParentBones` vs. exactly-one-per-mesh) rather than leaving it to be discovered by
trial and error.

## Final Assessment
No findings.
