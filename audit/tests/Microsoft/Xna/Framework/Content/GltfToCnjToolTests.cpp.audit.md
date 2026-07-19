# Audit: tests/Microsoft/Xna/Framework/Content/GltfToCnjToolTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/GltfToCnjToolTests.cpp` (1642 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: NOXNA (glTF import is a CNA content-pipeline extension, not part of XNA 4.0).
  Tests the offline CLI tool `tools/gltf_to_cnj/gltf_to_cnj.cpp` (`cna_tool_gltf_to_cnj`), spawned as
  a real subprocess via `posix_spawn`, covering the full glTF -> `.cnj`/binary-sidecar pipeline and
  round-trip loading back through `ContentManager`
- Main related tests: N/A (this IS a test file); complements `RuntimeGltfModelTests.cpp` (audited in
  this same batch, the in-process mirror of this file) and `GltfImportCoreTests.cpp` (not in this
  shard's batch, the shared underlying library both this file and the runtime path call into)

## Purpose
20 `TEST(GltfToCnjToolTest, ...)` cases exercising the real offline CLI tool as a subprocess (via
`posix_spawn`/`waitpid`, not a direct function call), covering: topological bone reordering with a
deliberately reversed `skin.joints` array, clean failure on a missing input file, sparse-accessor
resolution, embedded base-color texture extraction, multi-skin files producing separate `Model`
outputs, STEP-interpolation foreign-resample-time correctness, sparse-accessor unpacking, all-scene-
node-reachability scoping, CUBICSPLINE Hermite-basis evaluation, vertex color on both
`BasicEffect`/`SkinnedEffect`, unit-scale application to positions and bone translations,
`DualTextureEffect` occlusion-brightness remap, full PBR material serialization (`PbrEffect` and
`SkinnedPbrEffect`), morph target CLI/`.cnj` serialization (including CUBICSPLINE morph-weight
tangents), Draco mesh compression (both an unconditional-rejection case and a
`CNA_DRACO_AVAILABLE`-gated success case), and `KHR_lights_punctual` round-trip through the `.cnj`
JSON format.

## Executive Verdict
Overall excellent — this is the most thorough single test file encountered in this shard, exercising
a real subprocess end-to-end for every major glTF import feature with precise, often hand-derived
numeric assertions (e.g. the CUBICSPLINE Hermite basis giving exactly X=10.0 at a foreign resample
time, distinguishing a correct implementation from both a value-only fallback (0.0) and a naive
linear fallback (5.0) — see lines 1200-1204). However, one genuine internal inconsistency was found:
a stale comment on an unrelated test misstates the current behavior of morph-target CLI serialization
(see Detailed Findings).

## Checklist Results
- `RunGltfToCnjTool()` (lines 870-893) correctly spawns the real tool binary via `posix_spawn` and
  reports a distinct `ADD_FAILURE()` if the spawn itself fails, separately from a nonzero tool exit
  code — a meaningful distinction between "test infra broke" and "tool under test rejected the input."
- `ConvertsIndexlessDualBoneSkinnedFixtureAndLoadsBackThroughContentManager`: verifies topological
  bone reorder, real bind-pose translation extraction (not just parent-index remap), animation-track
  bone-index remapping to the new post-reorder index, translation-holds-bind-pose-value when only
  rotation is animated, and a genuine end-to-end `AnimationPlayer` playback check that the mid-
  animation skin transform differs from identity — a thorough, layered single test.
- `ResolvesSparseAccessorOverride`'s own comment (lines 979-981) correctly documents *why* the test
  exists: proving `cgltf_accessor_unpack_floats` (sparse-safe) is used rather than
  `cgltf_accessor_read_float` (which would reject sparse accessors outright) — ties the assertion
  back to a specific implementation-choice risk, not just "sparse works."
- `StepInterpolatedChannelHoldsValueAcrossAForeignResampleTime` and
  `EvaluatesCubicSplineWithRealHermiteBasis`: both correctly test union-time resampling behavior at a
  time value foreign to the channel under test, with precise expected values that rule out plausible
  wrong implementations (STEP-holds-old-value vs. accidental linear interpolation; real Hermite vs.
  value-only or linear fallback).
- `WiresBaseColorAndOcclusionTexturesThroughDualTextureEffect`'s own comment (lines 1301-1304) and
  `SerializesAndReloadsPbrMaterialThroughTheOfflineCnjPath`/`...SkinnedPbrMaterial...` correctly
  assert exact vertex strides (20/48/68 bytes) per effect type, a real discriminator that a wrong
  effect-selection branch would fail immediately.
- `RejectsDracoCompressedPrimitive` (unconditional) and the `CNA_DRACO_AVAILABLE`-gated
  `ConvertsDracoCompressedTriangleAndLoadsBackThroughContentManager` use two distinct fixture
  constants (`kDracoGltf` vs. `kDracoTriangleGltf` respectively) — the file correctly covers both "the
  tool must reject a Draco-compressed primitive it cannot handle" and, when the feature is actually
  compiled in, "the tool correctly decodes and round-trips a supported Draco primitive." (The exact
  distinguishing content of `kDracoGltf` vs. `kDracoTriangleGltf` was not independently re-diffed
  byte-for-byte in this pass; the dual-test structure itself is sound.)

## Detailed Findings

### 1. Stale/contradictory comment about morph-target CLI serialization (MEDIUM — documentation defect)
- **Location**: Lines 1378-1380, the doc comment on
  `TEST(GltfToCnjToolTest, SerializesAndReloadsPbrMaterialThroughTheOfflineCnjPath)`:
  > "// CNB-56/59: the offline CLI tool must serialize PbrEffect's 4 maps + factor values to real
  > // .cnj/binary-sidecar files (stride 48), and ModelTypeReader's own .cnj JSON path must read them
  > // back correctly -- unlike morph targets, which the CLI tool deliberately does not serialize."
- **Contradiction**: Roughly 100 lines later in the very same file, lines 1475-1478 (the doc comment
  on `TEST(GltfToCnjToolTest, SerializesAndReloadsMorphTargetsThroughTheOfflineCnjPath)`) state the
  opposite:
  > "// Morph target CLI/.cnj serialization: the offline CLI tool must write a binary morph sidecar +
  > // "morphTargets"/"morphWeights"/"morphWeightTrack" JSON fields, and ModelTypeReader's own .cnj
  > // JSON path must reconstruct the same MorphTargetDataEXT the runtime glTF path already builds
  > // directly (formerly a documented scope cut -- CNB-64/Phase 13B -- that only emitted a warning)."
  and the test body itself (lines 1479-1523) asserts real morph-target data (`PositionDeltas`,
  `Weights`, `WeightTrack` with LINEAR interpolation) was correctly serialized by the CLI tool and
  reconstructed via `ContentManager::Load<Model>()` — including a second test,
  `SerializesAndReloadsCubicSplineMorphWeightsThroughTheOfflineCnjPath` (lines 1529-1560), proving
  CUBICSPLINE morph-weight tangents round-trip too.
- **Explanation**: The PBR test's comment was almost certainly written and never updated across a
  later change that implemented CNB-64/Phase 13B's morph-target CLI serialization (the newer comment
  explicitly calls the no-serialization behavior "formerly a documented scope cut ... that only
  emitted a warning" — past tense, i.e. the gap existed once but was closed). The PBR comment is a
  leftover reference to a scope cut that no longer holds.
- **Impact**: Low functional risk (this is a comment, not code, and does not affect test correctness
  or behavior) but real risk of misleading a future maintainer reading only the PBR test's comment
  into believing morph targets still aren't CLI-serializable, when the same file's own later tests
  prove otherwise.
- **Suggested fix** (not applied — audit-only): remove or update the "unlike morph targets, which the
  CLI tool deliberately does not serialize" clause in the comment at line 1380.

## Cross-File Observations
- Fixture-sharing with `RuntimeGltfModelTests.cpp` is extensive and deliberate (both files' own
  comments cross-reference each other, e.g. line 1507's "exactly like RuntimeGltfModelTest's own
  identical assertion for the runtime path" and line 1528's "same fixture and hand-derived expected
  value as RuntimeGltfModelTest.LoadsCubicSplineMorphWeightAnimationFromGltf") — this is a sound
  design proving the CLI-tool path and the runtime path share behavior via the common
  `GltfImportCore` library, not just superficially similar test names.
- The CNB-88 occlusion-brightness-remap fix and the CNB-97 `KHR_lights_punctual` extension are each
  tested identically in both this file and `RuntimeGltfModelTests.cpp`, confirming the fix/feature
  applies uniformly regardless of which import entry point is used.

## Missing or Weak Tests
No test in this file exercises a resolvable-but-genuinely-invalid glTF JSON body (e.g. malformed JSON
syntax, as opposed to a missing input *file*, which `MissingInputFileFailsCleanly` already covers) —
whether the tool fails cleanly on structurally broken (not just absent) glTF input is not verified
here. This may be covered by `GltfImportCoreTests.cpp` (not in this shard's batch); not independently
confirmed either way in this pass.

## Positive Findings
- The hand-derived Hermite-basis and STEP-interpolation expected values, each with an explicit
  comment distinguishing the correct answer from specific plausible-wrong-implementation answers
  (lines 1200-1204, 1103-1104), are a genuinely strong test-design pattern that goes well beyond
  "some non-default value came out."
- The vertex-color, PBR, and skinned-PBR tests' exact-stride assertions are an effective, cheap
  discriminator for effect-selection bugs that would otherwise only surface as a subtler rendering
  defect.
- Deliberate, cross-referenced fixture-sharing with `RuntimeGltfModelTests.cpp` demonstrates real
  care in proving shared-library behavior consistently across both call sites.

## Final Assessment
MEDIUM: stale/contradictory comment (lines 1378-1380) incorrectly states morph targets are "not
serialized" by the CLI tool, directly contradicted by this same file's own
`SerializesAndReloadsMorphTargetsThroughTheOfflineCnjPath` and
`SerializesAndReloadsCubicSplineMorphWeightsThroughTheOfflineCnjPath` tests roughly 100 lines later.
Documentation-only; no test-correctness or production-behavior defect.
