# glTF ↔ XNA conventions: what is *not* converted

`plans/plan_gltf.md` `GLTF-105`. This document exists to state, once and in one place, which conventions
glTF 2.0 and XNA 4.0 agree on and where their rasterizer-facing winding rules differ. Each is a
place where a well-meaning "fix" could silently break every asset in the project, and where the
absence of import-time conversion code looks like an omission rather than a decision.

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
| Vertex winding for a front face | Counter-clockwise | Clockwise under XNA's default `CullCounterClockwise` state | No index rewrite; a glTF-aware draw uses `CullClockwise` (and reverses it for a mirrored placement) |
| Distance unit | Metres (§3.4) | Undefined; whatever the game chooses | None at import; `unitScale` is the offline tool's own knob |

Because the coordinate conventions agree, the importer contains **no axis remap, no handedness
negation and no V flip anywhere**. A position authored as `(1, 2, 3)` arrives in the vertex buffer
as `(1, 2, 3)`; a normal of `(0, 0, 1)` stays `(0, 0, 1)`; a UV of `(0.25, 0.75)` stays
`(0.25, 0.75)`. Triangle indices also remain in the file's order, but the draw-time cull state must
account for the front-face difference described below.

### Front-face winding is draw state (`GLTF-423`)

glTF declares a counter-clockwise triangle front-facing. XNA's default
`RasterizerState::CullCounterClockwise`, as its name says, removes that winding and retains the
clockwise face. CNA deliberately does not reverse imported index buffers: culling is application
state, and one buffer can be shared by mirrored and unmirrored placements that need opposite
decisions.

The glTF viewer therefore uses `CullClockwise` for an ordinary single-sided placement,
`CullCounterClockwise` when its current world determinant is negative, and `CullNone` only for an
authored `doubleSided` material or the explicit `--no-cull` debug option. This is a format-to-XNA
draw-policy boundary, not an axis or handedness conversion. Applications that call `Model::Draw`
directly retain control of the same `RasterizerState` and must make the equivalent decision.

### Asset UVs are not render-target orientation state (`GLTF-192`, `GLTF-397`)

An OpenGL render target may store its native colour attachment bottom-up even though an uploaded
`Texture2D`, glTF and XNA all define UV `(0,0)` at the logical top-left. That is a resource-storage
difference, not permission to rewrite an asset's UVs. EasyGL corrects it at sampling time with one
per-bound-resource flag: `cnaSampleUV(uv, flip)` receives `flip=1` only for a bottom-up render-target
surface and `0` for an ordinary uploaded texture. Every flag is rebuilt and uploaded on every draw,
including the all-zero case, so sampling a render target cannot leave state that flips the next glTF
texture. PBR's base colour, normal, metallic-roughness, emissive and occlusion slots each have their
own flag; Vulkan and SOFTWARE need no native-storage correction but expose the same top-left public
result.

The separation is asserted from both sides. `GltfConventions.PositionsNormalsAndUvsPassThroughUnchanged`
locks asymmetric authored V values byte-for-byte, and `tex-reference-checkerboard` has distinct
colours/numerals in all four quadrants. The renderer-neutral
`rendertarget_sampling_orientation_test.cpp` then uses a non-square 8x4 pattern with every texel
unique and standard, deliberately **unflipped** mesh UVs. Its CD legs require an ordinary
`Texture2D` to sample upright and a `RenderTarget2D` containing the same logical bytes to match it
texel-for-texel through stock 3D effects; other legs cover SpriteBatch, target chains, explicit
public flips, MSAA, mipmaps, viewport and scissor without changing those mesh coordinates. Current
results are EasyGL **62/62**, Vulkan **58/58** (its unsupported 4x path is recorded explicitly) and
SOFTWARE **62/62**; HEADLESS passes the declared non-rasterising/refusal boundary.

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

