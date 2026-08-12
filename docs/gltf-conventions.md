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

## Where this leaves `unitScale`

glTF §3.4 says one unit is one metre. XNA says nothing at all, so there is no conversion to apply at
import — the runtime path always uses `unitScale = 1`. The offline `gltf_to_cnj` tool exposes a
`unitScale` argument for content authored in another unit; it scales translations, not orientation,
and is the only place a length is reinterpreted.
