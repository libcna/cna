# Audit: tests/Microsoft/Xna/Framework/Content/RuntimeGltfModelTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/RuntimeGltfModelTests.cpp` (764 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: NOXNA (glTF is not part of XNA 4.0; this is CNA's own content-pipeline
  extension). Tests runtime (non-CLI) in-process glTF loading through `ContentManager::Load<Model>()`
  via `ModelTypeReader::ReadGltfModel()` / `CNA::Internal::GltfImport::GltfImportCore`
  (plans/plan_cnj.md CNB-70/71, Phase 13D)
- Main related tests: N/A (this IS a test file); complements `GltfToCnjToolTests.cpp` (offline CLI
  tool, audited separately in this batch) and `GltfImportCoreTests.cpp` (not in this shard's batch)

## Purpose
10 tests proving `ContentManager::Load<Model>("name")` can resolve and parse a `name.gltf` file
directly at runtime — no `.cnj`/binary sidecar files at all — covering: basic textured geometry,
skinned+animated geometry with deliberately reversed `skin.joints` topological ordering, morph
targets (default weights + LINEAR and CUBICSPLINE weight animation), full PBR material maps
(`PbrEffect`), combined skinning+PBR (`SkinnedPbrEffect`), `DualTextureEffect` occlusion-brightness
remap (CNB-88), Draco mesh compression decoding (conditionally compiled under
`CNA_DRACO_AVAILABLE`), and `KHR_lights_punctual` extension wiring onto `BasicEffect`.

## Executive Verdict
Excellent. This file is essentially the in-process mirror of `GltfToCnjToolTests.cpp`'s CLI-tool
tests, and its own top comment (lines 1-19) is explicit that many fixtures are deliberately identical
to that file's (`kDualTextureGltf`, `kDracoTriangleGltf`, the reversed-`skin.joints` stress case) — a
sound design choice proving the same import logic (`GltfImportCore`, shared between the CLI tool and
the runtime path) behaves identically whether invoked via subprocess or in-process. Assertions are
precise throughout: exact topological-reorder bone-index checks, exact Hermite-tangent-driven
CUBICSPLINE values (0.15625, matching `MorphTargetEXTTests.cpp`'s own isolated derivation), and exact
occlusion-brightness-halving pixel checks (~127 from a raw 255 input).

## Checklist Results
- `LoadsUnskinnedTexturedModelDirectlyFromGltf`: correctly asserts `Tag` stays `nullptr` when there's
  no skeleton (SkinningData's documented default) — a meaningful negative assertion, not just "loads
  without crashing."
- `LoadsSkinnedAnimatedModelDirectlyFromGltfWithReversedJointOrder`: verifies both the topological
  bone reorder (`SkeletonHierarchy[0]==-1`, `[1]==0`) AND that the animation track's `BoneIndex`
  correctly points to the reordered new index (1), not the original glTF joints-array index — the
  same non-obvious correctness property `GltfToCnjToolTests.cpp`'s equivalent test checks.
- `LoadsCubicSplineMorphWeightAnimationFromGltf`: real Hermite-basis math check via a hand-derived
  expected value, cross-referenced explicitly against `MorphTargetEXTTests.cpp`'s isolated unit test
  for the same math — genuine, non-coincidental verification.
- `RemapsOcclusionTextureBrightnessForDualTextureEffectFromGltf` and
  `AppliesKhrLightsPunctualToBasicEffectFromGltf`: both assert precise numeric values (brightness
  halving with tolerance 2/255; light color 0.25/0.5/0.75 exactly) rather than loose "texture exists"
  or "light exists" checks.
- Draco test is correctly scoped by its own comment (lines 721-724) to only prove decode-without-crash
  plus correct vertex/index topology, explicitly deferring byte-level accuracy to
  `GltfImportCoreTests.cpp`'s more detailed assertions for the identical fixture — a reasonable,
  disclosed division of labor rather than a silently weak test.

## Detailed Findings
None.

## Cross-File Observations
- Shares near-identical fixtures with `GltfToCnjToolTests.cpp` (`kDualTextureGltf`,
  `kDracoTriangleGltf`, `kMorphedTriangleGltf`/`kCubicSplineMorphedTriangleGltf`,
  `kBasicTexturedWithLightGltf`) and with `GltfImportCoreTests.cpp` (Draco bitstream) — together these
  three files establish that the same import behavior holds across the CLI-tool path, the `.cnj`
  round-trip path, and the direct runtime path for every major feature (morph targets, PBR, skinning,
  Draco, lights).
- The reversed-`skin.joints` topological-reorder stress case appears in at least three places across
  this shard (`GltfToCnjToolTests.cpp`'s `kTinySkinnedGltf`, this file's `kSkinnedAnimatedGltf`, and
  presumably `GltfImportCoreTests.cpp`) — consistent, deliberate stress-testing of a genuinely tricky
  algorithm (topological sort under an adversarial input ordering), not accidental duplication.

## Missing or Weak Tests
None identified independently in this pass; the file's own scope-narrowing comments (e.g. for the
Draco test) already disclose where deeper coverage lives elsewhere rather than leaving a silent gap.

## Positive Findings
Genuinely careful, cross-referenced numerical assertions (Hermite-tangent CUBICSPLINE value,
occlusion-brightness halving) rather than approximate or existence-only checks. The explicit, honest
scope-narrowing comment on the Draco test (deferring byte-level checks to another file rather than
either duplicating them or silently omitting them) is a good testing-hygiene example.

## Final Assessment
No findings.