`plans/plan_gltf.md` `GLTF-132`. Where a node's authored transform goes, end to end, and which stage owns
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
worldTangent.w         = localTangent.w * sign(det(World₃ₓ₃))
```

The normal matrix is the inverse transpose rather than the world 3×3 because a **non-uniform** scale
skews a normal that is merely multiplied by the world matrix. Under uniform scale the two agree,
which is exactly why the difference goes unnoticed until an asset with a non-uniform scale arrives.

Skinning applies the same distinction a second time. For a blended joint matrix `S`, the shader
uses these model-space quantities before applying the outer world transforms above:

```
position' = S * position
normal'   = normalize(transpose(inverse(S₃ₓ₃)) * normal)
tangent'  = S₃ₓ₃ * tangent
tangent.w'= tangent.w * sign(det(S₃ₓ₃))
```

Tangents are directions, then PBR re-orthogonalises them against `normal'` before constructing the
TBN basis. Normals are plane covectors: multiplying one directly by `S₃ₓ₃` is correct for rotation
or uniform scale, but `skin-nonuniform-joint-scale`'s `S=[1,2,1]` separates the two directions by
about 51 degrees. EasyGL evaluates the inverse transpose through cofactors so the GLSL ES 1.00
profiles do not require a matrix `inverse()` intrinsic; determinant sign is retained for mirrors,
and a nearly singular blend takes the established finite fallback instead of producing NaN.
EasyGL's PBR tangent path likewise uses a scalar triple product rather than a GLSL `determinant()`
intrinsic and combines the world, optional instance and blended-skin signs. A zero determinant keeps
the authored sign: the tangent frame is undefined there, but turning `w` into zero would erase it
for every later operation too.

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

CNA **detects** this (`MeshInstanceOut::mirroredEXT`) and reports it. It does not rewrite the shared
index or vertex buffer: one mesh instanced by both a mirrored and an unmirrored node would have one
placement fixed and the other broken. The two consequences then separate at draw time. Winding
reverses the ordinary glTF-aware cull choice described above and remains an application-owned
`RasterizerState` decision, the same boundary `doubleSided` sits behind. PBR tangent handedness is
shader-owned instead: EasyGL multiplies the unchanged local `TANGENT.w` by the direction
transform's determinant sign per placement (`GLTF-176`).

### Bounds follow the same placement

`plans/plan_gltf.md` `GLTF-128`. Each glTF loader initialises the ordinary XNA
`ModelMesh::BoundingSphere` in **mesh-local space**, from the positions of every primitive grouped
into that mesh. The direct and offline paths both use the vertex pose that is initially uploaded,
including authored non-zero `mesh.weights` or `node.weights`; the `.cnj` format needs no additional
bounds field because its existing vertex sidecars contain the same positions.

`Model::getBoundingSphereEXTProperty()` then follows the transform pipeline above rather than
re-reading those sidecars. A rigid mesh sphere is transformed by its **current absolute parent-bone
transform**. A mesh listed in `Model::SkinsEXT` is conservatively unioned under every matrix in its
current effect palette before that mesh placement, which includes glTF's mesh-node cancellation
and later `AnimationPlayer` updates. A rotated child below a non-uniformly scaled parent can make a
composed matrix sheared, so the radius uses a conservative upper bound on its largest stretch
rather than the longest-basis shortcut that is exact only for an orthogonal TRS basis. No meshes
means `std::nullopt`; a zero-radius sphere remains valid point geometry.

The result is in **model-root space**. For an imported glTF this is the composed scene space that
`SceneNodeOut::worldTransform` calls world, but it deliberately excludes the caller's application
`world` matrix passed to `Model::Draw`, which the model cannot know. A caller needing final
application-world bounds transforms the returned sphere by that same matrix.

This has the existing mesh-sphere deformation boundary, not a second hidden geometry tracker. The
skin bound is conservative over the complete active palette, so it may overbound a mesh that uses
only a few joints but cannot miss a linearly blended vertex solely because its palette moved. A
later morph re-upload still does not rewrite `ModelMesh::BoundingSphere`; if it moves vertices
outside the imported sphere, the application updates that read-write XNA property. The whole-model
accessor will use the updated value on its next call.

## `KHR_materials_variants`: source indices and complete part states

`plans/plan_gltf.md` `GLTF-341`/`GLTF-342`. CNA preserves the extension's root variant array in source
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

## `KHR_texture_transform`: per-map shader state

