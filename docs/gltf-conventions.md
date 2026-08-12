# glTF ↔ XNA conventions: what is *not* converted

`plan_gltf.md` `GLTF-105`. This document exists to state, once and in one place, the set of
conventions glTF 2.0 and XNA 4.0 **already agree on** — because every one of them is a place where
a well-meaning "fix" would silently break every asset in the project, and where the absence of
conversion code looks like an omission rather than a decision.

Every claim here is asserted by
`modules/content/tests/CNA/Internal/GltfImport/GltfConventionsTests.cpp`. A change that introduces
an axis flip, a handedness swap, a UV flip or a transform-order swap must fail one of those tests
first. That is the point: the invariants are cheap to state and expensive to rediscover.

## The agreements

| Convention | glTF 2.0 | XNA 4.0 | Conversion needed |
|---|---|---|---|
| Handedness | Right-handed | Right-handed | **None** |
| Up axis | +Y | +Y | **None** |
| Forward axis | −Z | −Z | **None** |
| Texture coordinate origin | Top-left, V down | Top-left, V down | **None** |
| Quaternion component order | `[x, y, z, w]` | `Quaternion(X, Y, Z, W)` | **None** — a straight member copy |
| Vertex winding for a front face | Counter-clockwise | Counter-clockwise | **None** |
| Distance unit | Metres (§3.4) | Undefined; whatever the game chooses | None at import; `unitScale` is the offline tool's own knob |

Because all of these agree, the importer contains **no axis remap, no handedness negation and no V
flip anywhere**. A position authored as `(1, 2, 3)` arrives in the vertex buffer as `(1, 2, 3)`; a
normal of `(0, 0, 1)` stays `(0, 0, 1)`; a UV of `(0.25, 0.75)` stays `(0.25, 0.75)`.

### The one thing that *is* converted, and why it is not an exception

