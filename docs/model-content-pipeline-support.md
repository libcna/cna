# Model loading and content pipeline: support and limitations

Covers `Microsoft::Xna::Framework::Graphics::Model` (and its `ModelMesh`/`ModelMeshPart`/
`ModelBone`/collection types): its runtime API, which Phase 49 (graphics Tasks 431-440)
audited line by line against FNA, and the content loaders that build a `Model` from files.

## `Model`'s runtime API: fully audited, FNA-faithful

Graphics Tasks 431-439 (full detail in Git history) confirmed `Model`/`ModelMesh`/
`ModelMeshPart`/`ModelBone` + all 4 collection types match FNA's property/method surface and
behavior closely, real bugs found and fixed along the way:

- `ModelBoneCollection`/`ModelMeshCollection` — missing `TryGetValue`/`Contains`/`begin()`/`end()`
  found and added (Tasks 432/433); `ModelMeshCollection::operator[](int)` had genuine UB via raw
  `[]` indexing, fixed to `.at()` (Task 433).
- `CopyAbsoluteBoneTransformsTo`/`CopyBoneTransformsFrom`/`CopyBoneTransformsTo` all verified
  against FNA's exact multiply order and per-bone logic (Tasks 435-437); one real, intentionally-kept
  deviation documented in `CHECKLIST.md` (CNA loops by `Bones.Count`, FNA by the caller's array
  length — CNA is strictly safer for oversized arrays).
- `Model::Draw` verified to apply each mesh's own `Effect` (Task 438) and each mesh's own
  `ParentBone`'s absolute transform (Task 439) — the latter required fixing a real gap:
  **`ModelMesh::ParentBone` was never assignable through any public API before Task 439.** A new
  4-arg `Model` constructor overload (`Model(GraphicsDevice*, vector<ModelBone*> bones,
  vector<ModelMesh*> meshes, vector<ModelBone*> meshParentBones)`) now lets hand-built/test code
  give each mesh an arbitrary parent bone, matching what FNA's real `ModelReader` always did
  internally (`mesh.ParentBone = bones[parentBoneIndex]`).
- **Fixed (Task 916, 2026-07-09)**: `Model`'s constructors used to always default `Root` to
  `bones[0]`, with no way to specify an arbitrary root bone index (FNA:
  `model.Root = bones[rootBoneIndex]`, any index). The 4-arg constructor now takes an optional 5th
  `rootBoneIndex` parameter (default `0`, preserving prior behavior); throws `std::out_of_range` for
  an out-of-bounds index, matches the 3-arg constructor's own empty-bones leniency (no throw, `Root`
  stays `nullptr`) when `bones` is empty regardless of the requested index.

## How a `Model` is loaded

`ContentManager::Load<Model>()` takes the first file that exists, in the order described in
[`cnj-format.md`](cnj-format.md):

| File | Loader | What it is |
|---|---|---|
| `.xnb` | `CNA::Internal::Xnb::ModelReader` | a model compiled by XNA, MonoGame, FNA or CNA's own pipeline; see [`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md) |
| `.cnb` | the CNB `Model` codec | CNA's compiled form, produced by `cna_tool_cnj_to_cnb` or `cna_tool_gltf_to_cnb`; see [`cnb-format.md`](cnb-format.md) |
| `.cnj` | `ModelTypeReader` | the editable JSON document with binary sidecars; see [`cnj-format.md`](cnj-format.md), "Model documents" |
| `.gltf`, `.glb` | the direct glTF 2.0 import | no conversion step; see [`gltf-conformance.md`](gltf-conformance.md) and [`gltf-limitations.md`](gltf-limitations.md) |

The CNA-original `.model.json` format is no longer read. A `.model.json` file becomes a version-1
`.cnj` by renaming it and adding the envelope ([`cnj-format.md`](cnj-format.md), "Older CNA JSON files").

## What the loaders build, against FNA's `ModelReader`

Until 2026-07 the only loader was the `.model.json` reader, which created a single bone, never set
`ParentBone`, `BoundingSphere` or `Tag`, and had no tests. That is no longer the case:

| FNA `ModelReader` behaviour | CNA today |
|---|---|
| a bone hierarchy with names and transforms | ✅ `.xnb` reads it; a version-2 `.cnj`, a `.cnb` and a glTF file carry the node tree. A version-1 `.cnj` has no hierarchy: each mesh gets its own child bone of the root at the identity transform. |
| `mesh.ParentBone = bones[parentBoneIndex]` | ✅ every route assigns it (`parentBone` in a `.cnj`) |
| `model.Root = bones[rootBoneIndex]` | ✅ `.xnb` honours the stored index; the other routes put the root at index 0 |
| a per-mesh `BoundingSphere` | ✅ read from `.xnb`; computed from the vertex positions for `.cnj` and glTF |
| `Tag` on the model, each mesh and each part | ✅ read from `.xnb`. A `.cnj` model's `Tag` carries its skinning data when it names a `skeleton`, and a morphing part's `Tag` its morph targets |
| shared `VertexBuffer`/`IndexBuffer`/`Effect` resources | ✅ `.xnb` resolves shared resources. A `.cnj` shares textures and effects by asset name through the content cache, and gives each mesh entry its own buffers |
| `existingInstance` (re-populating a loaded `Model`) | ❌ not supported on any route; FNA uses it only for pipeline hot-reload, never from game code |

The loaders are tested through `ContentManager`, not only through hand-built models: among others
`CnjModelTests.cpp`, `CnjModelSharedAnimationClipTests.cpp`, the XNB `ModelContentTypeReaderTests.cpp`,
the CNB model codec and equivalence suites, and the glTF import suites and conformance ladder
([`gltf-conformance.md`](gltf-conformance.md)).

## Not the same system: `SkinnedModelEXT`

`SkinnedModelEXT` (`.skeleton.bin`/`.clip.bin`, loaded via `SkinnedModelTypeReader`) is a
**deliberately separate**, `CNAEXT`, GPU-vertex-skinned mesh+skeleton+animation container — not
built on `Model`/`ModelBone`/`ModelMesh` at all, since those encode XNA's *rigid* per-mesh
parent-bone-transform hierarchy (a wrong fit for per-vertex GPU skinning). It is for games that
bring their own skinned characters and has its own binary format, its own content-pipeline reader,
and its own test coverage (`ContentManagerSkinnedModelTests.cpp`) — see `docs/skinned-model-ext.md`.
Everything else in this document is about the **plain, XNA-shaped** `Model` only.

## Summary

| Area | Status |
|---|---|
| `Model`/`ModelMesh`/`ModelMeshPart`/`ModelBone` runtime API | ✅ Fully audited and tested against FNA (Tasks 431-439); 1 documented safe deviation; Task 916's `rootBoneIndex` gap fixed 2026-07-09 |
| Loading from `.xnb` | ✅ `ModelReader`, wire-compatible with XNA/MonoGame/FNA models |
| Loading from `.cnb`, `.cnj` and glTF | ✅ see the table above |
| `.model.json` | ❌ no longer read; convert it to `.cnj` |
| Bone hierarchy, `ParentBone`, `BoundingSphere`, `Tag` through content | ✅ on every route that stores them; a version-1 `.cnj` has no node hierarchy |
| Resource sharing across meshes | ✅ `.xnb`; textures and effects only for `.cnj` |
| `existingInstance` hot-reload | ❌ not implemented; FNA uses it only from pipeline tooling |
| Skinned/animated content (`SkinnedModelEXT`) | ✅ separate system, see [`skinned-model-ext.md`](skinned-model-ext.md) |