`plans/plan_gltf.md` `GLTF-184`/`GLTF-186`. The extension belongs to a texture reference, not to a vertex
stream. CNA therefore keeps authored UV bytes unchanged and carries five independent transforms in
base-colour, normal, metallic-roughness, emissive and occlusion order. Each map first selects one of
the two packed authored UV channels and then applies its own transform:

```
u' = cos(rotation) * scale.x * u - sin(rotation) * scale.y * v + offset.x
v' = sin(rotation) * scale.x * u + cos(rotation) * scale.y * v + offset.y
```

This is scale, then counter-clockwise rotation, then translation, exactly as the extension defines
it. `PbrEffect` and `SkinnedPbrEffect` retain the authored `TextureTransformEXT` values;
`FillGpuDrawParams` converts them to two padded affine rows per map. Direct glTF loading and the
optional 25-number `.cnj` field preserve the same state, with identity defaults for old content.
All PBR renderers consume the ten rows before applying any renderer-owned render-target V
orientation correction, which is not part of the asset transform.

The earlier importer baked one reference transform into a shared UV stream. That was the only
practical representation before the dual-UV strides and five map selectors existed, but it was
destructive: two maps sharing one authored set can deliberately transform it differently. The bake
and its `unbakedTextureTransformsEXT` warning were retired together, leaving one sampling path.
`texture-transform-per-map` locks the result at L6 and L7: at one authored coordinate its base map
must sample blue while its differently transformed normal map must sample a +Z quadrant.

## Texture mipmaps are role-aware or absent (`GLTF-206`)

glTF's four `*_MIPMAP_*` minification filters request a mip chain, but CNA decodes the PNG/JPEG
images used by core glTF into one-level `Texture2D` objects. CNA deliberately does **not** generate
one generic RGBA chain. The correct downsample depends on the map's meaning:

- base colour and emissive samples are sRGB-encoded and must be filtered in linear light;
- normal-map vectors must be decoded, filtered and renormalised;
- metallic-roughness and occlusion are linear data channels and must not receive the colour-map
  transfer function.

For now, every LOD therefore samples level zero. This is deterministic and preserves authored
values, at the cost of aliasing and reduced minification quality. It is also explicit:
`SamplerOut::minFilterRequiresMipChain` distinguishes an authored mip filter from an unspecified
default, and `MeshOut::mipmappedSamplerMapsWithoutMipChainEXT` names only affected maps the chosen
effect samples. Both load paths warn with those names. `NEAREST` and `LINEAR` request base level
only and do not warn; neither does an absent/undefined sampler, because glTF leaves that filtering
choice to the implementation.

## Where normals and tangents are renormalised

`plans/plan_gltf.md` `GLTF-177`. §3.7.2.1 requires `NORMAL` and `TANGENT` to be unit length, but three
things downstream break that, and each is renormalised at a **stated** point rather than wherever
it happened to be convenient:

| Stage | What breaks unit length | Renormalised? |
|---|---|---|
| Import, authored normals | nothing — the file promises unit length | **No.** Passed through byte-exact |
| Import, generated normals (`GLTF-173`/`GLTF-461`) | the face normal is the un-normalized cross product, twice the triangle's area | **Yes**, at generation |
| Morph blending, **recomputed** flat normals (`GLTF-461`) | the same cross product, from the morphed positions | **Yes**, per vertex after recomputation |
| Import, generated tangents | Gram-Schmidt against the normal | **Yes**, at generation |
| Morph blending, authored deltas (CPU, `SetMorphWeightsEXT`) | a weighted sum of unit normals is not unit | **Yes**, per vertex after blending |
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

`plans/plan_gltf.md` `GLTF-090`/`GLTF-091`/`GLTF-092`. Three places where a file authors more than an XNA
vertex layout can carry. None is an error, all three are reported, and each is reported at the
severity it deserves:

| What the file has | What CNA does | Reported as |
|---|---|---|
| `COLOR_0` in `FLOAT` or `UNSIGNED_SHORT` | quantised to `Color`'s 8 bits per channel, `round(clamp(f,0,1) × 255)` | nothing — this is the layout working as designed |
| `COLOR_1` and beyond | ignored; XNA carries one colour channel | a **warning**, with the count |
| `_ANYTHING` (application-specific), except the legacy basis below | ignored; §3.7.2.1 reserves the prefix so a reader may | a **debug** line, naming each |

