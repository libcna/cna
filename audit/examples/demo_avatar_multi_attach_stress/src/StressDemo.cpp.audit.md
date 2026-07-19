# Audit: examples/demo_avatar_multi_attach_stress/src/StressDemo.cpp

## Metadata
- Source file: `examples/demo_avatar_multi_attach_stress/src/StressDemo.cpp` (283 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar_multi_attach_stress` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises `SkinnedModelEXT::AttachPartEXT`/`AddPartEXT`, `VertexBuffer`,
  `IndexBuffer`, `ModelMeshPart`, `VertexPositionNormalTextureSkinned`
- Related production code: `AvatarRenderer::PartTintEXT` (this file's own comment explicitly cites
  reading that method's source before relying on its fallback behavior)

## Purpose
Implements `AttachNext()` (attaches `hair_Cap`, then `hair_Ponytail`, then an unbounded series of
synthetic single-bone-weighted quad accessories via `AttachSyntheticAccessory()`), with an
on-screen `Parts.size()` counter.

## Executive Verdict
Correct, no findings, and directly answers this audit directive's specific question: this file does
**not** re-implement or duplicate bone-weight-blending logic. `AttachSyntheticAccessory()`'s
synthetic quad vertices (lines 131-138) are built with `Vector4(1, 0, 0, 0)` weights and `{0, 0, 0,
0}` bone indices for every vertex — i.e. rigidly, 100%-weighted to bone 0 with zero blending
whatsoever, and the comment (lines 105-110) explicitly confirms this is deliberate ("rigidly skinned
entirely to bone 0 (weight 1.0, no blending)"). This is categorically different from the
already-fixed `generate_body.py::fix_automatic_weights()` bone-weight-*blending* logic (which
computes smooth multi-bone influence based on perpendicular distance to a bone axis) — there is no
overlapping risk surface here at all.

## Checklist Results
- `AttachSyntheticAccessory()` correctly copies `BoneCount`/`ParentBoneIndices`/`BindPoseLocal`/
  `InverseBindPoseGlobal` from the already-loaded host `model_` (lines 111-115), matching
  `AttachPartEXT`'s documented requirement that an attached model "share this model's exact bone
  count and index order" — comment explicitly notes the accessory's own `Clips` are irrelevant
  post-attach since `DrawRealEXT` always uses the host model's clip.
- The palette-cycling fallback comment (lines 117-120) explicitly documents having read
  `AvatarRenderer::PartTintEXT`'s source before relying on its "shared skin tint for any
  non-matching part name" fallback behavior — confirmed as "not a bug, an intentional existing
  fallback," not an assumption.
- `AttachNext()`'s `attachCount_` branching (`==1` hair_Cap, `==2` hair_Ponytail, else synthetic) is
  simple, correctly ordered, and matches the header's own documented design ("hair variants first,
  then a growing series").
- No `NetworkSession`/`GamerServices`-session dependency.

## Detailed Findings
None.

## Cross-File Observations
This file provides the clearest, most direct confirmation among the 8 shards in this batch that the
"infinite slab" bone-weight-*blending* bug class (found and fixed in `generate_body.py`) has no
analog anywhere in the C++ runtime demo layer — every avatar demo audited in this batch either
consumes pre-baked glTF weights via `SkinnedModelEXT`/`AvatarRenderer` or, in this file's specific
case, deliberately uses zero-blend rigid single-bone weighting for synthetic content.

## Missing or Weak Tests
Not applicable — manual/visual-validation demo; the smoke-test summary print
(`attachCount=%d finalPartsSize=%zu`) is a light self-check, and the referenced
`avatar_attach_part_integration_test.cpp` covers the automated-assertion side.

## Positive Findings
The `PartTintEXT` fallback-behavior comment is a good example of verifying assumed behavior against
the actual source before depending on it, rather than guessing.

## Final Assessment
No findings. Directly confirms this file's synthetic-accessory logic uses zero-blend rigid
single-bone weighting, not a re-implementation of the already-fixed bone-weight-blending math.
