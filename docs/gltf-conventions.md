# glTF ↔ XNA conventions: what is *not* converted

`GLTF-105`. This document exists to state, once and in one place, which conventions
glTF 2.0 and XNA 4.0 agree on and where their rasterizer-facing winding rules differ. Each is a
place where a well-meaning "fix" could silently break every asset in the project, and where the
absence of import-time conversion code looks like an omission rather than a decision.

Reviewed against current source on 2026-11-01. The six tests in
`modules/content/tests/CNA/Internal/GltfImport/GltfConventionsTests.cpp` pin selected position,
normal, UV, quaternion, matrix and composition examples. Other behavior below has its own
importer/renderer tests. Those examples do not prove every sentence, malformed input or renderer
path; conventions, source policy and measured rendering evidence are separate claims.

## The agreements

| Convention | glTF 2.0 | XNA 4.0 | Conversion needed |
|---|---|---|---|
| Handedness | Right-handed | Right-handed | **None** |
| Up axis | +Y | +Y | **None** |
| Camera view direction | Local −Z | `Vector3::Forward` is −Z | **None** |
| Authored asset front | +Z | No universal asset-facing requirement | Preserve the file's orientation; do not confuse asset front with camera view direction |
| Texture coordinate origin | Top-left, V down | Top-left, V down | **None** |
| Quaternion component order | `[x, y, z, w]` | `Quaternion(X, Y, Z, W)` | **None** — a straight member copy |
| Vertex winding for a front face | Counter-clockwise | Clockwise under XNA's default `CullCounterClockwise` state | No index rewrite; a glTF-aware draw uses `CullClockwise` (and reverses it for a mirrored placement) |
| Distance unit | Metres (§3.4) | Undefined; whatever the game chooses | None at import; `unitScale` is the offline tool's own knob |