The severities are the point. Quantisation is lossless at the endpoints and within half a
step everywhere else, and is what every XNA `Color` does — warning about it would be noise on every
coloured mesh. A second colour set is data that does not arrive: a mesh whose real tint lives in
`COLOR_1` imports looking like a mistake, and the warning is the only thing that traces it. A
custom attribute is *expected* to be ignored — a file whose own tooling reads `_BATCHID` still
imports as ordinary geometry — so it is named at debug level rather than warned about.

There is one deliberately narrow legacy exception. Some XNA-era exporters store an authored
tangent basis as paired `FLOAT VEC3` custom attributes named exactly `_TANGENT` and `_BINORMAL`.
When a primitive also authors `NORMAL` and does not carry glTF's standard `TANGENT VEC4`, CNA
preserves the pair: `_TANGENT` supplies xyz and CNA reconstructs w as
`sign(dot(cross(normal, tangent), binormal))`. The import report records
`legacy-tangent-basis-imported` as information. A standard `TANGENT` always wins; an incomplete or
wrongly typed legacy pair remains ordinary ignored custom data and is named in the debug report.

## Every primitive gets an index buffer

`plans/plan_gltf.md` `GLTF-070`. §3.7.2 makes `indices` optional: a primitive without it draws its
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

`plans/plan_gltf.md` `GLTF-251`. §3.7.3.2's own equation, written once so no reader has to reconstruct it
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

`plans/plan_gltf.md` `GLTF-252`. A joint participates in three index spaces that happen to coincide in
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

## A primitive with no `NORMAL` is split, not smoothed

`plans/plan_gltf.md` `GLTF-461`. §3.7.2.1 makes flat normals a **MUST**, and flat shading gives a vertex one
normal **per face** — so a vertex shared between differently oriented faces has no single correct
value and the only conforming answer is to duplicate it once per orientation.

* **Where the split happens.** At import, in `ExtractMesh`, **after** the topology conversion — a
  strip's adjacent triangles are exactly the shared-vertex case flat shading has to resolve, so
  splitting before the conversion would resolve the wrong connectivity.
* **How much it duplicates.** The minimum the geometry forces. Faces are grouped per vertex, so a
  mesh whose author already split its edges, and a mesh whose shared vertices lie on coplanar faces,
  keep their vertex count exactly. New indices are handed out in **source-vertex order**, so an
  unsplit primitive keeps its own numbering and its generated bytes are byte-for-byte unchanged.
* **What follows the split.** One remap (`GatherVertexStreamEXT`) renumbers every per-vertex stream:
  positions, both texture-coordinate sets, colours, joints, weights, the authored tangent and every
  morph delta. `MeshOut::vertexSourceEXT` carries the mapping, because the split is the one
  transformation in this importer that changes the vertex *numbering* and a consumer comparing CNA's
  stream against the file's own accessors needs it rather than a rule it has to re-derive.
* **Why a morphed primitive is split at every corner instead.** §3.7.2.2 requires flat normals for
  **each morph target**, and a `POSITION` delta can rotate a face — so two faces that are coplanar at
  rest need not stay coplanar, and a rest-pose split cannot serve every reachable pose. Only a fully
  split record can carry an exact per-face normal at any weight. The normals are then a function of
  the **weights**, so `BlendMorphTargetsEXT` recomputes them from the morphed positions on every
  weight change; nothing can be baked.
* **Why the authored tangent is thrown away.** The same sentence in §3.7.2.1 says so: an authored
  basis was built against normals the file then failed to supply, so keeping it pairs a tangent with a
  normal it is not orthogonal to. A basis is generated from the computed normals instead, and the
  discard is reported (`ignoredTangentForGeneratedNormalsEXT`).

## Morphing happens on the CPU

`plans/plan_gltf.md` `GLTF-285`. A morph target is a per-vertex delta array, and CNA applies the weighted
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

