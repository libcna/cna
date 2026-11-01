# Model loading and content pipeline: support and limits

This page covers `Model`, `ModelMesh`, `ModelMeshPart`, `ModelBone`, their four collections and
the content routes that construct them. Tasks 431–439 and 916 are historical audit/repair records,
not a blanket claim that every current loader, extension or renderer is identical to FNA.
The current implementation reference is the local FNA checkout; measured Microsoft XNA decisions
take precedence ([behavior decisions](xna-fna-behavior-decisions.md)).

## Runtime contract

- `CopyAbsoluteBoneTransformsTo` composes row-vector transforms as `local * parentAbsolute`.
  The hierarchy/index order must make the parent's result available before the child.
- All three copy methods reject vectors shorter than `Bones.Count` with
  `System::ArgumentOutOfRangeException`. They process exactly the bone count, ignoring extra
  source elements or leaving extra destination elements untouched. **Current FNA does the same**;
  the former CHECKLIST claim that oversized arrays were a CNA-only deviation is obsolete.
- `Model::Draw` walks each mesh's current effect collection, requires `IEffectMatrices`, sets
  `World = parentAbsolute * world`, then View/Projection, and calls `mesh.Draw()`.
  Missing `IEffectMatrices` raises `System::InvalidOperationException`.
- `ModelMesh::Draw` binds each drawable part's buffers, applies each current-technique pass and
  submits its indexed window. Ordinary parts default to TriangleList; `PrimitiveTypeEXT` carries
  imported point/line topologies. The renderer's 3D capability and effect/declaration contract
  still apply.
- CNA's public construction helpers are extensions to FNA's internal model-construction path.
  The parent-list overload accepts either no parent assignments or one per mesh, plus optional
  `rootBoneIndex` (default 0). Size/root errors raise `System::ArgumentOutOfRangeException`.
  With no bones, Root remains null; this leniency is not validation that such a model is drawable.
- Hand-built models/descriptors borrow bone/mesh/part pointers. Content-built models retain
  shared resource owners through `setOwnedResources`; copying a Model value shares those owners,
  not an independent set of GPU resources. `ContentManager::Unload` disposes tracked materialized
  children even while a Model value copy still exists.

Two explicit invalid-model differences remain recorded in [CHECKLIST](../CHECKLIST.md): a null
mesh ParentBone selects bone index 0, and a part with a null Effect is skipped. These do not make
arbitrary null pointers, invalid indices or an empty drawable hierarchy safe. Draw scratch is
thread-local in CNA rather than FNA's process-wide scratch; this does not promise concurrent
mutation or drawing through arbitrary graphics contexts.

CNA extensions also expose model cameras, skin-to-mesh mappings, material variants, an import
report and a whole-model bounding sphere. The sphere is recomputed from current node transforms
and applicable live skin palettes, before the caller's draw-world transform; it is conservative,
not a replacement for every deformation-specific bound. See [glTF conventions](gltf-conventions.md).

## Loading and resolution

For an ordinary cache-miss `ContentManager::Load<Model>("name")`, compiled XNB is tried through
the virtual `OpenStream` seam first, then CNB, then loose-file resolution. A real higher-tier
decode/I/O/override failure is propagated, not silently replaced with another format. Only the
base missing-XNB condition permits CNA's lower tiers; explicitly stream-only managers retain
their separate contract.

| Route | Reader / construction | Boundary |
|---|---|---|
| `.xnb` | `CNA::Internal::Xnb::ModelReader` | Supported XNA/FNA/MonoGame/CNA wire shapes and registered reader families; arbitrary custom readers, compiled effects and platform payloads need their own support. See [XNB support](xnb-content-pipeline-support.md). |
| `.cnb` | Model schema 1 or 2 loader | Schema 1 carries the native canonical model/material/animation shape. Schema 2 carries exact declarations, shared buffer/effect indices, draw windows, stored spheres and an explicit root; they are different schemas, not interchangeable feature supersets. See [CNB format](cnb-format.md). |
| `.cnj` | `ModelTypeReader` | Versioned editable Model document and sidecars; see [CNJ](cnj-format.md). |
| `.gltf`, `.glb` | Direct glTF 2.0 import | Runtime construction without an offline conversion step, within the [registry limitations](gltf-limitations.md) and [conformance scope](gltf-conformance.md). |

Within the loose tier an existing literal path wins; otherwise `.cnj` precedes the model reader's
`.gltf`/`.glb` candidates. Use extensionless logical names for normal compiled-tier selection.
The obsolete `.model.json` envelope is not a supported loader; migrate its contents to a
version-1 `.cnj`, rather than relying on the generic literal-path resolver to recognize it.

## What each route preserves

| Feature | Current behavior |
|---|---|
| Hierarchy and mesh parents | XNB/schema-2 CNB carry their stored graph. CNJ with a real bones array and direct glTF retain scene-node placement. Hierarchyless CNJ creates an identity Root and named child bones for its meshes. |
| Root selection | XNB and schema-2 CNB use stored root indices. Native CNJ/schema-1 CNB/direct glTF use their constructed root at index 0. |
| Bounds | XNB/schema-2 CNB read stored per-mesh spheres. Native CNJ/glTF/schema-1 CNB compute them from available geometry positions. Whole-model EXT bounds add placement/palette composition. |
| Tags | XNB supports registered object tags and supported string-keyed dictionary boxing, not every arbitrary `std::any` value. Native paths use specific carriers such as `SkinningData`, `ModelAnimationsEXT` and morph-part data; generic tag serialization is not uniform across routes. |
| Resource sharing | XNB shared-resource fixups and CNB schema-2 indices preserve their represented buffer/effect identities. Native CNJ creates buffers per represented part and reuses external textures/effects through the content cache; glTF reuses material effects and placement geometry according to its importer. |
| Existing-instance reload | CNA `ModelReader::Read` explicitly refuses a supplied existing Model with `ContentLoadException`. A normal cache hit reuses the cached value; it is not an in-place reader reload. FNA has a separate existing-instance path. |

`Model` supports skinning alongside its XNA-shaped node/mesh hierarchy: native loaders expose
`SkinningData` and clips, with `SkinsEXT` identifying the meshes each palette drives. Direct glTF
can carry multiple skin records; native compiled CNJ/schema-1 CNB represents one skeleton.
Applications advance the appropriate animation player and apply palettes; `Model::Draw` does not
automatically advance clips, choose material variants or install per-draw BlendState/CullMode.
Those import/draw conventions are detailed in the glTF pages.

`SkinnedModelEXT` remains a separate move-only part/skeleton/clip container with its own
`.skinnedmodel.json` reader and attachment ownership rules ([guide](skinned-model-ext.md)).
Its existence does not mean that ordinary Model content is rigid-only, nor does loading one
container implicitly convert it to the other.

## Verification

The 2026-11-01 audit checked Model/mesh runtime code, current local FNA Model/ModelReader, resolution,
native and schema-2 builders, XNB tag/fixup behavior and ownership tests. Graphics/content targets
build. Privately on OPENGLES3, all 111 focused runtime/collection cases and 40 selected CNJ/XNB/CNB,
shape and ownership cases pass. These are source/runtime regression checks, not a new genuine-XNA
oracle capture, arbitrary-content/all-renderer qualification or a recapture of the entire glTF
pixel ladder.
