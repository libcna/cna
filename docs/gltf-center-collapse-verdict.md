# glTF center-collapse verdict (`GLTF-011`)

**Task:** `GLTF-011` — the terminus of the P0 center-collapse track (`plan_gltf.md` §28).
**Question answered:** *why do imported glTF models collapse toward the centre?*
**Answer:** two independent mechanisms, both now fixed, plus three further defects that damage
geometry in other ways. Every one is named below with its first divergent layer, its before/after
numbers, its owning task and the permanent fixture that locks it.

This report depends on the fixes, never the other way round (`plan_gltf.md` §28.2). It is written
**after** `GLTF-007`, `GLTF-063`, `GLTF-071`, `GLTF-115`, `GLTF-248` and `GLTF-260` landed, and it
records what those tasks measured — it does not re-open them.

---

## 1. Verdict in one paragraph

"Collapsed toward the centre" was never one bug. **Two** independent mechanisms each produced it on
their own, from opposite ends of the pipeline:

| # | Mechanism | Who it hit | First divergent layer | Closed by |
|---|---|---|---|---|
| **A** | Every mesh instance was emitted in **mesh-local space with an identity bone**, because the import data model had nowhere to put a node transform | every multi-part rigid asset | **L4** | `GLTF-103` → `GLTF-113` → `GLTF-114` → `GLTF-115` |
| **B** | The **skin ancestry above the joint set was dropped** from the bind pose while the file's `inverseBindMatrices`, which contain it, were kept verbatim — so every skinned vertex was multiplied by the *inverse* of what was lost | every skinned character rigged under an armature | **L4** | `GLTF-245` → `GLTF-247` → `GLTF-248` → `GLTF-260` |

Mechanism A superimposes every part at the origin. Mechanism B multiplies the character by the
reciprocal of the armature transform — for the very common centimetre-authored rig or Blender
axis-conversion node carrying a uniform scale, that is literally a division by that scale.

Three further proven defects corrupt geometry without collapsing it: a sparse index accessor
decoding to all zeros (**D4**, fixed), non-`TRIANGLES` topology silently reinterpreted (**D5**,
partially remediated), and rigid node animation being dropped (**D6**, open). One more (**D7**,
factor-only PBR material) is a shading defect, not a geometric one.

**Both collapse mechanisms are closed.** Neither is suppressed by a known-defect test any more:
each is asserted by an ordinary green test through the real loader, so a regression fails as a
normal build break.

---

## 2. How to read a row of this report

The campaign's oracle is a seven-layer ladder (`docs/gltf-conformance.md` §3.1). The **first
divergent layer** is the highest layer at which a fixture still matched the specification — the
layer *below* the one where the defect first becomes measurable. It is the single most useful fact
per defect, because it names the layer that owns the bug and exonerates every layer beneath it.

| Layer | What it measures |
|---|---|
| L1 | container/JSON structure counts |
| L2 | decoded accessor arrays |
| L3 | the semantic mesh (`MeshOut`) — positions, indices, material |
| L4 | **world-space** vertex positions, including node composition and the skin equation |
| L5 | the exact vertex/index **bytes** handed to `VertexBuffer::SetData` |
| L6 | effect parameters bound per draw (not yet built — `GLTF-008`) |
| L7 | rendered image (not yet built — `GLTF-009`) |

Two structural facts fall straight out of the table below and are worth stating up front:

* **The accessor layer was never the problem.** L2 was correct on every single fixture, including
  the ones whose geometry was destroyed. `GLTF-041` exists to keep it that way.
* **L5 alone could never have found the collapse.** For D1/D2/D3 the vertex bytes are *byte-identical
  before and after the fix* (§3.1) — positions stay mesh-local by design, and the placement lives in
  a `ModelBone`. A byte oracle looking only at the vertex buffer would have declared the importer
  healthy while every part sat on top of every other part.

---

## 3. Per-asset verdicts

### 3.1 D1 — node TRS discarded · `xf-shared-mesh` · first divergent layer **L4**

**Owning task:** `GLTF-113` (data model) under `GLTF-103` (architecture), locked by `GLTF-115`.

Minimal reproducer: one mesh, two nodes; the second translated `[10,0,0]`.

```text
Node 0  "InstanceAtOrigin"      local = I
Node 1  "InstanceTranslatedX10" local = T(10,0,0)
mesh 0  triangle, local positions (0,0,0) (1,0,0) (0,1,0)
```