`plans/plan_gltf.md` `GLTF-215`/`GLTF-240`. The rule is the **material model the file declares**, never
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

`plans/plan_gltf.md` `GLTF-242`/`GLTF-243`. `PbrEffect` defaults to **zero ambient with every light
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

For `CUBICSPLINE`, that bake is exact only at the union keys: `AnimationClipEXT` and `.cnj` keep
complete T/R/S values but not the source interpolation mode or tangents, so playback uses lerp/slerp
between the baked keys. `anim-cubicspline` measures the loss rather than calling it negligible. Its
zero-tangent translation is `10(3t^2-2t^3)`; the baked keys at `0`, `.5`, `1` replay as `10t`. At
`t=.25` the source is `1.5625`, playback is `2.5`, and the absolute error is `0.9375` — **9.375% of
the 10-unit endpoint span**. The maximum on that fixture is `10sqrt(3)/18 = 0.962250449`, or
**9.622504%**, at `t=(3-sqrt(3))/6` and its reflection. This is deliberately accepted as the current
compatibility approximation and `resampledTrackCount` exposes when it can occur. It is not a global
error bound: arbitrary authored tangents can overshoot the endpoint span, so no finite percentage of
that span bounds every glTF spline. Preserving the exact curve would require an additive clip/`.cnj`
schema plus lazy runtime evaluation, as the morph path already carries separately.

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

## Clip space and depth stay renderer-owned (`GLTF-396`)

The importer never produces a clip-space position and never changes a projection for a particular
backend. It leaves vertices mesh-local, carries the same world transforms and effect parameters to
every renderer, and exposes a glTF camera through CNA's ordinary `Matrix::CreatePerspective*` or
`CreateOrthographic*` functions. Those XNA matrices use the Direct3D convention
`-w <= x,y <= w`, `0 <= z <= w`; the default viewport maps `z/w` to window depth `[0,1]`.

The raster backends do not all implement that input in the same native clip volume:

| Backend | Native/effective rule | Consequence for CNA/glTF draws |
|---|---|---|
| Vulkan | Native `0 <= z <= w`; stock vertex shaders flip clip Y once and leave Z unchanged. `VkViewport` applies `MinDepth..MaxDepth`. | Exact XNA depth convention. |
| EasyGL / OpenGL ES | Native `-w <= z <= w`; stock shaders pass the XNA projection's Z through. With the default GL depth range, XNA `z/w` in `[0,1]` occupies window depth `[0.5,1]`. | Visibility and depth ordering are unchanged because the mapping is monotonic, but only half of the available depth interval/precision is used. A custom shader emitting negative clip Z is OpenGL-specific and is outside the stock XNA effect contract. |
| SOFTWARE | Divides by W and applies `MinDepth + (z/w)*(MaxDepth-MinDepth)` directly. Its CPU polygon clip currently cuts only at `w > 1e-5` (the eye plane), then clips raster bounds; it does not implement every homogeneous X/Y/Z frustum plane. | XNA depth mapping is exact for in-frustum geometry. Geometry between the configured projection near plane and the eye, or beyond another homogeneous plane, is a documented CPU-renderer limitation rather than an import conversion. |
| HEADLESS / STUB | No rasterisation. | No clip/depth claim; capability/refusal tests are the oracle. |

This difference is harmless to the current corpus for a concrete reason, not because L7 was
assumed: all fixture positions remain object/world-space through L5, L6 compares identical stock
effect matrices, and the application/test camera supplies the same XNA projection on every
backend. Every current `*Gltf*` test is green on HEADLESS, OPENGLES3, Vulkan and SOFTWARE.
Renderer pixel controls separately prove the part that must agree: EasyGL and Vulkan each pass the
shared 39/39 viewport suite, including two `MinDepth/MaxDepth` depth-order checks; SOFTWARE passes
its 25/25 viewport/depth suite and the 4/4 public depth-state contract. None of those results makes
out-of-contract negative-Z custom clip coordinates portable.

Therefore a future image that differs because of clipping or window-depth mapping is owned by the
renderer. Adding a Z remap, near-plane cut or Y flip to glTF import would corrupt L1-L6 for every
other backend and violate the shared-defect rule below.

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
