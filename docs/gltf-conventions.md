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

### Bounds follow the same placement

`plan_gltf.md` `GLTF-128`. Each glTF loader initialises the ordinary XNA
`ModelMesh::BoundingSphere` in **mesh-local space**, from the positions of every primitive grouped
into that mesh. The direct and offline paths both use the vertex pose that is initially uploaded,
including authored non-zero `mesh.weights` or `node.weights`; the `.cnj` format needs no additional
bounds field because its existing vertex sidecars contain the same positions.

`Model::getBoundingSphereEXTProperty()` then follows the transform pipeline above rather than
re-reading those sidecars: it transforms each mesh sphere by the mesh's **current absolute parent
bone transform** and merges the placed spheres. The result therefore moves immediately with rigid
node animation. A rotated child below a non-uniformly scaled parent can make that composed matrix
sheared, so the radius uses a conservative upper bound on its largest stretch rather than the
longest-basis shortcut that is exact only for an orthogonal TRS basis. No meshes means
`std::nullopt`; a zero-radius sphere remains valid point geometry.

The result is in **model-root space**. For an imported glTF this is the composed scene space that
`SceneNodeOut::worldTransform` calls world, but it deliberately excludes the caller's application
`world` matrix passed to `Model::Draw`, which the model cannot know. A caller needing final
application-world bounds transforms the returned sphere by that same matrix.

This has the existing mesh-sphere deformation boundary, not a second hidden geometry tracker. GPU
skinning and a later morph re-upload do not rewrite `ModelMesh::BoundingSphere`; if either moves
vertices outside the imported default-pose sphere, the application updates that read-write XNA
property. The whole-model accessor will use the updated value on its next call.

## `KHR_materials_variants`: source indices and complete part states

`plan_gltf.md` `GLTF-341`/`GLTF-342`. CNA preserves the extension's root variant array in source
order and uses its integer position as identity. Display names are exposed for UI, but they are not
keys: the specification does not require them to be unique. A freshly loaded model always reports
selection `-1`, meaning every primitive's core `material`; importing the extension never changes
the default rendering implicitly.

Selection is model-wide and sparse. For each part CNA first restores its captured default, then
uses an override only when that primitive maps the selected variant. This reset-first rule matters
when switching from a mapped variant to one absent on that primitive: retaining the previous state
would violate the extension while looking plausible. `-1` performs the same explicit restoration.

An alternative is a **complete material-dependent part state**, not an effect parameter patch.
Changing material may select another effect class or vertex layout and may alter textures, sampler
states and the morph buffer's packed stride. CNA therefore extracts each mapped material through
the ordinary full primitive path and swaps the resulting effect, vertex buffer/count, tag and all
sampler slots together. Index data, topology, placement and bounds do not depend on material and
remain shared.

The `.cnj` writer uses the same rule rather than inventing a smaller variant-material schema: every
mapped alternative is a complete mesh-state record linked to the preceding default record, while a
single root name table preserves identity. The reader captures linked records as alternatives and
does not expose them as additional `ModelMeshPart`s. Thus direct glTF and offline conversion have
one selection contract, including mappings that cross PBR and unlit layouts.

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

## Attributes CNA has nowhere to put

`plan_gltf.md` `GLTF-090`/`GLTF-091`/`GLTF-092`. Three places where a file authors more than an XNA
vertex layout can carry. None is an error, all three are reported, and each is reported at the
severity it deserves:

| What the file has | What CNA does | Reported as |
|---|---|---|
| `COLOR_0` in `FLOAT` or `UNSIGNED_SHORT` | quantised to `Color`'s 8 bits per channel, `round(clamp(f,0,1) × 255)` | nothing — this is the layout working as designed |
| `COLOR_1` and beyond | ignored; XNA carries one colour channel | a **warning**, with the count |
| `_ANYTHING` (application-specific) | ignored; §3.7.2.1 reserves the prefix so a reader may | a **debug** line, naming each |

The severities are the point. Quantisation is lossless at the endpoints and within half a
step everywhere else, and is what every XNA `Color` does — warning about it would be noise on every
coloured mesh. A second colour set is data that does not arrive: a mesh whose real tint lives in
`COLOR_1` imports looking like a mistake, and the warning is the only thing that traces it. A
custom attribute is *expected* to be ignored — a file whose own tooling reads `_BATCHID` still
imports as ordinary geometry — so it is named at debug level rather than warned about.