The [glTF coordinate/camera rules](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#coordinate-system-and-units)
distinguish a +Z-facing asset from a camera looking along −Z. With the normal unit scale, CNA
applies **no global axis remap, handedness negation or asset V flip**. A position authored as
`(1, 2, 3)` arrives in the vertex buffer
as `(1, 2, 3)`; a normal of `(0, 0, 1)` stays `(0, 0, 1)`; a UV of `(0.25, 0.75)` stays
`(0.25, 0.75)`. This does not promise unchanged vertex/index numbering through topology conversion
or generated-flat-normal splitting (§ below). Draw-time culling must account for the front-face
difference described next.

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
own flag; SOFTWARE needs no native-storage correction but exposes the same top-left public
result.

The separation is asserted from both sides. `GltfConventions.PositionsNormalsAndUvsPassThroughUnchanged`
checks asymmetric authored V values numerically, and `tex-reference-checkerboard` has distinct
colours/numerals in all four quadrants. The renderer-neutral
`rendertarget_sampling_orientation_test.cpp` then uses a non-square 8x4 pattern with every texel
unique and standard, deliberately **unflipped** mesh UVs. Its CD legs require an ordinary
`Texture2D` to sample upright and a `RenderTarget2D` containing the same logical bytes to match it
texel-for-texel through stock 3D effects; other legs cover SpriteBatch, target chains, explicit
public flips, MSAA, mipmaps, viewport and scissor without changing those mesh coordinates. Current
results and renderer-specific qualification belong in [the render-target guide](rendertarget-support.md)
and the relevant renderer page; an old case count here is not a current all-renderer pass.
HEADLESS has a non-rasterising boundary, not pixel equivalence.

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
for evidence that the two coordinate systems differ. It is the transform representation change;
other import operations still include attribute repacking, topology conversion and optional unit scaling.

A translation-only case checks translation placement but cannot distinguish a correctly copied
rotation basis from a transposed one if both implementations preserve translation separately.
The non-symmetric rotation/scale and translation witness in
`GltfConventions.ANodeMatrixWithRotationAndScaleSurvivesTheLayoutConversion` covers that gap.
This helper copies the affine 3×3/translation fields and writes the affine last column; it is not
a general projective-matrix conversion API.

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

`GLTF-132`. Where a node's authored transform goes, end to end, and which stage owns
each step. The architecture is `GLTF-103` Option A: **vertex positions stay mesh-local**, and a
node's placement travels as a `ModelBone` transform. Node placement is not baked into positions;
initial morph weights and an offline `unitScale` can still change the uploaded vertex bytes.
Multiple scene nodes can place one mesh without requiring a coordinate-system conversion.

| Stage | Input | Output | Owner |
|---|---|---|---|
| Local transform | `node.matrix`, **or** its TRS | `SceneNodeOut::localTransform` | `BuildSceneGraph` |
| Composition | `local`, parent's world | `SceneNodeOut::worldTransform` | `BuildSceneGraph` |
| Placement | the graph, `node.mesh` | one `MeshInstanceOut` per placement | `CollectMeshGroups` |
| Model shape | selected scene graph | one `ModelBone` per imported graph row, including the synthetic root | both loaders |
| Draw | bone tree, caller's `world` | `IEffectMatrices::World` | `Model::Draw` |
| Normals | the bound world | `uNormalMatrix` | the renderer |

### The equations

A node's local transform, per §3.5.3, with the components applied in this order:

```
local = T · R · S          (glTF column vectors: the rightmost operand acts first)
local = S * R * T          (XNA row vectors: the leftmost operand acts first)
```

Both apply scale, rotation, then translation. This follows the
[glTF node-transform definition](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#transformations);
the written multiplication order changes when switching vector conventions.

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

Skinning applies the same distinction a second time. In **shader column-vector notation**, for a
blended joint matrix `S`, these model-space quantities precede the outer world transform:

```
position' = S * position
normal'   = normalize(transpose(inverse(S₃ₓ₃)) * normal)
tangent'  = S₃ₓ₃ * tangent
tangent.w'= tangent.w * sign(det(S₃ₓ₃))
```

Tangents are directions, then PBR re-orthogonalises them against `normal'` before constructing the
TBN basis. Normals are plane covectors: multiplying one directly by `S₃ₓ₃` is correct for rotation
or uniform scale, but `skin-nonuniform-joint-scale`'s `S=[1,2,1]` separates the two directions by
about 51 degrees. EasyGL evaluates the inverse transpose through cofactors rather than a matrix
`inverse()` intrinsic; determinant sign is retained for mirrors,
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

`GLTF-128`. Each glTF loader initialises the ordinary XNA
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
only a few joints. The convex-union argument assumes valid nonnegative, unit-sum skin weights;
it is not a bound for every malformed influence set. A
later morph re-upload still does not rewrite `ModelMesh::BoundingSphere`; if it moves vertices
outside the imported sphere, the application updates that read-write XNA property. The whole-model
accessor will use the updated value on its next call.

## `KHR_materials_variants`: source indices and complete part states

`GLTF-341`/`GLTF-342`. CNA preserves the extension's root variant array in source
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

`GLTF-184`/`GLTF-186`. The extension belongs to a texture reference, not to a vertex
stream. CNA therefore keeps authored UV bytes unchanged. The five core maps carry independent
transforms in base-colour, normal, metallic-roughness, emissive and occlusion GPU-slot order;
specular-strength and specular-colour maps have two additional independent transforms. Each map selects one of
the two packed authored UV channels and then applies its own transform:

```
u' = cos(rotation) * scale.x * u - sin(rotation) * scale.y * v + offset.x
v' = sin(rotation) * scale.x * u + cos(rotation) * scale.y * v + offset.y
```

This is scale, then counter-clockwise rotation, then translation, exactly as the extension defines
it. `PbrEffect` and `SkinnedPbrEffect` retain the authored `TextureTransformEXT` values;
`FillGpuDrawParams` converts them to two padded affine rows per map. Direct glTF loading and the
optional 25-number `.cnj` field preserve the same state, with identity defaults for old content.
The common draw contract has ten core affine rows plus four specular rows. PBR renderers consume
the applicable state before renderer-owned render-target V correction. The core 25-number CNJ
field is not the complete seven-map material record; the two specular transforms have separate
fields. Renderer/source support does not automatically make every related extension claimed in
the registry; see [the limitations guide](gltf-limitations.md).

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

`GLTF-177`. §3.7.2.1 requires `NORMAL` and `TANGENT` to be unit length, but three
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
  malformed, and the vertex buffer says what the file said. Shader normalization is not general
  validation of zero, non-finite or otherwise malformed vectors. Renormalising at import would hide a broken export *and* make
  the vertex bytes disagree with the accessor they came from — which the L5 goldens compare.
* **CPU data and shader normalization serve different consumers.** The morph helper normalizes
  its blended normal/tangent data before upload, so CPU readers can inspect that deformed stream.
  Skinning, nonuniform world transforms and raster interpolation still require their own
  normalization later. None of this automatically refreshes stored mesh bounds.

## Attributes CNA has nowhere to put

`GLTF-090`/`GLTF-091`/`GLTF-092`. Three places where a file authors more than an XNA
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

`GLTF-070`. §3.7.2 makes `indices` optional: a primitive without it draws its
vertices in order. CNA **always materialises one** anyway — `0, 1, 2, …` for a non-indexed
primitive — and that is a decision rather than an oversight.

It keeps one draw path instead of two. Every renderer's draw, every `ModelMeshPart`, the `.cnj`
format and the L5 goldens would otherwise each need a non-indexed variant, and each of those is a
place the two paths can drift. This adds 2- or 4-byte indices according to the importer's width
selection, plus topology/splitting costs. Current native layouts span 24–80 bytes per vertex;
there is no universal two-byte-per-vertex overhead for every imported primitive.

The one thing this must never become is an *empty* index buffer: an empty one draws nothing, so a
model would vanish rather than error. `GltfIndexForm.AnIndexlessPrimitiveIsMaterialisedAsASequentialIndexBuffer`
asserts the generated run is `0,1,2`, not absent.

## The joint matrix, in both conventions

`GLTF-251`. The joint matrix, written once so no reader has to reconstruct it
from three call sites:

```
glTF (column-vector):  jointMatrix(j) = globalTransform(joint j) · inverseBindMatrix(j)

XNA  (row-vector):     jointMatrix(j) = inverseBindMatrix(j) * globalTransform(joint j)
```

The two are the same transform: XNA's row-vector convention applies the *left* operand first, so
the order is reversed term for term. `BuildSkeleton` produces the second form.

Three properties of that equation are load-bearing, and each was a defect when it was wrong:

* **`globalTransform(joint)` is the joint's full scene ancestry**, not its position within the
  skin's joint list. A transform on an armature node *above* the joints is part of it. Dropping it
  while keeping the file's own `inverseBindMatrices` — which do include it — leaves every skinned
  vertex multiplied by the inverse of what was lost (`D8`, `GLTF-245`).
* **The skinned mesh node's transform, ancestors included, is not part of it.** §3.7.3.2: "the
  transform of the skinned mesh node MUST be ignored". The glTF tutorial writes an extra
  `inverse(globalTransform(meshNode))` in front; that form is for a renderer that also draws the
  mesh with its node's transform, so the two cancel. CNA draws a skinned mesh from the identity
  root bone and applies neither. Until cna-street's `CNASTREET-SKINDRAW` it applied the inverse
  alone (`GLTF-247`, withdrawn), which moved every skinned mesh by the inverse of its node --
  CesiumMan, whose mesh node sits under a Z-up armature, drew lying on its side.
* **`skin.skeleton` is a hint, never a traversal stop.** It names a convenient common root for the
  joints; walking only as far as it drops any ancestor above it (`GLTF-249`).

### Scene nodes and palette slots are different identities

`GLTF-252`. A joint participates in three index spaces that happen to coincide in
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

`GLTF-461`. §3.7.2.1 makes flat normals a **MUST**, and flat shading gives a vertex one
normal **per face** — so a vertex shared between differently oriented faces has no single correct
value and the only conforming answer is to duplicate it once per orientation.

* **Where the split happens.** At import, in `ExtractMesh`, **after** the topology conversion — a
  strip's adjacent triangles are exactly the shared-vertex case flat shading has to resolve, so
  splitting before the conversion would resolve the wrong connectivity.
* **How much it duplicates.** Faces are grouped per source vertex by generated-normal direction.
  The comparison has a floating-point reproducibility tolerance (about 0.081 degrees), so this is
  not an exact minimum split by mathematical face orientation: nearly coplanar faces can share an
  area-weighted normal and non-identical merges are counted. New indices follow source-vertex
  order; `vertexSourceEXT` is the authoritative remap rather than an assumption that numbering
  always stays unchanged.
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

`GLTF-285`. A morph target is a per-vertex delta array, and CNA applies the weighted
sum **on the CPU**, re-uploading the whole vertex buffer through `SetDataRaw` on each helper call.
The decision and its cost, recorded together:

* **Why.** The same CPU blend feeds existing graphics renderers without a compute/delta-buffer
  shader path. This is a shared CPU implementation rather than a separate morph shader per renderer.
* **What it costs.** Every `SetMorphWeightsEXT` call blends and uploads the full vertex buffer;
  it does not memoize identical weights. CPU work depends on vertex count and active target
  streams, plus any normal recomputation, while upload bytes depend on vertex count and stride.
  Which phase dominates needs measurement on the actual asset/renderer.
* **What it buys beyond portability.** The blended buffer is what every consumer that reads the
  buffer sees, not just the draw — a CPU-side geometry scan, a picking ray, a `.cnj` re-export. A
  GPU blend would leave all of those reading the rest pose. The stored `ModelMesh::BoundingSphere`
  is deliberately not such a scan and must be updated by the caller after a later morph, as the
  bounds section above records.
* **Status.** CPU morphing is the implemented path. It does not automatically advance a retained
  weight track: applications can call `EvaluateMorphWeightsEXT` and then `SetMorphWeightsEXT`.
  No unimplemented GPU path has been measured as byte/pixel identical.

## Which effect a primitive gets

`GLTF-215`/`GLTF-240`. The rule is the **material model the file declares**, never
which texture maps happen to be present — that earlier rule was `D7`, and it downgraded a
factor-only metallic-roughness material to an untextured white `BasicEffect`.

| The file says | Effect | Stride |
|---|---|---|
| metallic-roughness (including no material, an omitted `pbrMetallicRoughness`, or the supported specular-glossiness conversion) | `PbrEffect` | 48, or 60 with a second packed UV channel or `COLOR_0` |
| …and the primitive is skinned | `SkinnedPbrEffect` | 68, 76 with a second UV channel, 80 with `COLOR_0` |
| `KHR_materials_unlit`, no colour | `BasicEffect`, lighting off | 32 |
| …and skinned | `SkinnedEffect`, white-ambient/no-directional policy | 52 |
| `KHR_materials_unlit` with `COLOR_0` | `BasicEffect` / `SkinnedEffect` | 24 / 56 |
| unskinned, uncoloured non-PBR material with base-colour and occlusion images | `DualTextureEffect` | 28; see the unlit occlusion defect below |

Metallic-roughness is glTF's default in two separate ways, so most files take the first row.
`BasicEffect` and `SkinnedEffect` stay reachable for the content that genuinely is not PBR, and
`mat-unlit`/`skin-unlit` in the corpus are exactly that case — with byte-exact vertex goldens, so
"still reachable" is a tested property rather than an intention.

`GLTF-462`/`GLTF-463` removed the old vertex-colour downgrade: rigid stride 60 and skinned stride
80 retain colour, normals, tangent basis and PBR material together. Colour is still packed into
8-bit channels; it no longer forces BasicEffect/SkinnedEffect selection. The canonical rule table
is `VertexLayoutTableEXT()`, rather than an inferred meaning for a stride in a consumer.

### `KHR_materials_unlit` is a mapping, not an approximation

`GLTF-337`/`GLTF-338`. The extension means "shade this surface with its base colour and nothing
else", and `BasicEffect::LightingEnabled = false` is exactly that — one of the few glTF extensions
CNA implements rather than approximates. Three things travel with the flag:

* **`baseColorFactor` becomes the diffuse colour.** On the non-PBR path nothing else reads it, so
  without this an unlit material would import unlit *and white* — which is the effect's own
  default, and therefore indistinguishable from the extension having been ignored.
* **The punctual-light rig is skipped.** Its empty-list fallback calls
  `EnableDefaultLighting()` and its nonempty path changes/enables directional slots. Neither
  belongs after an explicit unlit configuration; the fallback can turn lighting back on.
* **The three light slots are parked, not left unset.** Their directions are uploaded whether or
  not the lights contribute, and the effect's constructed default is the zero vector — which a
  shader with an unguarded normalize can turn into NaN. Zero colour makes the term a no-op; a unit
  direction makes it a safe one.

`unlitEXT` records authored material intent; `COLOR_0` on a metallic-roughness material no longer
changes that material into non-PBR. The historical `GLTF-241` rationale is not current selection behavior.

SkinnedEffect cannot switch to an unlit shader through `IEffectLights`: its false lighting setter
refuses. CNA parks all directional lights and supplies white ambient. The registry retains
`IMPLEMENTED_WITH_A_NAMED_LIMIT`; this configuration is not evidence of every renderer's exact
unlit parity. A separate current defect is the legacy DualTexture selection: an unskinned,
uncoloured unlit material with base-colour and occlusion images can sample that occlusion texture,
although [KHR_materials_unlit](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_unlit)
requires the base-colour term without lighting-model inputs. See handoff §5.1; the guide must not
present that branch as an exact mapping.

### `KHR_materials_pbrSpecularGlossiness` is converted, not refused

`GLTF-349`. Khronos archived the extension, but it is what a decade of older assets are authored
in, so refusing it would reject content that is otherwise perfectly importable. It is converted to
metallic-roughness with CNA's deliberately lossy compatibility mapping:

| specular-glossiness | metallic-roughness |
|---|---|
| `diffuseFactor` | `baseColorFactor` |
| `diffuseTexture` | the base-colour map |
| — | `metallicFactor` = 0 (the surface becomes a dielectric) |
| `glossinessFactor` | `roughnessFactor` = `1 − glossiness` |
| `specularFactor` | **dropped** |

This is not a general specular-glossiness-to-metallic-roughness equivalence algorithm: CNA fixes
metallic to zero and drops the source specular factor. Both coloured and achromatic reflections
can change, even on a dielectric. Native PBR specular extension inputs elsewhere in CNA do not
make this legacy conversion exact. The report records the conversion and the maximum dropped
source specular component; that scalar is not a measured pixel-error or perceptual-loss bound.

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

`PbrEffect` itself starts with zero ambient and disabled directional lights. Imported lit glTF
materials do not simply retain those defaults: `ApplyPunctualLightsEXT` calls
`EnableDefaultLighting()` when its imported light list is empty. When present, up to three
imported directional approximations are applied instead. Explicitly unlit materials skip that
rig, and effects without `IEffectLights` cannot receive it. A zero imported-light count therefore
does not mean that the loaded model necessarily renders black. PBR is a CNA extension; its
defaults are not a blanket statement about every XNA stock effect.

`KHR_lights_punctual` is imported as **up to three directional lights**, the existing import policy.
A point or spot light becomes a directional one aimed at the scene origin, a
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
measurement and the other is this import policy's bounded shader input. CNA does not derive a
physical exposure for this conversion. `color × intensity` is clamped; for example, white at
intensity 683 still becomes white at unit strength. The report carries the
worst pre-clamp channel, which is what tells an author how far out of gamut they were rather than
merely that something was clipped.

### Native point/spot support and the import boundary

CNA already has the native `PunctualLightEXT` contract and effect setters such as
`PbrEffect::setPunctualLightEXT` and its skinned sibling, including point/spot terms and range/cone
state. This is CNAEXT functionality, separate from XNA's three-directional-light interface.
The glTF importer still returns its documented directional approximations; it does not
transparently bind an arbitrary authored punctual-light scene to those native effect terms.
Do not confuse an import-policy limitation with absence of a runtime lighting API, or assume that
an application-bound native light proves automatic glTF light parity.

### The IBL boundary, stated once

CNA has native image-based lighting: `ImageBasedLightEXT` can be supplied to PbrEffect and
SkinnedPbrEffect, and `GraphicsDevice::SupportsImageBasedLightingEXT()` reports the renderer
boundary. A valid environment replaces the flat ambient term; it is not added to it.
Loading a glTF does not create an environment or install an exposure/tone-mapping pipeline.
Applications and comparison viewers must choose these policies explicitly. Output sRGB encoding
is a transfer function, not tone mapping. L6 checks material/draw parameters and analytic BRDF
witnesses check selected terms; neither proves pixel agreement with a viewer using a different
lighting integral, environment or display transform.

## Where this leaves `unitScale`

glTF §3.4 says one unit is one metre. XNA says nothing at all, so there is no conversion to apply at
import — the runtime path always uses `unitScale = 1`. The offline `gltf_to_cnj` tool exposes a
`unitScale` argument for content authored in another unit.

It scales **length-valued coordinates** — vertex positions and morph position deltas, matrix
translation fields in bind poses/inverse binds/root prefixes, animated translation keys and the
node hierarchy's local translations — never orientation and
never a node's authored scale, because a change of unit is not a change of shape. All of those have
to move together: a model whose vertices shrink by a hundred while its node offsets do not comes
apart, each part correctly sized and standing a hundred times too far from the next.

Use a finite positive multiplier. The standalone `gltf_to_cnj` CLI currently uses `std::stof`
without checking full-token consumption or finiteness: `1trailing` and `inf` both reach export
success on a small isolated fixture. This parser defect is recorded in handoff §5.1; positive-only
comparison is not sufficient numeric validation. It is separate from the intended unit conversion.

## Animation: which channels arrive, and what the rest report

An imported clip is a list of keyframes per bone, each holding translation, rotation and scale
together. glTF keys each of those on its own sampler, so the two shapes do not correspond
one-for-one and the reconciliation has consequences worth naming.

**Channels are baked onto the union of their key times.** A bone whose translation is keyed at
`t = 0, 2` and whose scale is keyed at `t = 1, 3` produces a track with four keys, each holding
both components — every source key exactly, and an interpolation between them. A component no
channel drives is filled from the node's **own rest pose**, not from identity, or a node authored
with a rotation and animated only in translation would snap upright the moment the clip started.

`AnimationClipEXT` and `.cnj` keep complete T/R/S values but not the source interpolation mode or
tangents, and playback uses lerp/slerp between keys. A `STEP` or `CUBICSPLINE` channel is therefore
kept by its samples (CNA-DEMOS-ISSUE-031): a `STEP` channel gets a hold key 10 µs before each of
its keys, so the earlier value holds until the step; a `CUBICSPLINE` interval is sampled at least
every 1/60 s (four samples at the least), each evaluated on the source spline, so playback follows
the curve within the error of linear interpolation between those samples. On `anim-cubicspline`,
the zero-tangent translation `10(3t^2-2t^3)`, baking only the union keys `0`, `.5`, `1` would
replay as `10t`, 0.9375 units off at `t=.25` and at most `10sqrt(3)/18 = 0.962250449` off; with the
spline samples playback is exact at `t=.25` and within 0.0021 everywhere. That bound is the
fixture's (`|f''| <= 60`), not a global one: arbitrary authored tangents can bend a spline more
sharply. Exact preservation would still need an additive clip/`.cnj` schema plus lazy evaluation,
as the morph path carries separately. `resampledTrackCount` counts tracks resampled onto a union of
disagreeing channel times; the interpolation samples are not counted.

**Sampler input must be ascending, and the two ways of breaking that get different answers.** §3.11
requires strictly increasing input, and every reader here relies on it. A **decreasing** step is
refused with a named error: the curve doubles back, so a time inside the reversed span has two
authored values, and sorting instead would silently re-pair each time with a value the exporter did
not write — a broken file turned into a plausible-looking wrong animation. **Equal** adjacent times
are kept and counted: they are what an exporter writes for a hard cut, the bracket search's
zero-length span already yields the earlier sample, and refusing them would reject assets that play
correctly. What tolerance costs is that a track holds one keyframe per time, so the value in force
*at* the cut survives and the post-cut value does not — the jump arrives as a ramp.

**TRS and morph-weight channels have separate carriers.** A TRS channel whose target has no row
in the selected scene-node or skin-palette mapping is counted as skipped by that extraction route.
Weights do not become BoneTrackEXT T/R/S keys. The model loader separately retains one selected
weight channel per mesh in `MorphWeightTrackEXT`, including STEP/CUBICSPLINE mode and Hermite
tangents; applications evaluate/apply it through the CPU morph helpers. Currently
`ExtractMorphWeightTrack` finds the first file node referencing the mesh and the first matching
weight channel while traversing animations. It is not a general per-instance, named-animation
weight-clip collection. The loader reconciles retained channels with diagnostic counts so a
retained morph track is not automatically reported as dropped. No automatic per-frame morph
advance follows merely from loading a Model.

**An animation that drives nothing imported yields no rigid clip**, because an `animations` key on a
model with no animated bone is its own kind of lie. A *skinned* clip is emitted even when trackless,
which is not an inconsistency: skinned clips are selected by name, so dropping one silently renames
every clip after it from the application's point of view.

---

## Clip space and depth stay renderer-owned (`GLTF-396`)

The importer never produces a clip-space position and never changes a projection for a particular
renderer. It leaves vertices mesh-local, carries the same world transforms and effect parameters to
every renderer, and exposes a glTF camera through CNA's ordinary `Matrix::CreatePerspective*` or
`CreateOrthographic*` functions. Those XNA matrices use the Direct3D convention
`-w <= x,y <= w`, `0 <= z <= w`; the default viewport maps `z/w` to window depth `[0,1]`.

The graphics renderers do not all implement that input in the same native clip volume:

| Renderer | Native/effective rule | Consequence for CNA/glTF draws |
|---|---|---|
| EasyGL / OpenGL ES | Native `-w <= z <= w`; every renderer-owned 3D vertex shader converts XNA/D3D depth with `z'=2z-w`, exactly as MojoShader does for compiled Effects. | Exact XNA near/far clipping and full `MinDepth..MaxDepth` window-depth mapping. |
| SOFTWARE | Clips every primitive against the six homogeneous XNA/D3D planes, divides by W, and applies `MinDepth + (z/w)*(MaxDepth-MinDepth)` directly. | Exact XNA clip-volume and depth-range convention. |
| HEADLESS / STUB | No rasterisation. | No clip/depth claim; capability/refusal tests are the oracle. |

The table describes the EasyGL/SOFTWARE mechanisms, not the complete renderer registry.
SDLGPU, WEBGPU, FNA3D and DIRECTX9 keep their native clip/orientation adaptations in their own
renderer/effect paths; see their maintained renderer guides. Import data and XNA projections
remain renderer-neutral. The shared EasyGL/SOFTWARE stock-effect witness checks the near-depth
endpoint and rejection of negative clip Z for its selected draws; it does not qualify every
shader, platform or renderer.

Therefore a future image that differs because of clipping or window-depth mapping is owned by the
renderer. Adding a Z remap, near-plane cut or Y flip to glTF import would double-convert correct
renderers, corrupt import/draw contracts, and violate the shared-defect rule below.

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

`GltfSharedDefectPolicy` scans selected source paths for dependency-direction violations:
renderer sources must not name the importer and importer sources may name the common renderer
contract, not an implementation. This is a useful structural gate, not compiler enforcement of
all transitive dependencies or proof that no hard-coded compensating math exists.
`scripts/gltf-renderer-parity.sh` runs selected golden-assertion tests and compares normalized
GTest result lines, not raw geometry dumps. Its outer control flow does not reliably propagate
all failing run statuses: matching failure/partial result streams can compare equal. Check each
underlying test run succeeds before treating the script's zero exit as parity evidence; handoff
§5 records this harness limitation. Source review and actual renderer witnesses remain necessary.

## The vendored cgltf: never patched, always worked around (`GLTF-038`)

CNA parses glTF with a vendored copy of `cgltf` (`third_party/cgltf/cgltf.h`, single header, MIT).
Three faults in it are known and CNA lives with all three, which raises the obvious question: why
not simply fix the header?

**The policy** is to keep the chosen upstream header byte-identical and implement workarounds
on CNA's side. `GltfVendoredCgltf.TheVendoredHeaderCarriesNoCnaEdits` checks recognizable cgltf
content, absence of CNA/task markers and presence of LICENSE. It does not compare an upstream
hash or prove byte identity: an unmarked edit can pass. The companion test finds named workaround
symbols/calls, not complete function semantics. Upstream provenance and discriminating regression
tests must be reviewed when upgrading; a passing marker scan alone is insufficient.

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
that is a decision with its own review, not something to do quietly in a bug fix. The precedent for the current disposition is the audit's cgltf finding: the misaligned `float` load UBSan found inside cgltf
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
