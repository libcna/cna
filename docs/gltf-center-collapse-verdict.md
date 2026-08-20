# glTF center-collapse verdict (`GLTF-011`)

**Task:** `GLTF-011` — the terminus of the P0 center-collapse track (`plans/plan_gltf.md` §28).
**Question answered:** *why do imported glTF models collapse toward the centre?*
**Answer:** two independent mechanisms, both now fixed, plus three further defects that damage
geometry in other ways. Every one is named below with its first divergent layer, its before/after
numbers, its owning task and the permanent fixture that locks it.

This report depends on the fixes, never the other way round (`plans/plan_gltf.md` §28.2). It is written
**after** `GLTF-007`, `GLTF-063`, `GLTF-071`, `GLTF-115`, `GLTF-248` and `GLTF-260` landed, and it
records what those tasks measured — it does not re-open them. §3.5 was extended when `GLTF-072`
closed the topology conversion immediately afterwards.

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
fixed for the triangle topologies; the point and line modes are now an explicit refusal to import,
pending a draw path), and rigid node animation being dropped (**D6**, now imported and reported —
serialisation pending). One more (**D7**, factor-only PBR material) is a shading defect, not a
geometric one; its factors now survive and only the alpha and sidedness state is still lost.

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

### 3.5 D5 — non-`TRIANGLES` modes reinterpreted · the seven `mode-*` fixtures · first divergent layer **L3**

**Owning tasks:** `GLTF-071` + `GLTF-072` (both landed). Status: `partially-remediated` — see the
scope boundary below.

| Fixture | Expected | Before | Today |
|---|---|---|---|
| `mode-triangle-strip` (mode 5) | two triangles `[0,1,2]`, `[2,1,3]` | ❌ read as a triangle list: **one** triangle, vertex 3 unreachable | ✅ **converted at import** to `[0,1,2,2,1,3]`, byte-exact at L5 |
| `mode-triangle-fan` (mode 6) | two triangles `[0,1,2]`, `[0,2,3]` | ❌ read as a triangle list | ✅ **converted at import** to `[0,1,2,0,2,3]` |
| `mode-triangles` (mode 4) | two triangles | ✅ correct | ✅ unchanged, byte for byte |
| `mode-points` (mode 0, non-indexed) | four points, **zero** triangles | ❌ one triangle from the implicit index range | ⚠️ import **rejected** naming `POINTS` / `mode 0` |
| `mode-lines` (mode 1) | two segments | ❌ one triangle | ⚠️ import **rejected** naming `LINES` / `mode 1` |
| `mode-line-strip` (mode 3) | two segments | ❌ one triangle | ⚠️ import **rejected** naming `LINE_STRIP` / `mode 3` |
| `mode-line-loop` (mode 2) | three segments, the last closing | ❌ one triangle | ⚠️ import **rejected** naming `LINE_LOOP` / `mode 2` |

`prim.type` was never read. `GLTF-071` reads it and classifies all seven modes. `GLTF-072` then
converts the two topologies that have an **exact triangle-list equivalent**, preserving winding —
a strip's odd triangle emits `(i+1, i, i+2)`, so every triangle faces the same way and back-face
culling behaves as the author intended.

The equivalence is asserted at the byte level, which is what makes the conversion trustworthy:
`mode-triangle-strip` and `mode-triangles` author the same quad by different routes and produce
**byte-identical index buffers**, while `mode-triangle-fan` — the same four indices under the other
rule — must not match. A conversion that ignored the mode could not satisfy both.

`MeshOut` now carries `sourceTopology` beside `topology`, so a conversion is visible rather than
lossy: `topology` is always `Triangles` on anything the importer returns, while `sourceTopology`
still names what the file declared.