## Every primitive gets an index buffer

`plan_gltf.md` `GLTF-070`. §3.7.2 makes `indices` optional: a primitive without it draws its
vertices in order. CNA **always materialises one** anyway — `0, 1, 2, …` for a non-indexed
primitive — and that is a decision rather than an oversight.

It keeps one draw path instead of two. Every renderer's draw, every `ModelMeshPart`, the `.cnj`
format and the L5 goldens would otherwise each need a non-indexed variant, and each of those is a
place the two paths can drift. The cost is the index buffer's own bytes for a primitive that did
not need one, which is 2 bytes per vertex against the 32 to 68 the vertex itself takes.

The one thing this must never become is an *empty* index buffer: an empty one draws nothing, so a
model would vanish rather than error. `GltfIndexForm.AnIndexlessPrimitiveIsMaterialisedAsASequentialIndexBuffer`
asserts the generated run is `0,1,2`, not absent.

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

### Scene nodes and palette slots are different identities

`plan_gltf.md` `GLTF-252`. A joint participates in three index spaces that happen to coincide in
simple files and must therefore never be inferred from one another:

| Space | Meaning | Consumer |
|---|---|---|
| `skin.joints[]` index | authored file-local joint reference | decoded `JOINTS_0` before import policy |
| `sceneNodeIndex` | stable identity in `SceneGraphOut` and `Model::Bones` | hierarchy, rigid animation, cameras and lights |
| `paletteIndex` | parent-before-child slot in this skin's GPU palette | `SkinningData`, `BlendIndices`, `uBones[]` |

`SkeletonResult::oldToNew` converts the first to the third. The scene-aware overload additionally
records `sceneNodeIndexToPaletteIndex` and `paletteIndexToSceneNodeIndex`; non-joint scene nodes and
joints outside the imported default scene map to `-1`. These are internal carrier fields, not a new
public API. `skin-parented-joints` deliberately authors the child before its parent and
`GltfSkinSpaces.SceneNodeAndPaletteMappingsAreExplicitAndBidirectional` proves that the scene graph
keeps its stable order, the palette becomes topological, both maps invert one another, and all four
`BlendIndices` bytes address the palette.

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
* **What it buys beyond portability.** The blended buffer is what every consumer that reads the
  buffer sees, not just the draw — a CPU-side geometry scan, a picking ray, a `.cnj` re-export. A
  GPU blend would leave all of those reading the rest pose. The stored `ModelMesh::BoundingSphere`
  is deliberately not such a scan and must be updated by the caller after a later morph, as the
  bounds section above records.
* **Status.** GPU morphing is explicitly **GLTF ROBUST**, not a gap in CORE conformance: the
  rendered result is identical, only the path differs.

## Which effect a primitive gets

`plan_gltf.md` `GLTF-215`/`GLTF-240`. The rule is the **material model the file declares**, never
which texture maps happen to be present — that earlier rule was `D7`, and it downgraded a
factor-only metallic-roughness material to an untextured white `BasicEffect`.

| The file says | Effect | Stride |
|---|---|---|
| metallic-roughness (including *no material at all*, a material omitting `pbrMetallicRoughness`, and a converted `KHR_materials_pbrSpecularGlossiness`) | `PbrEffect` | 48 |
| …and the primitive is skinned | `SkinnedPbrEffect` | 68 |
| `KHR_materials_unlit` | `BasicEffect`, lighting off | 32 |
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

### `KHR_materials_unlit` is a mapping, not an approximation

`GLTF-337`/`GLTF-338`. The extension means "shade this surface with its base colour and nothing
else", and `BasicEffect::LightingEnabled = false` is exactly that — one of the few glTF extensions
CNA implements rather than approximates. Three things travel with the flag:

* **`baseColorFactor` becomes the diffuse colour.** On the non-PBR path nothing else reads it, so
  without this an unlit material would import unlit *and white* — which is the effect's own
  default, and therefore indistinguishable from the extension having been ignored.
* **The punctual-light rig is skipped.** Every path through it ends with lighting on: the
  no-lights fallback calls `EnableDefaultLighting()`, and the normal path enables light slots. So
  applying it after setting the flag silently undoes it, and the file renders lit by XNA's default
  three-light rig with nothing to show for it.
