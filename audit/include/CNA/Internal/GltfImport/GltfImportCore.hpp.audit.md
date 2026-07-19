# Audit: include/CNA/Internal/GltfImport/GltfImportCore.hpp

## Metadata
- Source file: `include/CNA/Internal/GltfImport/GltfImportCore.hpp`
- Audit status: AUDITED (scoped-depth review — 387-line header inventoried in full; paired with a
  scoped-depth review of the 1409-line `.cpp`, see that report)
- Subsystem: `cna-internal-core` shard
- File type: C++ header
- XNA/FNA relevance: backs the glTF model-import content pipeline (feeds `ModelContentTypeReaders.hpp`/
  `Microsoft::Xna::Framework::Graphics::Model` construction) — a CNA-original content-authoring path, not a
  direct XNA API itself
- Main related tests: not independently located in this pass

## Purpose
Declares the glTF-to-CNA-model intermediate representation (`BoneOut`/`SkeletonResult`, `KeyframeOut`/
`TrackOut`/`ClipOut` for animation, `MeshOut`/`MeshGroup`, `LightOut`, morph-weight tracks) and the extraction
functions (`BuildSkeleton`, `ExtractClips`, `ExtractImage`, `ExtractMesh`, `CollectMeshGroups`,
`ExtractPunctualLightsEXT`, `ExtractMorphWeightTrack`) built on the `cgltf` parsing library.

## Executive Verdict
Healthy in the areas read (scoped-depth review); no defects found.

## Checklist Results
### API design
Clean, well-organized intermediate-representation structs; function signatures and their doc comments
correctly describe unit-scale handling, image extraction fallbacks, and the Draco-mesh-compression path
(`DecodeDracoPrimitiveEXT`) as an optional, gated feature (matching `CLAUDE.md`'s own documented Draco
detection policy — absent at build time, a Draco-compressed primitive throws a clear error at import time).

## Detailed Findings
None in the areas read.

## Cross-File Observations
See `.cpp` report for the concrete correctness verification of `BuildSkeleton`/`UnpackAccessor`.

## Missing or Weak Tests
Not independently verified in this pass.

## Positive Findings
Clear separation of concerns (per-feature extraction functions each independently callable/testable).

## Final Assessment
No defects found in the areas read (scoped-depth review).
