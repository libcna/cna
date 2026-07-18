# Audit: examples/easygl_model_json_reader_skeleton_test.cpp

## Metadata

- Source file: `examples/easygl_model_json_reader_skeleton_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `.model.json`/`.cnj` `ModelTypeReader` skeletal-
  animation-schema regression test (also compiled/run under `examples-tests-vulkan`,
  `cmake/Tests/VulkanTests.cmake` line 848)
- File type: C++ example/integration-test executable (`ModelJsonReaderSkeletonTest : Game`, `main()`); pure
  structural test, `Draw()` is an empty override
- Related production code: `ModelTypeReader::Read()`'s `"skeleton"`/`"animations"` handling (`ContentManager.cpp`
  lines 2156-2203), `ReadAnimationClipFileEXT`/`BinReaderEXT` (lines 1107-1259), `SkinningData`/`AnimationPlayer`
  (`AnimationPlayer.hpp`/`.cpp`)
- XNA/FNA relevance: `SkinningData`/`AnimationPlayer`/`AnimationClip`/`Keyframe` are all explicitly `NOXNA` — they
  mirror Microsoft's own (non-framework) XNA Skinned Model Sample convention, not the XNA 4.0 API surface itself;
  `Model.Tag`, `SkinnedEffect.Texture` are real XNA members. FNA parity applies to the underlying `Model`/
  `SkinnedEffect` object shapes only, not the `.skeleton.bin`/`.clip.bin` binary formats or `.model.json` schema
  (all CNA-internal).
- Main related tests: this file (Task 941); feeds directly into
  `easygl_model_skinned_animation_playback_test.cpp` (Task 942)'s end-to-end pixel-level pipeline test

## Purpose

Regression test for the Phase 77 `.model.json` schema extension: an optional `"skeleton"` field (naming a
`.skeleton.bin` file) and `"animations"` array (naming `.clip.bin` files), producing a `SkinningData` object
attached to the loaded `Model`'s `Tag` property, plus per-mesh `"effect": "SkinnedEffect"` + `"texture"` binding.
Hand-writes a 2-bone skeleton binary, a "Move" animation clip binary (bone 0 translating (0,0,0)→(2,0,0) over 1s),
a stride-52 GPU-skinned vertex fixture, and a solid-red QOI texture, then loads via `Content.Load<Model>()` and
checks: `Model.Tag` is a real `SkinningData`; `BoneCount`/`SkeletonHierarchy`/`BindPose`/`InverseBindPose` match the
fixture; `AnimationClips["Move"]` has the expected `Duration`/keyframe data; feeding the clip into a real
`AnimationPlayer` and driving it to `t=1s` reaches the expected world-space bone position; the mesh's `Effect` is a
real `SkinnedEffect` with a bound, non-null `Texture`. Correct placement per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — every binary format assumption in this file's hand-rolled fixture writers (`.skeleton.bin` layout,
`.clip.bin` layout, the QOI encoder) was independently checked byte-for-byte against the actual production readers
(`ContentManager.cpp`'s skeleton-parsing block, `ReadAnimationClipFileEXT`, and the vendored `qoi.h` decoder SDL3_image
links against) during this audit and matches exactly; this is a real end-to-end integration test of the parser →
data-model → `AnimationPlayer` pipeline, not a "loads without crashing" placeholder.

## Checklist Results

### API / XNA / FNA parity
`SkinningData : public System::Object` (`AnimationPlayer.hpp` line 48) correctly inherits `System::Object` so that
`dynamic_cast<SkinningData*>(model.getTagProperty())` (line 225) is well-formed (`Model::Tag`/`getTagProperty()`
returns `System::Object*`, and `dynamic_cast` requires a polymorphic base — satisfied since `System::Object`
presumably declares at least one virtual member, consistent with `SkinningData::GetTypeName() const override`
being declared). `AnimationClip`/`Keyframe` are `NOXNA using` aliases of `AnimationClipEXT`/`KeyframeEXT`
(`AnimationPlayer.hpp` lines 26, 33) — correctly reused rather than duplicated, per the header's own documented
rationale (shared with the separate Avatar-rendering `SkinnedModelEXT` path).

### Behavioral correctness
Verified the `.skeleton.bin` fixture (lines 142-153) byte-for-byte against the reader (`ContentManager.cpp` lines
2164-2191): `boneCount`(int32)=2, then 2×parent-index(int32) = `{-1, 0}`, then 2×bind-pose `Matrix` (16 floats
each, `AppendMatrix` writing `M11..M44` in row-major order matching `BinReaderEXT::ReadMatrix()`'s own `m[0..15]`→
`Matrix(m0,...,m15)` construction order), then 2×inverse-bind-pose `Matrix` — exact match to
`skinningData->SkeletonHierarchy`/`BindPose`/`InverseBindPose` resize-then-sequential-read loops. The test's own
assertions (`SkeletonHierarchy == {-1, 0}`, `BindPose[1].Translation.Y == 1.0`,
`InverseBindPose[1].Translation.Y == -1.0`, lines 229-239) match the fixture's own literal values exactly — not
independently guessed. The `.clip.bin` fixture (lines 156-174) was checked against `ReadAnimationClipFileEXT`
(`ContentManager.cpp` lines 1216-1259): `duration`(double)=1.0, `trackCount`(int32)=1, then per-track
`boneIndex`(int32)=0, `keyCount`(int32)=2, then per-key `time`(double) + 3×translation-float + 4×rotation-float
(quaternion x,y,z,w) + 3×scale-float — the fixture's byte order matches the reader's read order field-for-field,
including the quaternion's `(x,y,z,w)` component order and the reader's own documented note (line 1235-1240) about
avoiding undefined argument-evaluation order when reading these floats (a real prior bug the reader's own comment
describes, correctly not re-introduced by this test's straightforward sequential `AppendFloat` calls).

### Logic
The `AnimationPlayer` end-to-end check (lines 254-261) is the strongest part of this file: constructs a real
`AnimationPlayer(*skinningData)`, calls `StartClip(clip)` and `Update(TimeSpan::FromSeconds(1.0), false, false)`,
then asserts `GetWorldTransforms()[0].Translation.X == 2.0f`. Traced against `AnimationPlayer::RecomputeTransforms()`
(`AnimationPlayer.cpp` lines 105-162): bone 0 has no parent (`SkeletonHierarchy[0] == -1`), so
`worldTransforms_[0] = boneTransforms_[0]` directly (line 151-152), and `boneTransforms_[0]` is overwritten by
`SampleTrack(track, pos=1.0s)` (line 130-131) — since `pos >= keys.back().Time` (both equal 1.0s exactly, line 29
condition `pos >= keys.back().Time`), `SampleTrack` returns the *unclamped* keyframe-1 translation `(2,0,0)`
directly (lines 29-34, no interpolation needed at the exact endpoint) — giving `Translation.X == 2.0f` exactly,
matching the test's assertion with **zero floating-point-interpolation tolerance risk** (this is not an
interpolated intermediate value that could suffer rounding — it's the literal keyframe value returned verbatim).
This is a well-chosen assertion that avoids a flaky-test trap a lerp-midpoint check could have introduced.

### Memory/resource lifetime
Standard per-shard pattern (unique temp dir, no cleanup — pre-existing, shared convention not re-flagged here).
`AnimationPlayer player(*skinningData);` — `skinningData` here is `dynamic_cast<SkinningData*>(model.getTagProperty())`,
i.e. a pointer owned by the `Model`'s own `ownedResources_`/`ModelResources::skinningData` (kept alive via
`model`'s `shared_ptr`, itself a local in `Initialize()` living for the whole check block) — `player` (a local,
constructed and used entirely within the same scope as `model`) never outlives its `skinningData` reference, no
dangling risk.

### C++ correctness
`WriteSolidColorQoi` (lines 98-115) hand-encodes a minimal, always-valid QOI file (every pixel a literal
`QOI_OP_RGBA` chunk) — independently verified in this audit against the actual vendored `qoi.h` decoder (magic
`"qoif"`, big-endian width/height, `channels`/`colorspace` bytes, `QOI_OP_RGBA = 0xFF` tag, and the exact 8-byte
end padding `{0,0,0,0,0,0,0,1}`) that SDL3_image's `IMG_Load` (used by CNA's `ImageLoader::Load`) links against —
byte-for-byte correct, not a guessed format.

### Performance
N/A — tiny (2×2 pixel, 2-bone, 3-vertex) fixtures, test-only cost.

### Thread safety
N/A.

### Architecture
Correctly placed and scoped; pure structural/data test with an intentionally empty `Draw()`.

### Maintainability
304 lines — the longest file in this batch, but proportionate: it exercises 3 independent binary formats
(skeleton, clip, QOI texture) plus the full `Model`→`SkinningData`→`AnimationPlayer` chain in one coherent scenario,
and each fixture-writing helper is a small, single-purpose local lambda/function with its own doc comment.

### Portability
N/A.

### Robustness
Same shared shard-wide pattern noted in sibling reports (`Content.Load<Model>()` call at line 223 is not wrapped in
`try`/`catch`; `Game.cpp` has no top-level handler) — not re-scored as a new finding per file in this batch (see
`easygl_model_json_reader_32bit_indices_test.cpp`'s F1 for the fuller writeup of this shared characteristic).
`dynamic_cast`-based checks in this file (lines 225, 275) correctly use the null-returning form rather than a
throwing lookup, so — unlike the sibling bone-hierarchy test's `F1` — this file's own defensive-null-check style
*is* consistent with the actual API contracts it calls.

### Testing
This is itself a test file.

### Cross-file consistency
Shares the `.skeleton.bin`/`.clip.bin` binary format and `WriteSolidColorQoi` helper pattern with
`easygl_model_skinned_animation_playback_test.cpp` (audited separately in this batch) — consistent, non-duplicated
binary-format assumptions across both files (both independently verified against the same production reader in
this audit).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `SkinnedEffect.Texture` binding check doesn't verify the loaded texture's actual pixel content

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `check(skinnedFx->getTextureProperty() != nullptr, ...)` (line 279-280)
- Evidence: the check only confirms the texture pointer is non-null, not that it decoded the fixture's actual
  solid-red QOI content correctly (e.g. via `GetData()` and a pixel-value check) — a texture-loading regression
  that produced *a* texture object with wrong/garbage pixel data (as opposed to no texture at all) would pass this
  check.
- Why it matters: low impact — this file's own stated scope is the `.model.json`/`SkinningData` parsing path, not
  QOI pixel-decoding correctness (which is presumably covered by the codebase's own dedicated texture-loading
  tests elsewhere), so a non-null check is a reasonable, appropriately-scoped assertion for *this* file's purpose.
- Suggested future action: none required; flagged for completeness only.

## Cross-File Observations

- The `SampleTrack`/`RecomputeTransforms` logic this test exercises via `AnimationPlayer` is shared, unmodified
  production code with `easygl_model_skinned_animation_playback_test.cpp` — both files' fixtures were independently
  checked against the same implementation in this audit and are mutually consistent (same field-order assumptions).
- `AnimationPlayer`'s constructor (`AnimationPlayer.cpp` line 58-65) unconditionally calls `RecomputeTransforms()`
  before any `StartClip()`, which itself validates `SkinningData`'s internal size-consistency
  (`BoneCount`/`SkeletonHierarchy.size()`/etc. must all agree, `AnimationPlayer.cpp` lines 108-120, throwing
  `System::ArgumentException` otherwise) — this test's fixture is internally consistent (`BoneCount=2` matches all
  three vectors' sizes), so this validation path is exercised only on the success side here; worth noting for
  `AnimationPlayer`'s own subsystem audit that no test in this batch exercises the mismatched-size throw path.

## Missing or Weak Tests

- No test (in this file) of the interpolated **midpoint** of the "Move" clip (e.g. `t=0.5s`) — only the two
  keyframe endpoints (`t=0` implicitly via bind pose, `t=1s` explicitly) are exercised, so `SampleTrack`'s actual
  `Vector3::Lerp`/`Quaternion::Slerp` interpolation math is not verified against a known expected midpoint value by
  this file (though `easygl_model_skinned_animation_playback_test.cpp`'s sibling test also only checks endpoints).
- No negative-path test for `AnimationPlayer::RecomputeTransforms()`'s own internal consistency validation (see
  Cross-File Observations).

## Positive Findings

- Every hand-rolled binary fixture format in this file (skeleton, clip, QOI) was independently verified
  byte-for-byte against the real production reader/decoder in this audit and matches exactly — a genuinely
  rigorous, non-superficial integration test.
- The `AnimationPlayer` end-to-end check deliberately samples an *exact* keyframe endpoint rather than an
  interpolated value, avoiding floating-point-tolerance flakiness while still proving the full
  parse→data→playback pipeline is wired correctly.

## Final Assessment

A thorough, well-constructed structural/data integration test spanning three independent binary formats and the
full `Model`→`SkinningData`→`AnimationPlayer` pipeline. Every fixture assumption was independently traced against
the real production reader and matches exactly; the only gaps are narrow test-coverage completeness notes (texture
pixel-content verification, interpolated-midpoint sampling, malformed-`SkinningData` throw path), none of which
indicate an actual defect in this file or the code it exercises.