* **The three light slots are parked, not left unset.** Their directions are uploaded whether or
  not the lights contribute, and the effect's constructed default is the zero vector — which a
  shader that normalises defensively turns into NaN. Zero colour makes the term a no-op; a unit
  direction makes it a safe one.

`unlitEXT` is its own flag rather than "not PBR", because the two mean different things: a
vertex-coloured metallic-roughness primitive is also non-PBR (`GLTF-241`) and must still be **lit**.

The one named limit is skinned: `SkinnedEffect`'s shader is lit by construction and has no
`LightingEnabled` — real XNA's has none either. The nearest expressible thing is no directional
light plus a white ambient, so `diffuse × ambient` is `diffuse`. That is unlit apart from any
specular term the material asks for, which is why the registry classifies the extension
`IMPLEMENTED_WITH_A_NAMED_LIMIT` rather than outright implemented.

### `KHR_materials_pbrSpecularGlossiness` is converted, not refused

`GLTF-349`. Khronos archived the extension, but it is what a decade of older assets are authored
in, so refusing it would reject content that is otherwise perfectly importable. It is converted to
metallic-roughness with the standard mapping:

| specular-glossiness | metallic-roughness |
|---|---|
| `diffuseFactor` | `baseColorFactor` |
| `diffuseTexture` | the base-colour map |
| — | `metallicFactor` = 0 (the surface becomes a dielectric) |
| `glossinessFactor` | `roughnessFactor` = `1 − glossiness` |
| `specularFactor` | **dropped** |

The dropped row is the whole approximation. Specular-glossiness can express a **coloured** specular
reflection; metallic-roughness can only approach that by making the surface metal, which also tints
the diffuse — a *different* material, not a closer one. A dielectric material therefore converts
almost exactly, and a brass one goes grey. Both the fact and the magnitude of the loss are
reported, at a severity that tracks the magnitude.

Converting is not claiming: the extension stays unclaimed in the registry, so a file listing it in
`extensionsRequired` is still refused. It is asking for the one term the conversion discards.

### `EXT_meshopt_compression` is refused

`GLTF-351`. cgltf makes this extension look supported — it parses the compression block and
validates its metadata thoroughly, so both `cgltf_parse` and `cgltf_validate` succeed. What it does
not do is *decode*: that needs `meshopt_decodeVertexBuffer` and friends, supplied by the caller,
which CNA does not provide.

Without a decoder, `cgltf_buffer_view_data` falls through to `buffer->data + view->offset`, so
every accessor over a compressed view reads whatever bytes happen to be there. Not an error, not
empty — undefined geometry that renders mangled or invisible with nothing to say why. So the file
is refused at validation, and refused even when the extension is only *used* rather than
*required*: that is the one place the "an ignorable extension is a warning" rule does not apply,
because an ignorable extension is one whose absence leaves the file readable, and this one
relocates the geometry.

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
Every one of those is counted and reported (`GLTF-326`, `GLTF-327`).

### The approximation table

`GLTF-327`, `GLTF-330`. Every row is a property glTF carries and XNA's `DirectionalLight` does not.
None is an error, none is recoverable inside XNA's lighting model, and each is counted in
`LightReportEXT` so the size of the loss is visible without re-reading the file.

| glTF property | What CNA does with it | Why it matters |
|---|---|---|
| `type: "directional"` | Maps exactly. | The one kind XNA has. |
| `type: "point"` | Becomes a directional light aimed from the light's world position at the scene origin. | The lit side of an object is right only where the object *is* the origin; everything else is lit from a subtly wrong angle. |
| `type: "spot"` | Same, and the cone is discarded. | A focused beam becomes full-scene illumination — the largest single approximation here. |
| `range` | Ignored. | Bounds where a light reaches. A directional light has no falloff, so a lamp scoped to one room lights the whole scene, and the error **grows with distance** — smallest exactly where an author placing it is looking. |
| `innerConeAngle` / `outerConeAngle` | Ignored. | As above; they are the *shape* of the pool of light. |
| `intensity` | Multiplied into `color`, then clamped per channel to `[0,1]`. | See below. |
| 4th and later lights | Dropped, in node order. | `DirectionalLight0..2` is the whole budget. |