| Layer | Expected (spec-derived) | Before (`fb37282`) | After |
|---|---|---|---|
| L2 | positions `(0,0,0) (1,0,0) (0,1,0)` | ✅ same | ✅ same |
| L3 | same, mesh-local | ✅ same | ✅ same |
| **L4** | instance 0 → `(0,0,0) (1,0,0) (0,1,0)`; instance 1 → `(10,0,0) (11,0,0) (10,1,0)`; world X ∈ **[0,11]** | ❌ **both instances identity**, world X ∈ **[0,1]** | ✅ X ∈ **[0,11]**, 2 instances, matrices not all identity |
| L5 | 3 vertices × stride 32, positions `(0,0,0) (1,0,0) (0,1,0)` | ✅ same | ✅ **byte-identical** |

Transform chain, after:

```text
instance 0: world = I
instance 1: world = T(10,0,0)          column-major [1,0,0,0, 0,1,0,0, 0,0,1,0, 10,0,0,1]
```

**Root cause.** `CollectMeshGroups` produced `struct MeshGroup { const cgltf_skin*;
std::vector<const cgltf_mesh*>; }` — no `cgltf_node*`, no matrix, no instance identity anywhere in
the import data model. The mesh was recorded twice and the node not at all, so the second instance
was an exact duplicate of the first.

**Why L5 is unchanged.** `GLTF-103` adopted Option A (a real `ModelBone` hierarchy), so vertex
positions deliberately stay mesh-local and the placement reaches the model as a bone. That is what
makes instancing survive: `xf-shared-mesh`'s two placements share one mesh rather than duplicating
its vertices.

### 3.2 D2 — parent→child composition discarded · `xf-parent-child` · first divergent layer **L4**

**Owning task:** `GLTF-113`, locked by `GLTF-115`.

```text
Node 1 "Parent" local = S(2,2,2)
  └ Node 0 "Child" local = T(0,3,0), mesh 0
```

| Layer | Expected | Before | After |
|---|---|---|---|
| L2 / L3 | positions `(0,0,0) (1,0,0) (0,1,0)` | ✅ | ✅ |
| **L4** | world = `parentWorld * childLocal`; positions `(0,6,0) (2,6,0) (0,8,0)`; Y ∈ **[6,8]** | ❌ Y ∈ **[0,1]** (nothing composed) | ✅ Y ∈ **[6,8]** |

World matrix, after — column-major `[2,0,0,0, 0,2,0,0, 0,0,2,0, 0,6,0,1]`. Note the child's authored
translation of 3 lands at **6**, because the parent's scale multiplies it; a fix that merely
concatenated translations would land at 3 and pass a weaker test.

### 3.3 D3 — `node.matrix` discarded · `xf-matrix-node` · first divergent layer **L4**

**Owning task:** `GLTF-113`, locked by `GLTF-115`.

```text
Node 0 "MatrixNode" matrix = T(4,5,6)   (authored as a 16-float column-major matrix, not TRS)
```

| Layer | Expected | Before | After |
|---|---|---|---|
| L2 / L3 | positions `(0,0,0) (1,0,0) (0,1,0)` | ✅ | ✅ |
| **L4** | positions `(4,5,6) (5,5,6) (4,6,6)`; X ∈ **[4,5]** | ❌ X ∈ **[0,1]** | ✅ X ∈ **[4,5]** |

Same mechanism as D1/D2 — the data model had nowhere to put it. `BuildSceneGraph` now reads the
local transform through `cgltf_node_transform_local`, which already applies the specification's own
"`matrix`, **or else** TRS" exclusivity rule, so both authorings compose identically and no
CNA-side branch decides between them.

### 3.4 D4 — sparse **index** accessor decoded to zeros · `sparse-indices` · first divergent layer **L3**

**Owning task:** `GLTF-063`.

A quad whose index accessor uses `accessor.sparse` to turn a degenerate second triangle into a real
one.

| Layer | Expected | Before | After |
|---|---|---|---|
| **L2** | the accessor is readable and cgltf resolves the sparse override | ✅ **correct** | ✅ correct |
| **L3** | indices `[0,1,2,0,2,3]` | ❌ **`[0,0,0,0,0,0]`** — the quad collapsed to one degenerate point | ✅ `[0,1,2,0,2,3]` |
| L5 | `sparse-indices.ib.bin` = `00 00 01 00 02 00 00 00 02 00 03 00` | ❌ all zero | ✅ byte-exact |

