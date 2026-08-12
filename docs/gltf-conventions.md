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

## Where this leaves `unitScale`

glTF §3.4 says one unit is one metre. XNA says nothing at all, so there is no conversion to apply at
import — the runtime path always uses `unitScale = 1`. The offline `gltf_to_cnj` tool exposes a
`unitScale` argument for content authored in another unit.

It scales **translations only** — vertex positions, bone bind poses and inverse bind matrices,
animated translation keys, and the node hierarchy's own local translations — never orientation and
never a node's authored scale, because a change of unit is not a change of shape. All of those have
to move together: a model whose vertices shrink by a hundred while its node offsets do not comes
apart, each part correctly sized and standing a hundred times too far from the next.