**On intensity specifically.** glTF's units are photometric and unbounded — **lux** for a
directional light, **candela** for point and spot — while `DirectionalLight::DiffuseColor` is a
colour in `[0,1]`. There is no scale factor that makes those the same quantity: one is a physical
measurement and the other is a shader input, and the conversion between them is an *exposure*
decision that belongs to a tone-mapping stage CNA does not have (see the IBL boundary below). So
`color × intensity` is clamped, and an ordinary authored value clamps: `683` is not an absurd
number, it is the lumens-per-watt constant, and it imports as plain white. The report carries the
worst pre-clamp channel, which is what tells an author how far out of gamut they were rather than
merely that something was clipped.

### If real point and spot lights were wanted

`GLTF-331`, scoped and **not implemented**. No CNA stock effect shader has a point or spot term, so
supporting them is a shader-and-effect change rather than an importer one, and the importer is not
where it should be hidden. The shape it would take:

1. A `PointLightEXT` / `SpotLightEXT` uniform block alongside the existing three directional slots,
   carrying position, colour, range and (for spot) the two cone angles.
2. The falloff and cone terms from the extension's own specification, in the shader — the
   inverse-square-with-range-window and the smooth cone interpolation are both stated there, so
   this is transcription rather than design.
3. An `EffectLightCollectionEXT`-style API so an application can bind them, since XNA's own
   `IEffectLights` names exactly three directional lights and cannot express any of this.
4. Every renderer's uniform layout updated in step, which is what makes this large: the light block
   is shared shader ABI, not a per-renderer detail.

Until that exists, the approximation above stands and is reported. What must **not** happen in the
meantime is an importer that quietly picks a "good enough" directional substitute without saying
so — which is precisely what `GLTF-326`/`GLTF-327` exist to prevent.

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

## Animation: which channels arrive, and what the rest report

An imported clip is a list of keyframes per bone, each holding translation, rotation and scale
together. glTF keys each of those on its own sampler, so the two shapes do not correspond
one-for-one and the reconciliation has consequences worth naming.

**Channels are baked onto the union of their key times.** A bone whose translation is keyed at
`t = 0, 2` and whose scale is keyed at `t = 1, 3` produces a track with four keys, each holding
both components — every source key exactly, and an interpolation between them. A component no
channel drives is filled from the node's **own rest pose**, not from identity, or a node authored
with a rotation and animated only in translation would snap upright the moment the clip started.

**Sampler input must be ascending, and the two ways of breaking that get different answers.** §3.11
requires strictly increasing input, and every reader here relies on it. A **decreasing** step is
refused with a named error: the curve doubles back, so a time inside the reversed span has two
authored values, and sorting instead would silently re-pair each time with a value the exporter did
not write — a broken file turned into a plausible-looking wrong animation. **Equal** adjacent times
are kept and counted: they are what an exporter writes for a hard cut, the bracket search's
zero-length span already yields the earlier sample, and refusing them would reject assets that play
correctly. What tolerance costs is that a track holds one keyframe per time, so the value in force
*at* the cut survives and the post-cut value does not — the jump arrives as a ramp.

**Two kinds of channel are skipped, and both are reported by count.** A channel whose target node is
not in the default scene drives a bone that was never imported (`animations` is a top-level array
scoped to nothing, so a channel may name any node in the file). A channel on the `weights` path has
nothing to drive either, because morph weights are applied on the CPU at import rather than per
frame. Neither is an error and neither is silent: an animation that plays its rotation and never
morphs reads as a broken morph target, not as an unimported channel.

**An animation that drives nothing imported yields no rigid clip**, because an `animations` key on a
model with no animated bone is its own kind of lie. A *skinned* clip is emitted even when trackless,
which is not an inconsistency: skinned clips are selected by name, so dropping one silently renames
every clip after it from the application's point of view.

---

## A shared defect is never fixed inside a renderer (`GLTF-392`)

**The rule.** If a glTF asset is wrong on a renderer, the fix goes where the decision was made. A
change inside `modules/renderers/` is a legitimate fix only for something that renderer genuinely
owns — a shader, a state mapping, a vertex-layout binding. Anything about *what the file means*
belongs to the importer, and a change there is the only kind that fixes every renderer at once.