**This is the cleanest layer localisation in the campaign.** L2 passed and L3 failed *on the same
accessor*, which proves the defect was in CNA's index-reading call, not in accessor decoding.
`cgltf_accessor_read_index` returns `0` when `accessor->is_sparse` or `accessor->buffer_view` is
null, with **no error channel**, and `ExtractMesh` checked neither. `GLTF-063` replaced that call
with a CNA-side sparse-aware, bounds-checked reader and added the `index < POSITION.count`
validation.

Golden vertex buffer (stride 32, 4 vertices — the quad the zeroed indices made unreachable):

```text
vertex 0  pos (0,0,0)  nrm (0,0,1)  uv (0,0)
vertex 1  pos (1,0,0)  nrm (0,0,1)  uv (0,0)
vertex 2  pos (1,1,0)  nrm (0,0,1)  uv (0,0)
vertex 3  pos (0,1,0)  nrm (0,0,1)  uv (0,0)     ← only reachable once D4 was fixed
```

### 3.5 D5 — non-`TRIANGLES` modes reinterpreted · `mode-triangle-strip`, `mode-points` · first divergent layer **L3**

**Owning tasks:** `GLTF-071` (landed) + **`GLTF-072` (open)**. Status: `partially-remediated`.

| Fixture | Expected | Before | Today |
|---|---|---|---|
| `mode-triangle-strip` (mode 5) | two triangles `[0,1,2]`, `[2,1,3]` | ❌ read as a triangle list: **one** triangle, vertex 3 unreachable | ⚠️ import **rejected** naming `TRIANGLE_STRIP` / `mode 5` |
| `mode-points` (mode 0, non-indexed) | four points, **zero** triangles | ❌ one triangle from the implicit index range | ⚠️ import **rejected** naming `POINTS` / `mode 0` |

`prim.type` was never read. `GLTF-071` reads it, classifies all seven modes, carries the mode on
`MeshOut`, and rejects what CNA cannot yet honour **with the mode named**. The silent corruption is
gone; the conversion is not written. Both fixtures record `l5.supported = false` with
`blockedBy: ["GLTF-072"]`, so the layer is visibly absent rather than quietly unasserted.

**This is the only remaining geometric defect from the original P0 audit**, and it is a *refusal to
import*, not a wrong import. It is tracked, loud, and cannot corrupt an asset.

### 3.6 D8 (part 1) — skin ancestry dropped · `skin-armature-ancestor` · first divergent layer **L4**

**Owning tasks:** `GLTF-245` (ancestry) + `GLTF-247` (mesh-space term), locked by `GLTF-248`.

```text
Node 1 "Armature"  local = T(0,100,0)
  └ Node 0 "Joint0"        local = I          → global = T(0,100,0)
Node 2 "SkinnedMeshNode"   local = I, mesh 0, skin 0
skin.inverseBindMatrices[0] = T(0,-100,0)     ← correctly authored: the true inverse of the global
```

The joint matrix the specification requires:

```text
jointMatrix = inverse(globalTransform(meshNode)) · globalTransform(joint) · inverseBindMatrix
            = I · T(0,100,0) · T(0,-100,0)
            = identity
```