glTF stores a matrix as 16 floats in **column-major order with the column-vector convention**
(`v' = M · v`). XNA's `Matrix` is **row-major with the row-vector convention** (`v' = v · M`). Those
are the numerical transpose of one another *for the same transform*, so `ConvertGltfMatrix` copies
basis vectors into the layout XNA reads them from:

```
glTF g[0..2]  (first basis vector)  → M11 M12 M13
glTF g[4..6]  (second basis vector) → M21 M22 M23
glTF g[8..10] (third basis vector)  → M31 M32 M33
glTF g[12..14] (translation)        → M41 M42 M43
```

This is a **storage-layout** conversion, not a coordinate-system one. Nothing about the transform's
meaning changes: the same point maps to the same place. It is listed here so that it is not mistaken
for evidence that the two coordinate systems differ — it is the only conversion in the importer, and
it is not one of those.

A **translation-only** matrix cannot tell a correct basis-vector copy from a naive full transpose,
because its rotation block is the identity and a transpose of the identity is the identity. Any test
of this conversion must therefore use a matrix with a **non-symmetric rotation block**, or it proves
nothing. `GltfConventions.ANodeMatrixWithRotationAndScaleSurvivesTheLayoutConversion` does.

## Transform order

Two multiplication orders are load-bearing, both in XNA's row-vector convention where the *left*
operand is applied first:

* **`Model::Draw`** binds `absoluteBoneTransform * world` — the bone's own transform first, then the
  world matrix the caller passed. Swapped, a model would be positioned by its bone hierarchy
  relative to the world rather than the other way round, which for a rig near the origin looks
  almost right and is not.
* **Scene-graph composition** is `local * parentWorld` — a node's own transform first, then its
  ancestry. `SceneNodeOut::worldTransform` is built that way, and `AnimationPlayer` reproduces it.

## The transform pipeline

`plan_gltf.md` `GLTF-132`. Where a node's authored transform goes, end to end, and which stage owns
each step. The architecture is `GLTF-103` Option A: **vertex positions stay mesh-local**, and a
node's placement travels as a `ModelBone` transform. Nothing is ever baked into a vertex buffer,
which is what lets one mesh be instanced by many nodes.

| Stage | Input | Output | Owner |
|---|---|---|---|
| Local transform | `node.matrix`, **or** its TRS | `SceneNodeOut::localTransform` | `BuildSceneGraph` |
| Composition | `local`, parent's world | `SceneNodeOut::worldTransform` | `BuildSceneGraph` |
| Placement | the graph, `node.mesh` | one `MeshInstanceOut` per placement | `CollectMeshGroups` |
| Model shape | the graph | one `ModelBone` per node, index-for-index | both loaders |
| Draw | bone tree, caller's `world` | `IEffectMatrices::World` | `Model::Draw` |
| Normals | the bound world | `uNormalMatrix` | the renderer |

### The equations

A node's local transform, per §3.5.3, with the components applied in this order:

```
local = S · R · T          (glTF, column-vector: scale first, then rotate, then translate)
local = S * R * T          (XNA, row-vector: identical order, read left to right)
```

`matrix` and TRS are mutually exclusive; when a malformed file authors both, `matrix` wins, and it
wins regardless of the order the two appear in the JSON.

Composition down the hierarchy, and the composed transform of a mesh placement:

```
world(node) = local(node) * world(parent)          world(scene root's parent) = identity
```

The synthetic `Root` bone at index 0 is an identity parent invented so a glTF scene's several roots
map onto `Model`'s single `Root`. It contributes nothing to any composition.

At draw time, again left-operand-first:

```
World bound for a part = absoluteBoneTransform(mesh.ParentBone) * callerWorld
uNormalMatrix          = transpose(inverse(World₃ₓ₃))
```

The normal matrix is the inverse transpose rather than the world 3×3 because a **non-uniform** scale
skews a normal that is merely multiplied by the world matrix. Under uniform scale the two agree,
which is exactly why the difference goes unnoticed until an asset with a non-uniform scale arrives.

### Defaults, and the values they are not

| Field | Omitted value | The wrong default, and what it does |
|---|---|---|
| `translation` | `(0, 0, 0)` | — |
| `rotation` | `(0, 0, 0, 1)` | `(0,0,0,0)` is not a rotation at all |
| `scale` | `(1, 1, 1)` | A zero-initialised scale collapses the node's whole subtree to a point |

### Mirroring

A node whose composed world transform has a **negative determinant** mirrors its geometry, and
§3.7.4 asks for the triangle winding to be reversed so its front faces stay front-facing. The
property belongs to the composed transform, never to the instancing node's own scale: an odd number
of mirroring ancestors mirrors, an even number does not.

CNA **detects** this (`MeshInstanceOut::mirroredEXT`) and reports it; it does not apply it. That is
not an omission but a consequence of Option A: one mesh instanced by both a mirrored and an
unmirrored node shares a single index buffer and a single vertex buffer, so reversing the winding —
or flipping `TANGENT.w`, which mirroring also inverts — at import would fix one placement by
breaking the other. Both are per-draw raster decisions, the same boundary `doubleSided` sits behind.

## `KHR_texture_transform`: baked, not shader-side

`plan_gltf.md` `GLTF-186`. The extension gives each texture reference its own offset/rotation/scale
on the UV set it samples. There are two places to apply it, and CNA **bakes it into the UV channel
at import**. That is a decision with a real cost, so it is recorded rather than left implicit.

**Why baking.** CNA's PBR effects sample every map from one shared UV channel (`GLTF-181`). A
shader-side transform needs a per-map transform *uniform* and a per-map UV *stream* to apply it to
— the second of which does not exist and would be a new vertex stride (`GLTF-182`). Baking needs
neither: the coordinates in the vertex buffer are already the transformed ones, and every renderer
draws the file correctly with no shader change at all. For the overwhelmingly common case — a
material whose maps all share one `texCoord` and one transform — baking is exactly equivalent and
free.

**What it costs.** Baking is destructive, and it loses precisely one case: two maps sharing a
`texCoord` with *different* transforms. One transform can be baked; the other map is then sampled
with the first one's coordinates. CNA bakes the **base colour's** transform and records every map
that wanted a different one in `MeshOut::unbakedTextureTransformsEXT`, which both loaders report by
name (`GLTF-184`). It is a wrong image, and it is a *named* wrong image.

**What would change the decision.** A second UV channel (`GLTF-182`) makes per-map transforms
expressible, at which point the transform belongs in the shader and this baking — and the report
that goes with it — should be removed rather than kept alongside. Until then, baking is the only
one of the two that renders anything at all.

## Where normals and tangents are renormalised

`plan_gltf.md` `GLTF-177`. §3.7.2.1 requires `NORMAL` and `TANGENT` to be unit length, but three
things downstream break that, and each is renormalised at a **stated** point rather than wherever
it happened to be convenient:

| Stage | What breaks unit length | Renormalised? |
|---|---|---|
| Import, authored normals | nothing — the file promises unit length | **No.** Passed through byte-exact |
| Import, generated normals (`GLTF-173`) | the area-weighted sum of face normals is not unit | **Yes**, at generation |
| Import, generated tangents | Gram-Schmidt against the normal | **Yes**, at generation |
| Morph blending (CPU, `SetMorphWeightsEXT`) | a weighted sum of unit normals is not unit | **Yes**, per vertex after blending |
| Skinning (GPU) | a weighted sum of rotated normals is not unit | **Yes**, in the vertex shader |
| Non-uniform node scale (GPU) | `transpose(inverse(W))` does not preserve length | **Yes**, in the vertex shader |
| Rasterizer interpolation (GPU) | interpolating two unit normals shortens the result | **Yes**, in the fragment shader |

Two consequences worth stating, because both look like bugs from the outside:

* **An authored non-unit normal survives import unchanged.** CNA does not "fix" it. The file is
  malformed, the vertex buffer says exactly what the file said, and the shader's own `normalize`
  makes it harmless at draw time. Renormalising at import would hide a broken export *and* make
  the vertex bytes disagree with the accessor they came from — which the L5 goldens compare.
* **The CPU morph path renormalises but the GPU one would not have to.** The blend happens on the
  CPU because the deformed buffer is re-uploaded (`SetDataRaw`), and the result is what every
  later reader sees — including a CPU-side bounds computation. Leaving it unnormalised would make
  a heavily morphed surface light darker as its normals shrink.

## The joint matrix, in both conventions

`plan_gltf.md` `GLTF-251`. §3.7.3.2's own equation, written once so no reader has to reconstruct it
from three call sites:

```
glTF (column-vector):  jointMatrix(j) = inverse(globalTransform(meshNode))
                                      · globalTransform(joint j)
                                      · inverseBindMatrix(j)

XNA  (row-vector):     jointMatrix(j) = inverseBindMatrix(j)
                                      * globalTransform(joint j)
                                      * inverse(globalTransform(meshNode))
```

The two are the same transform: XNA's row-vector convention applies the *left* operand first, so
the order is reversed term for term. `BuildSkeleton` produces the second form.

Three properties of that equation are load-bearing, and each was a defect when it was missing:

* **`globalTransform(joint)` is the joint's full scene ancestry**, not its position within the
  skin's joint list. A transform on an armature node *above* the joints is part of it. Dropping it
  while keeping the file's own `inverseBindMatrices` — which do include it — leaves every skinned
  vertex multiplied by the inverse of what was lost (`D8`, `GLTF-245`).
* **`inverse(globalTransform(meshNode))` cancels the skinned mesh node's own placement.** glTF
  requires a skinned mesh to be placed by its joints alone, so the term exists precisely to undo
  the node transform rather than to ignore it (`GLTF-247`).
* **`skin.skeleton` is a hint, never a traversal stop.** It names a convenient common root for the
  joints; walking only as far as it drops any ancestor above it (`GLTF-249`).

## Morphing happens on the CPU

`plan_gltf.md` `GLTF-285`. A morph target is a per-vertex delta array, and CNA applies the weighted
sum **on the CPU**, re-uploading the whole vertex buffer through `SetDataRaw` whenever the weights
change. The decision and its cost, recorded together:

* **Why.** It works on every renderer, unchanged — including the ones with no compute path and no
  vertex-texture fetch, which is most of the 46. A GPU implementation would need per-renderer
  shader work and a delta-texture or storage-buffer path, i.e. one implementation per backend.
* **What it costs.** One full vertex-buffer upload per weight change, per morphed primitive. For a
  face rig driven every frame that is the dominant cost of the feature, and it scales with the
  mesh rather than with the number of targets.
* **What it buys beyond portability.** The blended buffer is what *everything* sees, not just the
  draw — a CPU-side bounds computation, a picking ray, a `.cnj` re-export. A GPU blend would leave
  all of those reading the rest pose.
* **Status.** GPU morphing is explicitly **GLTF ROBUST**, not a gap in CORE conformance: the
  rendered result is identical, only the path differs.

## Which effect a primitive gets

`plan_gltf.md` `GLTF-215`/`GLTF-240`. The rule is the **material model the file declares**, never
which texture maps happen to be present — that earlier rule was `D7`, and it downgraded a
factor-only metallic-roughness material to an untextured white `BasicEffect`.

| The file says | Effect | Stride |
|---|---|---|
| metallic-roughness (including *no material at all*, and a material omitting `pbrMetallicRoughness`) | `PbrEffect` | 48 |
| …and the primitive is skinned | `SkinnedPbrEffect` | 68 |
| `KHR_materials_unlit` or `KHR_materials_pbrSpecularGlossiness` | `BasicEffect` | 32 |
| …and the primitive is skinned | `SkinnedEffect` | 52 |
| a non-PBR model with **both** a base-colour and an occlusion map | `DualTextureEffect` | 20 |
| any material, when the primitive carries `COLOR_0` | `BasicEffect`/`SkinnedEffect` | 24 / 56 |

Metallic-roughness is glTF's default in two separate ways, so most files take the first row.
`BasicEffect` and `SkinnedEffect` stay reachable for the content that genuinely is not PBR, and
`mat-unlit`/`skin-unlit` in the corpus are exactly that case — with byte-exact vertex goldens, so
"still reachable" is a tested property rather than an intention.

The last row is a documented downgrade rather than a choice: no CNA vertex layout carries a colour
alongside a tangent and no PBR shader reads a colour stream, so a vertex-coloured PBR material
keeps its colours and loses its material, and both loaders say so by name (`GLTF-241`).

## Lighting: what an imported model looks like with no lights

`plan_gltf.md` `GLTF-242`/`GLTF-243`. `PbrEffect` defaults to **zero ambient with every light
disabled**, which is correct XNA behaviour — XNA's stock effects light nothing until the
application says so — and it means a glTF file that declares no light imports perfectly and renders
**black**. That is not a bug and the defaults are deliberately unchanged; what would be a bug is
being unable to tell it apart from a broken import, so both loaders report how many lights
contributed, including when the answer is zero.

`KHR_lights_punctual` is imported as **up to three directional lights**, because that is XNA's
whole lighting model. A point or spot light becomes a directional one aimed at the scene origin, a
spot's cone is lost, a fourth light is dropped, and an out-of-gamut `color × intensity` is clamped.
Every one of those is counted and reported (`GLTF-326`).

### The IBL boundary, stated once

CNA has **no image-based lighting and no tone mapping**. glTF's own sample renderings use an
environment map and an ACES-style tone curve, so a CNA render of the same file will differ from
them in overall brightness and in every specular highlight — and that difference is **not a
conformance failure**. What conformance means here is the material's *parameters* reaching the
shader correctly (asserted at L6) and the BRDF's own terms matching Appendix B (`GLTF-235`); what
it does not mean is matching a renderer that solves a different lighting integral. A future IBL
pass would change the picture without changing anything this campaign asserts.

## Where this leaves `unitScale`

glTF §3.4 says one unit is one metre. XNA says nothing at all, so there is no conversion to apply at
import — the runtime path always uses `unitScale = 1`. The offline `gltf_to_cnj` tool exposes a
`unitScale` argument for content authored in another unit.

It scales **translations only** — vertex positions, bone bind poses and inverse bind matrices,
animated translation keys, and the node hierarchy's own local translations — never orientation and
never a node's authored scale, because a change of unit is not a change of shape. All of those have
to move together: a model whose vertices shrink by a hundred while its node offsets do not comes
apart, each part correctly sized and standing a hundred times too far from the next.