**Why it needs a rule rather than good sense.** The opposite is cheap in the moment. The wrongness
is visible on one renderer, the renderer is open in front of you, and a two-line adjustment makes
the picture right. What has actually happened is worse than the original defect: the importer's
mistake is now *compensated* on one of N renderers, the other N−1 stay wrong, and the compensation
is invisible to L1–L6 of the oracle ladder — because none of those layers looks at a renderer at
all. The next person to fix the importer properly then breaks the renderer that was compensating.
`GLTF-428` exists precisely to hunt for accumulated compensations of this kind.

**Enforced, not merely asked for.** `GltfSharedDefectPolicy` checks the dependency direction, which
is the part a compiler can see: **no renderer source may include the glTF importer**, so a renderer
cannot make an import decision even if someone wants it to; and **the importer may include the
renderer contract (`Renderers/Common/`) and no renderer implementation**, so "correct" can never
quietly come to mean "correct on EasyGL". `scripts/gltf-renderer-parity.sh` is the other half: any
L1–L5 difference between two renderers fails, and its message names this rule.

## The vendored cgltf: never patched, always worked around (`GLTF-038`)

CNA parses glTF with a vendored copy of `cgltf` (`third_party/cgltf/cgltf.h`, single header, MIT).
Three faults in it are known and CNA lives with all three, which raises the obvious question: why
not simply fix the header?

**The rule, and it has no exceptions today.** The vendored header is kept **byte-identical to
upstream**. A fault found in it is fixed *on CNA's side of the call*, and the workaround is written
so it can be deleted when an upgrade retires it. `GltfVendoredCgltf.TheVendoredHeaderCarriesNoCnaEdits`
enforces the first half mechanically — no `CNA`, `plan_gltf` or `GLTF-nnn` marker may appear in the
file — because a patch nobody can find is worse than one nobody made.

**Why.** A patched vendored header is invisible at exactly the moment it matters. The next upgrade
is a file replacement; a local edit inside it is either silently lost (and the fault returns, now
with a test that no longer explains itself) or silently kept (and the upgrade is not the upgrade
anyone thought they made). Neither failure announces itself. A workaround in CNA's own code is in
CNA's own diff, its own tests and its own blame.

**What that costs, honestly.** The workarounds run per accessor rather than inside the reader, and
one of them — the sparse-override re-application — walks data cgltf already walked. That is real,
and it is a load-time cost on an import path, not a per-frame one.

**The three faults, and where each is answered:**

| Fault | CNA's answer | Task |
|---|---|---|
| Sparse accessor values are read at the **base accessor's stride** rather than tightly packed, contradicting cgltf's own validator | `ApplySparseOverridesTightly` re-applies every sparse override after `cgltf_accessor_unpack_floats` returns | `GLTF-062` |
| §3.6.2.2's `max(c/N, −1)` clamp is omitted for signed normalized components, so `−128` decodes to `−1.0079` | `ClampNormalizedSigned` clamps after unpacking | `GLTF-056` |
| An accessor whose declared span **overflows `size_t`** passes `cgltf_validate`, because the span is computed with wrapping arithmetic | `ValidateGltfEXT` recomputes every span through `RequiredSpan`, and `UnpackAccessor` repeats the check where it allocates | `GLTF-039` |

**When the rule would change.** If a fault could not be answered outside the reader — a wrong value
with no CNA-side hook to correct it — the choice would be a patch *or* replacing the reader, and
that is a decision with its own review, not something to do quietly in a bug fix. `REMED-NA-016`
is the precedent for the current disposition: the misaligned `float` load UBSan found inside cgltf
was **not** patched there either. It turned out to be a malformed fixture CNA had accepted, and the
answer was to refuse such a file (§3.6.2.4) rather than to make the parser tolerate it.

**Upstream.** Report faults upstream; do not wait for them. The workaround lands with its own test
and stays until an upgrade proves it unnecessary — at which point the test is what tells you.

## What this file does not cover

Every decision here is one CNA *made*. What it **cannot** do — the approximations, the data no
vertex layout can carry, and the state that is carried but not yet applied — is
`docs/gltf-limitations.md`, and each entry there names the report field that tells a caller it
happened. The two are deliberately separate: a reader asking "why does CNA do it this way" and one
asking "what did my file lose" are asking different questions, and answering both in one document
is how a limitation ends up buried in a rationale.