**Scope boundary — why D5 is not yet `fixed`.** The four topologies that describe no triangles stay
rejected, and that is a deliberate decision rather than unfinished work. They **decode correctly**;
what they lack is a *draw path*, because every loader still computes `numIndices / 3`. Importing
them now would move the original defect from the import layer to the draw layer rather than fix it.
`GLTF-073` (a real `PrimitiveType` on `ModelMeshPart`), `GLTF-078` (a topology-aware primitive
count), `GLTF-076` (`LINE_LOOP`'s closing segment) and `GLTF-077` (the points decision) own them,
and each rejection message names the task that would lift it.

**No geometric defect from the original P0 audit remains.** What is left is a *refusal to import*,
not a wrong import: tracked, loud, and unable to corrupt an asset.

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

### 3.8 D6 — rigid node animation dropped · `anim-rigid-node` · first divergent layer **L4** · **PARTLY OPEN**

**Owning tasks:** `GLTF-293` (landed) + **`GLTF-294` (open)**. The audit ledger originally named
`GLTF-284` here, which is the morph weight-vector validation task and has nothing to do with rigid
animation; corrected when `GLTF-293` landed.

One `LINEAR` rotation channel on an unskinned mesh node — a door, a turntable, a clock hand. There
is no skin anywhere in the file.

| | Value |
|---|---|
| Expected | 1 clip, 1 track, the node poses through a quarter turn about +Z |
| Before | **0 clips**, no `animations` key in the `.cnj`, **no warning** |
| After | **1 clip** `Spin`, **1 track** on the `SpinningMesh` node's own bone: identity at `t=0`, `(0,0,√2/2,√2/2)` at `t=1`, scale filled from the node's bind pose. Reported by the converter; **not yet written to the `.cnj`** |

Two separate gates caused the loss: the converter called `ExtractClips` only for a *skinned* group,
and `ExtractClips` resolved every channel target against the skin's joint set, so a channel
targeting an ordinary node was discarded. Even if called it would have emitted a clip with zero
tracks. `ExtractSceneNodeClips` removes both — it resolves against the scene graph instead, which is
only possible because `GLTF-103`/`GLTF-114` gave rigid nodes real `ModelBone`s to drive.

**Why it is not yet `fixed`.** A scene-node track's `boneIndex` is a `sceneNodeIndex`; the `.cnj`
clip schema has no field distinguishing that from a joint-palette slot (§15.1.2). Writing one anyway
would let a reader apply a scene index as a palette slot — a fresh silent corruption in place of the
old one. `GLTF-294` adds the field and the playback path; until then the converter *reports* the
clip by name and track count rather than dropping it, which is the property D6 was really about.

**Not a collapse mechanism** — the asset imports in the right place, it just does not move yet.

### 3.9 D7 — factor-only PBR material lost · `mat-factor-only-gold` · first divergent layer **L3** · **PARTLY OPEN**

**Owning tasks:** `GLTF-215`/`GLTF-216`/`GLTF-217`/`GLTF-219`/`GLTF-221` (landed) +
**`GLTF-228`/`GLTF-229`/`GLTF-231` (open)**.

A metallic-roughness material with **no texture maps**: gold `baseColorFactor [1,0.72,0.315,0.5]`,
non-default metallic/roughness/emissive factors, `alphaMode BLEND`, `doubleSided`.

| | Value |
|---|---|
| Expected | a PBR material carrying every authored factor and alpha state |
| Before | `usePbr = false` → **`BasicEffect`**, stride 32, **zero** material fields emitted; rendered opaque white |
| After | `usePbr = true` → **`PbrEffect`**, stride 48; `baseColorFactor` reaches `DiffuseColor` `(1, 0.72, 0.315)` with `Alpha` `0.5`, and metallic `0.9` / roughness `0.35` / emissive `(0.25, 0.1, 0)` all survive |
| Still lost | `alphaMode`, `alphaCutoff`, `doubleSided` |

The old rule was `usePbr = !colored && (normalImage || metallicRoughnessImage)` — presence of a
*map*, not of a *material model*. Because the factor assignments sat behind that same guard, even
the three fields `MeshOut` could already carry were left at their defaults, which is why the audit
recorded "zero material fields emitted".

`GLTF-215` replaced the rule with the model the file declares. Metallic-roughness is glTF's default
in two distinct ways and both now hold: a primitive with **no material at all** gets the default
material (`GLTF-217`), and a material that merely omits the optional `pbrMetallicRoughness` object
still uses that model with default factors. Only `KHR_materials_pbrSpecularGlossiness` and
`KHR_materials_unlit` are excluded, alongside the pre-existing `!colored` limit — no PBR shader
reads a vertex Color stream.

**Two consequences worth knowing.** Stride 32 no longer occurs anywhere in the corpus: an unskinned
uncoloured primitive is 48 and a skinned one 68. And the `DualTextureEffect` glTF path is
**superseded** — a base-colour + occlusion material now reaches `PbrEffect`'s real `OcclusionMap`
instead of the halved occlusion-as-lightmap approximation, which existed only because the old rule
left PBR unavailable to it. `DualTextureEffect` itself is untouched and still reachable through
every other content path.

**Why it is not yet `fixed`.** `MeshOut` has no field for `alphaMode`, `alphaCutoff` or
`doubleSided`, and `PbrEffect` has no parameter to put them in. Adding both is
`GLTF-228`/`GLTF-229`/`GLTF-231`, which sit behind `GLTF-025` — the API-change gate whose own
acceptance requires the design recorded *before* implementation.

**A gap this change exposed, and the policy that closes it.** Every untextured primitive now goes
through `PbrEffect`, whose `AmbientLightColor` defaults to `(0,0,0)` with all three directional slots
disabled — so a file declaring no light at all would render black. glTF does not require a scene to
declare lights, and most authored assets do not. The import therefore applies the effect's own
`EnableDefaultLighting()` rig **only** when the file expresses no lighting intent, so an asset that
authored its own lights is never overridden or dimmed. It is a CNA import policy rather than a
specification rule, and is pinned by
`GltfLightingPolicy.AFileThatDeclaresNoLightGetsTheDefaultLightingRig` — a numerical assertion on the
effect's own light state, since the visual consequence is invisible until `GLTF-009` exists.

**Not a collapse mechanism** — geometry was always correct; shading was wrong.

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
| D5 (conversion) | `mode-triangle-strip`, `mode-triangle-fan`, `mode-triangles` | `GltfConformanceL3`, `GltfConformanceL5.AConvertedTopologyProducesTheSameBufferAsAnExplicitTriangleList`, `GltfPrimitiveTopology` | a strip converts with the fan's rule, or loses the odd triangle's winding swap |
| D5 (rejection) | `mode-points`, `mode-lines`, `mode-line-strip`, `mode-line-loop` | `GltfPrimitiveTopology`, `GltfKnownDefect` | a mode with no draw path imports silently instead of being named and rejected |
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
| A draw path for the point and line topologies | **`GLTF-073`**, **`GLTF-076`**, **`GLTF-077`**, **`GLTF-078`** | a named rejection, not a wrong import; the decoding is correct and the gap is at the draw layer |
| Serialising and playing a rigid node clip | **`GLTF-294`** | the clip is now imported and reported (`GLTF-293`); the `.cnj` schema cannot yet say which index space a track targets |
| The alpha and sidedness state of a material | **`GLTF-228`**/**`229`**/**`231`**, behind the `GLTF-025` API gate | shading, not geometry; the factors themselves now survive (`GLTF-215`/`216`) |
| L6 draw-parameter capture | `GLTF-008` | harness not yet built |
| L7 image oracle | `GLTF-009` | renderer is now available; the fixed-rig corpus matrix and determinism harness remain open — `docs/gltf-conformance.md` §5.3 |
| `ctest -L gltf-conformance` single label | `GLTF-010` | landed for L0–L6 plus the ledger and the `.cnj` tool; gains an L7 entry when `GLTF-009` lands |
| `cgltf_validate()` / `extensionsRequired` | `GLTF-021`, `GLTF-023` | Track B Phase 1 |

Per `plans/plan_gltf.md` §28, Track B's Phases 8–23 were gated on this report and are now unblocked.

---

## 7. Corpus accounting

| Quantity | Value |
|---|---|
| Generated fixtures **when this report was written** | **21** distinct assets (`manifest.json` → `distinctAssetCount`), each with a `.glb` twin |
| Generated fixtures today | **27** — `GLTF-021`/`GLTF-023` added the first container and robustness fixtures, `GLTF-267` added §11.4's `xf-scale-nonuniform`, `GLTF-137` added `skin-plus-static-mesh`, `GLTF-222` added `mat-emissive-strength`, `GLTF-299` added `anim-nonzero-start`, `GLTF-249` added `skin-skeleton-hint`, `GLTF-317` added the three `camera-*` fixtures, `GLTF-241` added `mat-vertex-color-pbr`, and `GLTF-224`/`GLTF-225` added `mat-normal-occlusion-scale`, and `GLTF-060` added `accessor-count-mismatch`, and `GLTF-296` added `camera-animated-node`, and `GLTF-281`/`GLTF-282` added `morph-node-weights-override`, and `GLTF-256`/`GLTF-261` added `skin-unnormalized` and `skin-73-joints` |
| Planned corpus (`plans/plan_gltf.md` §24.2) | **135** when this report was written; **136** today — `GLTF-137` added a row to §15.4's ladder for a file shape no ladder foresaw, which is a deliberate plan change and not the count error below returning. Completed by `GLTF-399` |
| L5 goldens | 17 of 21 at the time of writing; **34 of 38 today** — the four point/line topologies gained theirs with `GLTF-073`/`GLTF-078`, and the two remaining assets are the rejection fixtures, which have no L5 by construction |

`skin-mesh-node-transform` was the corpus's 16th asset, added by `GLTF-260`; `GLTF-072` then
completed the topology group's seven, taking the generated corpus to 21. It was **new to the
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
* if the asset uses a **triangle strip or fan** — **D5**, first divergent at **L3**, owned by
  `GLTF-071` + `GLTF-072`, locked by `mode-triangle-strip` / `mode-triangle-fan` / `mode-triangles`.
  **Fixed:** converted to an equivalent triangle list at import, winding preserved.
* if the asset uses a **point or line mode** — still **D5**, but a draw-path gap rather than a
  decoding one, owned by `GLTF-073`/`GLTF-076`/`GLTF-077`/`GLTF-078`. The asset fails to import
  with the mode named, instead of importing wrongly.
* if the asset is **in the right place but does not move** — **D6**, owned by `GLTF-293` (landed:
  the clip is imported onto the node's own bone) and **`GLTF-294` (open:** carrying it through the
  `.cnj` and playing it).
* if the asset is **in the right place but renders white** — **D7**, owned by `GLTF-215` (landed:
  the material model selects the effect, and every authored factor survives to `PbrEffect`) and
  **`GLTF-228`/`229`/`231` (open:** `alphaMode`, `alphaCutoff`, `doubleSided`).

The P0 center-collapse track (`plans/plan_gltf.md` §28) is **complete**.
