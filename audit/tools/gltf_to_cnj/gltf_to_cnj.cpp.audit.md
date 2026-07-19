# Audit: tools/gltf_to_cnj/gltf_to_cnj.cpp

## Metadata
- Source file: `tools/gltf_to_cnj/gltf_to_cnj.cpp` (650 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-gltf-to-cnj` shard
- File type: C++ CLI tool (offline asset converter, not part of the runtime engine)
- XNA/FNA relevance: none directly — targets CNA's general-purpose `Model`/`AnimationClip`
  content path (distinct from the Avatar-specific `SkinnedModelEXT`/`.skinnedmodel.json` system)
- Main related tests: N/A (offline developer tool)

## Purpose
Converts a glTF 2.0 file into CNA's `.cnj` Model/AnimationClip JSON format plus binary sidecars
(vertex/index/skeleton buffers, morph-target deltas, standalone animation-clip files) and any
referenced base-color/PBR textures, using the shared `CNA::Internal::GltfImport::GltfImportCore`
library for the actual glTF parsing/extraction.

## Executive Verdict
Correct, well-scoped, and honest about deliberate MVP cuts (only base-color/occlusion textures
extracted; vertex-color and `DualTextureEffect`'s `Texture2` are each mutually exclusive with the
other and with skinning) — all explicitly documented with references to the originating plan tasks
rather than silently narrowed.

## Checklist Results
- `Convert()`'s `DataGuard` (lines 545-555, RAII wrapping `cgltf_free`) correctly frees the parsed
  `cgltf_data*` on every exit path, including exceptions thrown from `ConvertGroup()` deeper in the
  call stack — no leak on the error path.
- `unitScale` argument validation (lines 621-636) correctly rejects both a non-numeric value
  (`std::stof` exception caught) and a non-positive value, with clear usage messages for both.
- `ConvertGroup()`'s DualTextureEffect fallback (lines 348-355): if occlusion-texture extraction
  fails after `useDualTexture` was already decided from material data, correctly falls back to
  `BasicEffect` rather than emitting a `DualTextureEffect` entry with an empty `Texture2` — avoiding
  a real "shader always samples both slots" rendering defect the comment explicitly explains.
  Correctly contrasted (in the same comment) against the PBR case, where falling through to
  `BasicEffect`/`DualTextureEffect`/`SkinnedEffect` would be wrong for a different reason (those
  effects don't understand the PBR vertex stride at all) — the two "must never fall through"
  reasons are kept distinct rather than conflated.
- Two independent texture-caching maps (`writtenTextures`, `remappedOcclusionTextures`) are kept
  deliberately separate rather than sharing one index sequence — the comment (lines 289-294)
  correctly explains why: the same `cgltf_image*` key can validly appear in both maps with
  *different* byte content (raw vs. brightness-remapped), so deriving a shared index from either
  map's `.size()` risks two unrelated entries colliding on the same output filename.

## Detailed Findings
None.

## Cross-File Observations
The `AppendMatrix()` byte-order comment (lines 76-78) explicitly cross-references
`BinReaderEXT::ReadMatrix()` in `ContentManager.cpp` to confirm the two sides of this binary
format agree — a good practice for a format with no schema/validation beyond "both sides agree by
convention."

## Missing or Weak Tests
No test was located exercising this tool directly (offline CLI tools of this kind are typically
verified via `tools/avatar_asset_pipeline/README.md`-style manual/documented runs rather than
automated GTest coverage) — not flagged as a gap given the project's own stated convention for this
class of tool.

## Positive Findings
The top-of-file comment's changelog-style history (CNB-50/51/52/70/82/83) accurately tracks how
this file's scope narrowed over time as shared logic moved into `GltfImportCore`, leaving only the
genuinely CLI-specific concerns (binary sidecar writing, JSON serialization, the entry point) — a
good example of a file's own comments staying accurate through refactoring rather than going stale.

## Final Assessment
No findings.
