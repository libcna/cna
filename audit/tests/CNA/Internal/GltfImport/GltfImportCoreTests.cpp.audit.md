# Audit: tests/CNA/Internal/GltfImport/GltfImportCoreTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/GltfImport/GltfImportCoreTests.cpp` (612 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::GltfImport::GltfImportCore::ExtractMesh`/
  `ExtractPunctualLightsEXT`/`RemapOcclusionImageForDualTextureEXT`/`ComputeTangentsEXT` (CNA-internal
  glTF-to-CNJ import pipeline, no direct FNA equivalent — glTF import is a NOXNA extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Direct, in-process unit tests for glTF mesh extraction: PBR UV-set-mismatch detection, Draco mesh
decompression, angle-weighted tangent generation, glTF extension handling (`KHR_texture_transform`,
`KHR_materials_emissive_strength`, `KHR_lights_punctual`), and occlusion-image remapping.

## Executive Verdict
An exceptionally rigorous test file for numerically-sensitive import code. Every non-trivial
expected value is derived from an independent source — hand-derivation, an independent Python
re-implementation, or `scipy`'s own quaternion rotation — rather than captured-and-trusted from
whatever the implementation currently outputs. The `ComputeTangentsEXTAngleWeightsTriangleContributions`
test is a standout: its own comment states the fixture geometry was deliberately chosen so the
angle-weighted result (`0.91369578, 0.40639884, 0`) is *materially different* from what an
unweighted sum would produce (`0.72499943, 0.68874946, 0`), specifically to prove the weighting term
itself changed the output — not a rounding-level coincidence.

## Checklist Results
- `ExtractMeshDetectsMismatchedPbrMapUvSets`/`ExtractMeshDoesNotFlagMatchedPbrMapUvSets` correctly
  test both the positive and negative case for the same feature, with UV values deliberately chosen
  to differ between TEXCOORD_0/TEXCOORD_1 so the mismatch isn't accidentally masked.
  d
- `ExtractMeshDecodesDracoCompressedTriangle`'s Draco fixture is built via a real `draco::Encoder`/
  `draco::TriangleSoupMeshBuilder` (not hand-authored compressed bytes), with the comment noting
  attribute-ID assignment order was confirmed via "a standalone encode+decode round-trip during
  authoring" — a genuinely careful verification step rather than an assumption about encoder
  behavior.
- `ComputeTangentsEXTWorksOnADracoCompressedPbrPrimitiveWithNoTangentAccessor` is a real regression
  test for a specific bug found during this same test's own development (Draco-compressed
  primitives have no backing data for `prim.indices`, so a naive re-read was reading through
  invalid/backing-less memory) — the fix is verified via finiteness + unit-length checks rather than
  exact value matching, appropriately since "the exact tangent value isn't the point" per its own
  comment.
- `ExtractPunctualLightsEXTCapsAtThreeLights` correctly asserts the cap against `data->lights_count`
  (proving the *source* data really has 4 lights) before checking the *extracted* result has only 3
  — ruling out the alternative explanation that the fixture itself only had 3 lights.
- `#ifdef CNA_DRACO_AVAILABLE`-gated tests correctly mirror the production code's own conditional
  compilation, with an explicit comment explaining why (a Draco-less build reports no test to skip,
  rather than a misleading "SKIPPED" result) — a thoughtful CI-hygiene choice.

## Detailed Findings
None.

## Cross-File Observations
The file's own comment on `RemapOcclusionImageForDualTextureEXT`'s tests explains that pixel-value
verification (the remapped result actually decoding to halved RGB) is covered elsewhere
(`GltfToCnjToolTests.cpp`/`RuntimeGltfModelTests.cpp`, not in this shard's file list) since this file
has no `GraphicsDevice`/`Texture2D` infrastructure — a reasonable, disclosed test-responsibility
split across files rather than a gap.

## Missing or Weak Tests
None identified within this file's own disclosed scope.

## Positive Findings
The combination of a real Draco encoder (not hand-authored bytes) for compression tests, and
independently-derived expected values (Python re-implementation, scipy quaternion rotation) for
geometric-math tests, represents some of the most rigorous numerical test verification found in this
entire audit.

## Final Assessment
No findings.