| | Value |
|---|---|
| **Before** (`fb37282`) | `bindPoseLocal` = `T(0,0,0)` (the armature's `[0,100,0]` **dropped**), `inverseBindGlobal` = `T(0,-100,0)` (kept verbatim) ⇒ **joint matrix `T(0,-100,0)`** |
| **After** | **joint matrix = identity**, exactly |
| Skinned position of local `(1,0,0)` | before `(1,-100,0)` → after **`(1,0,0)`** |

That asymmetry — ancestry dropped from one factor of the product but present in the other — is
precisely why the error is the *inverse* of the lost transform rather than the transform itself. On
a uniformly scaled armature it is a division by that scale, i.e. the collapse.

### 3.7 D8 (part 2) — the mesh-node cancellation · `skin-mesh-node-transform` · first divergent layer **L4**

**Owning task:** `GLTF-260` (depends on `GLTF-247`, `GLTF-114`, `GLTF-248`).

This fixture did not exist before P0-D. It had to be created because `skin-armature-ancestor`
**cannot** detect the defect `GLTF-260` owns: its mesh node is untransformed, so a missing
cancellation and a correct one produce the same number.

```text
Node 1 "SkinnedMeshNode" local = T(0,0,50), mesh 0, skin 0
Node 0 "Joint0"          local = I
skin.inverseBindMatrices[0] = I
```

With an identity joint and an identity IBM, the **entire** joint matrix is the cancellation, which
makes three outcomes numerically distinct:

| Outcome | Joint matrix `M43` | Meaning |
|---|---|---|
| no cancellation | `0` | `GLTF-247`'s term missing |
| **cancelled exactly once** | **`-50`** | **correct** |
| cancelled twice | `-100` | the node bone re-applied what was already cancelled |

**Measured today: `T(0,0,-50)`** — one cancellation, and the skinned positions are
`(0,0,-50) (1,0,-50) (0,1,-50)`, which is the mesh-local triangle carried into the joint's space.

`GLTF-260` asserts **both** halves, because either alone is satisfiable by a wrong implementation:

1. the cancellation exists and is applied exactly once; **and**
2. the mesh node's `ModelBone` still exists *carrying its transform*, while the mesh stays parented
   to the identity root — so the hierarchy `GLTF-114` introduced cannot silently re-apply it.

Half (2) is the one that is easy to "fix" the wrong way. A skinned mesh's placing transform must be
**ignored, not deleted**: deleting the nodes would make some assets render correctly today by
accident and would make `GLTF-247`'s cancellation impossible to express, because there would be
nothing left to cancel.

**Design note worth preserving.** Both the ancestry prefix and the cancellation ride on a *per-root
prefix* (`BoneOut::parentWorldPrefix`, `SkinningData::SkeletonRootPrefix`) rather than being baked
into the bind pose. Baking them in would have been silently undone the moment an animation clip
replaced a root joint's local transform. Carried separately, an animated root joint substitutes only
its own local transform, exactly like any other bone — `AnimationPlayer` composes
`world(root) = local · prefix`, and an absent prefix array reads as all-identity, so a skeleton built
without that context is unaffected.

### 3.8 D6 — rigid node animation dropped · `anim-rigid-node` · first divergent layer **L4** · **OPEN**

**Owning task:** `GLTF-284` (after `GLTF-103`…`GLTF-114`, which are now done).

One `LINEAR` rotation channel on an unskinned mesh node — a door, a turntable, a clock hand. There
is no skin anywhere in the file.

| | Value |
|---|---|
| Expected | 1 clip, 1 track, the node poses through a quarter turn about +Z |
| Today | **0 clips**, no `animations` key in the `.cnj`, **no warning** |

Two separate gates cause it: the converter calls `ExtractClips` only for a *skinned* group, and
`ExtractClips` itself resolves every channel target against the skin's joint set, so a channel
targeting an ordinary node is discarded. Even if called, it would emit a clip with zero tracks.

**Not a collapse mechanism** — the asset imports in the right place, it just does not move.
`GLTF-284` is now substantially easier than when the audit was written, because the real `ModelBone`
scene hierarchy from `GLTF-103`/`GLTF-114` gives the animation something to drive.

### 3.9 D7 — factor-only PBR material lost · `mat-factor-only-gold` · first divergent layer **L3** · **OPEN**

**Owning tasks:** `GLTF-217` / `GLTF-228` / `GLTF-229` (see also `GLTF-215`).

A metallic-roughness material with **no texture maps**: gold `baseColorFactor [1,0.72,0.315,0.5]`,
non-default metallic/roughness/emissive factors, `alphaMode BLEND`, `doubleSided`.

| | Value |
|---|---|
| Expected | a PBR material carrying every authored factor and alpha state |
| Today | `usePbr = false` → **`BasicEffect`**, stride 32, **zero** material fields emitted; renders opaque white |
| Lost fields | `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor`, `alphaMode`, `alphaCutoff`, `doubleSided` |

The selection rule is `usePbr = !colored && (normalImage || metallicRoughnessImage)` — presence of a
*map*, not presence of a *material*. glTF's default material **is** metallic-roughness, so a
factor-only material can never select `PbrEffect`; and because the factor assignments sit behind
that same `usePbr` guard, even the three fields `MeshOut` could carry are left at their defaults.

**Not a collapse mechanism** — geometry is correct, shading is wrong.

---

## 4. What is locked forever

Every fixed defect is now asserted by an **ordinary green test through the real loader**, not by an
inverted known-defect test. A regression is a normal build break naming the fixture.

| Defect | Regression fixture(s) | Asserting test | What a regression looks like |
|---|---|---|---|
| D1 | `xf-shared-mesh` | `GltfConformanceL4`, `GltfSceneGraphBones.SharedMeshGetsOneBonePerInstancingNode` | world X collapses from `[0,11]` to `[0,1]` |
| D2 | `xf-parent-child` | `GltfConformanceL4`, `GltfSceneGraphBones.DeepHierarchyMapsOntoBonesInLockstep` | world Y collapses from `[6,8]` to `[0,1]` |
| D3 | `xf-matrix-node` | `GltfConformanceL4`, `GltfSceneGraphBones.MatrixAuthoredNodeMapsOntoItsBone` | world X collapses from `[4,5]` to `[0,1]` |
| — | `xf-identity` | `GltfSceneGraphBones.UntransformedControlCase` | the zero point: catches a "fix" that transforms things that should not move |
| D4 | `sparse-indices` | `GltfConformanceL3`, `GltfConformanceL5`, `GltfIndexDecode` | indices revert to all-zero; `.ib.bin` mismatch at byte 0 |
| D5 | `mode-triangle-strip`, `mode-points` | `GltfPrimitiveTopology`, `GltfKnownDefect` | a mode is silently reinterpreted instead of named and rejected |
| D8 (ancestry) | `skin-armature-ancestor` | `GltfSkinSpaces.RootJointCarriesTheSceneAncestryAboveTheJointSet`, `…SkinnedVertexLandsWhereTheSpecificationSaysItDoes` | joint matrix drifts from identity to `T(0,-100,0)` |
| D8 (cancellation) | `skin-mesh-node-transform` | `GltfSkinSpaces.MeshNodeTransformIsCancelledExactlyOnce` | `M43` becomes `0` (missing) or `-100` (doubled) |
| D8 (structure) | `skin-armature-ancestor` | `GltfSceneGraphBones.SkinnedMeshKeepsItsNodeBoneButIsNotTransformedByIt`, `…AncestryIsPreservedInTheSceneModelButDoesNotTransformIt` | the mesh node's bone is deleted rather than merely not applied |

A defect record is **never deleted** from the corpus ledger. A fixed one keeps its original
measurement under `priorActual`, dated with the baseline commit it was taken on, and becomes the
regression witness. `GltfKnownDefectTests` asserts that bookkeeping in both directions: an open
defect must have a test there, and a remediated one must not.

---

## 5. The finding that matters most: **the oracle was wrong too**

During `GLTF-260`, adding `skin-mesh-node-transform` surfaced a defect **in the reference
expectation, not in CNA**.

Both L4 oracles — the Python generator helper (`tools/gltf_fixtures/manifest.py`) and the C++
`EvaluateWorldPositionsEXT` (`GltfOracleEXT.cpp`) — placed a **skinned** mesh by its own node's world
transform, exactly as they place a rigid one. Specification §3.7.3 says the **joints** place a
skinned mesh, and the joint matrix carries `inverse(globalTransform(meshNode))` precisely so the
node's own transform cancels out.

```diff
- instance.worldMatrix = world;
+ const GltfMatrix placement = node.skin != nullptr ? IdentityMatrix() : world;
+ instance.worldMatrix = placement;
```

Left alone, the oracle would have **demanded that CNA apply a transform the specification says to
ignore** — on the exact fixture written to prove that CNA must not apply it. It would have passed
review, gone green against a wrong implementation, and become permanent: a *golden bug*, the worst
outcome available to a conformance campaign, because it converts a specification violation into a
regression test that defends the violation.

The correction: a skinned instance reports the **identity** placement at `l4.instances`, and the
skinned result lives in the separate `l4.skin` block with the joint matrices spelled out. The node's
world transform is still computed and still self-checked against `cgltf_node_transform_world` — it is
simply not that mesh's placement.

**Recorded explicitly, as `GLTF-011`'s job:** the reference expectation was changed **to match the
specification**, not to match the implementation. The distinction is the whole point. `docs/gltf-
conformance.md` §3.3 states the standing rule this instance obeys — *never weaken an expectation to
make the current implementation green; if a fixture contradicts the forensic audit, stop and
investigate the contradiction*. Here the investigation ended with the fixture being right about
CNA and wrong about the specification, and the fix went into the oracle.

Any future session reading `l4.instances` for a skinned fixture and finding the identity where a
node transform was authored should read this section rather than "correcting" it back.

---

## 6. What the P0 track did **not** cover

The center-collapse track is closed. It is deliberately narrow, and these remain open:

| Item | Owning task | Why it is not a collapse defect |
|---|---|---|
| Topology conversion (strip/fan/points/lines) | **`GLTF-072`** (+`GLTF-077`, `GLTF-078`) | a named rejection, not a wrong import — the last geometric defect from the P0 audit |
| Rigid node animation | **`GLTF-284`** | the asset is in the right place, it just does not move |
| Factor-only PBR materials | **`GLTF-217`/`228`/`229`** (+`GLTF-215`) | shading, not geometry |
| L6 draw-parameter capture | `GLTF-008` | harness not yet built |
| L7 image oracle | `GLTF-009` | harness not yet built |
| `ctest -L gltf-conformance` single label | `GLTF-010` | needs L6/L7 |
| `cgltf_validate()` / `extensionsRequired` | `GLTF-021`, `GLTF-023` | Track B Phase 1 |

Per `plan_gltf.md` §28, Track B's Phases 8–23 were gated on this report and are now unblocked.

---

## 7. Corpus accounting

| Quantity | Value |
|---|---|
| Generated fixtures today | **16** distinct assets (`manifest.json` → `distinctAssetCount`), each with a `.glb` twin |
| Planned corpus (`plan_gltf.md` §24.2) | **135** distinct assets — completed by `GLTF-399` |
| L5 goldens | 14 of 16 (the two `GLTF-071` rejects record `l5.supported = false`) |

`skin-mesh-node-transform` is the corpus's 16th asset, added by `GLTF-260`. It was **new to the
generated corpus but not to the plan** — §15.4's skinning ladder already listed it among its 13
assets. P0-D incremented the §24.2 skinning group from 13 to 14 (and the total from 135 to 136) as
though it were newly planned; this report corrects that back, since the ladder enumerates 13 and
§24.1's rule is that the column sums to the distinct-asset total. The other four cross-referenced
ladders were re-counted at the same time and all agree: transforms 17 (§11.4), morph 13 (§16.3),
animation 10 (§17.2 excluding the morph-owned `anim-weights-*`), skinning 13 (§15.4).

---

## 8. Reproducing every number in this report

```bash
# the converter, exactly as the forensic audit built it
cmake -S . -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=OFF \
      -DCNA_BUILD_EXAMPLES=OFF -DCNA_ENABLE_NET=OFF
cmake --build cmake-build-debug --target cna_tool_gltf_to_cnj -j4

# the conformance ladder
cmake -S . -B cmake-build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=ON \
      -DCNA_BUILD_EXAMPLES=OFF -DCNA_ENABLE_NET=OFF
cmake --build cmake-build-tests --target CnaTests -j4
./cmake-build-tests/CnaTests --gtest_filter='Gltf*'      # from the repository root

# regenerate the corpus (must be byte-identical)
python -m gltf_fixtures --out tests/assets/gltf
```

Per-defect before/after numbers live in each fixture's `<id>.expected.json` under
`defects[].currentActual` and `defects[].priorActual`; every `priorActual` is dated with the
baseline commit `fb3728267e8f2179d43b96357ff372ae712b7e7f` it was measured on.

---

## 9. Verdict

For the assets the owner reported as deformed:

* if the asset is **rigid and multi-part** — the cause was **D1/D2/D3**, first divergent at **L4**,
  owned by `GLTF-113` under `GLTF-103`, locked by `xf-shared-mesh` / `xf-parent-child` /
  `xf-matrix-node`. **Fixed.**
* if the asset is a **skinned character under an armature** — the cause was **D8**, first divergent
  at **L4**, owned by `GLTF-245` + `GLTF-247` and closed by `GLTF-260`, locked by
  `skin-armature-ancestor` / `skin-mesh-node-transform`. **Fixed.**
* if the asset uses a **sparse index accessor** — **D4**, first divergent at **L3**, owned by
  `GLTF-063`, locked by `sparse-indices`. **Fixed.**
* if the asset uses a **non-`TRIANGLES` primitive mode** — **D5**, first divergent at **L3**, owned
  by `GLTF-071` (landed) and **`GLTF-072` (open)**. The asset now fails to import with the mode
  named, instead of importing wrongly.
* if the asset is **in the right place but does not move** — **D6**, owned by `GLTF-284`. **Open.**
* if the asset is **in the right place but renders white** — **D7**, owned by
  `GLTF-217`/`228`/`229`. **Open.**

The P0 center-collapse track (`plan_gltf.md` §28) is **complete**.
