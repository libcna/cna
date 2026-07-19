# Audit: src/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.cpp`
- Audit status: AUDITED (full read, 243 lines) — given special, high-care scrutiny per this
  audit's investigation into a project-memory-recorded joint-weight-blending defect history
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: NOXNA extension, no FNA equivalent
- Main related tests: not independently located in this pass

## Purpose
Implements `AddPartEXT`/`RemovePartEXT`/`AttachPartEXT` (owned-resource lifecycle management) and
`ComputeBoneTransformsEXT` (per-bone skinning-matrix computation from a named animation clip).

## Executive Verdict
Correct, and a mature, well-hardened file: at least six distinct, specifically-tracked defects
(Task 11.1, 11.2, 11.3, 11.4, 11.5, 11.21) are confirmed genuinely fixed and present in the code as
it stands. **Key corrective finding for this project's own audit trail: `ComputeBoneTransformsEXT`
does NOT perform per-vertex multi-bone weight blending, and therefore cannot be the location of the
"infinite slab" joint-weight-blend defect this project's persistent memory records** (a joint
weighted to one bone incorrectly picking up an entire other body-part's transform) — see Detailed
Findings for the full analysis of where that logic must actually live instead.

## Checklist Results
- `ComputeBoneTransformsEXT` (Task 11.3): validates `ParentBoneIndices`/`BindPoseLocal`/
  `InverseBindPoseGlobal` all equal `BoneCount` before indexing, throwing `System::ArgumentException`
  on mismatch — confirmed present and correct, matching the file's own comment describing a
  previously-real out-of-bounds `std::vector::operator[]` read this check now prevents.
- Looping-position math (Task 11.1): floor-mod-via-raw-ticks, avoiding an unbounded iterative
  wraparound cost — confirmed present, identical convention to `AnimationPlayer::Update()`
  (audited in this same batch).
- Topological-order validation (Task 11.2): `if (parent >= i) throw ...` before indexing
  `worldTransforms[parent]` — confirmed present and correctly reasoned (the file's own comment
  correctly notes this also transitively rules out cycles).
- `RemovePartEXT` (Task 11.4/11.5): correctly removes matching entries from all four *parallel*
  owned-resource vectors (`Parts`/`vertexBuffers_`/`indexBuffers_`/`ownedParts_`) in lockstep by
  index, and separately locates+removes the *non-parallel* `textures_` entry by pointer identity
  (its own comment correctly explains why `textures_` isn't parallel — only populated when a part
  actually has a texture) — confirmed this fixes the documented prior GPU-resource leak from
  erasing only the non-owning `Parts` descriptor vector.
- `AttachPartEXT` (Task 11.21): correctly validates `BoneCount` match before attaching, replaces
  same-named parts before appending (Task 11.4 fix, preventing a duplicate-rendered part), and
  moves (not copies) every owned-resource vector from `other` into `this` — confirmed correct
  ownership transfer, `other` left in a valid, fully-drained state.

## Detailed Findings

### Corrective note (not a defect): `ComputeBoneTransformsEXT` cannot be the site of the
"infinite slab" joint-weight-blend defect this project's memory records
Read this function (lines 144-241) with specific attention to per-vertex bone-weight blending, per
this audit's directive. **This function performs no per-vertex weight blending at all.** It
computes exactly one world-space transform *per bone* via a single-parent-chain composition
(`worldTransforms[i] = localTransforms[i] * worldTransforms[parent]`, structurally identical to
`AnimationPlayer::RecomputeTransforms` in this same batch), then multiplies each by that bone's own
`InverseBindPoseGlobal` to produce `outWorldBones[i]` — one matrix per bone, with no combination of
*multiple* bones' matrices for any single vertex anywhere in this function. There is no
`BlendWeight`/`BlendIndices` vertex-attribute consumption, no weighted sum of several bones'
matrices, and no normalization step of the kind a real "vertex weighted 70% to Bone A / 30% to Bone
B" skinning blend would require.

**This means the "infinite slab" defect — where a joint weighted to one bone incorrectly picked up
an entire other body-part's transform — cannot live in this function.** That symptom requires
*per-vertex* multi-bone weight consumption, which happens in one of two other places, neither of
which is in this file or this shard:
1. The GPU vertex shader's own per-vertex skin-matrix blend (`skinMat = weight.x*bones[idx.x] +
   weight.y*bones[idx.y] + ...`), computed per-backend in each graphics backend's own
   `skinned3d.vert.*` shader — this is exactly where this project's own cross-cutting findings
   document already-confirmed, unrelated skinned-shader defects (the missing
   `WorldInverseTranspose` normal transform, confirmed in all 14 backends with a `SkinnedEffect`
   implementation) as living. It is plausible, though not confirmed in this pass (out of this
   file's/shard's scope), that a similar per-vertex weight-blend defect could live in the same
   shader family.
2. The content-import pipeline's own `BlendWeight`/`BlendIndices` vertex-attribute population and
   weight normalization (e.g. wherever glTF's `WEIGHTS_0`/`JOINTS_0` accessors are converted into
   this project's vertex format) — also out of this file's/shard's scope.

This is recorded here as a scope-narrowing finding rather than a defect: this audit's own directive
specifically anticipated finding the "infinite slab" bug's root cause in this file, and the correct,
verified conclusion is that it is not here. Future audit passes investigating that symptom should
focus on the per-backend skinned vertex shaders and/or the content-import weight-population code,
not `SkinnedModelEXT::ComputeBoneTransformsEXT`.

## Cross-File Observations
`ComputeBoneTransformsEXT`'s structure (validate array-size invariants -> resolve looping position
-> sample per-bone local transforms -> compose world transforms in topological order -> multiply by
inverse bind pose) is essentially identical to `AnimationPlayer::RecomputeTransforms` (audited in
this same batch) — both explicitly cross-reference each other's Task 11.1/11.2-equivalent fixes in
their own comments, confirming the two independent systems converged on the same correct design
rather than one copying a bug from the other.

## Missing or Weak Tests
Not independently located in this pass. Given the corrective finding above, a targeted test
exercising `ComputeBoneTransformsEXT` output isn't the right place to look for the "infinite slab"
symptom — the right test target is whichever backend's skinned-vertex-shader test suite exercises a
non-trivial multi-bone-weighted vertex.

## Positive Findings
Every one of the six specifically-tracked prior defect fixes (Task 11.1, 11.2, 11.3, 11.4, 11.5,
11.21) is independently confirmed present and correctly implemented in the code as it stands today
— a mature, well-maintained file. The corrective scope-narrowing finding above is itself a positive
contribution to this project's own ongoing investigation into the "infinite slab" defect's true
location.

## Final Assessment
No new defects found. One significant corrective/scope-narrowing finding: the "infinite slab"
joint-weight-blend defect this project's memory records does not and cannot originate in this
function — it must live in per-backend skinned-vertex-shader code or the content-import weight
pipeline, neither of which is in this file.
