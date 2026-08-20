# Model loading and content pipeline: support and limitations

> **Update (plans/plan_xnb.md Phase F, 2026-07-16):** a second, genuinely real binary `.xnb` `ModelReader`
> now exists (`CNA::Internal::Xnb::ModelReader`), wire-compatible with real XNA/MonoGame/FNA-produced
> `Model` assets — full bone hierarchy, per-mesh `ParentBone`, `BoundingSphere`, shared-resource
> (`VertexBuffer`/`IndexBuffer`/`Effect`) resolution, all real. Per `cnj.md`'s "Core rule", `.xnb`
> always wins first in `ContentManager::Load<Model>()`'s resolution order, ahead of the
> `.model.json` path this document describes. **Everything below this note describes only the
> older, CNA-original `.model.json` loose-file loader** (`ModelTypeReader`) — see
> `docs/xnb-content-pipeline-support.md` for the real binary `.xnb` `ModelReader` this note
> refers to. The gaps documented below (no multi-bone hierarchy, no `ParentBone`/`BoundingSphere`/
> `Tag`, no resource sharing) are specific to the `.model.json` path and do **not** apply to the
> real `.xnb` `ModelReader`.

Covers `Microsoft::Xna::Framework::Graphics::Model` (and its `ModelMesh`/`ModelMeshPart`/
`ModelBone`/collection types) plus its content-pipeline loader, `ModelTypeReader`
(`src/Content/Xna/ContentManager.cpp`). Written as the closing documentation
task for Phase 49 (Tasks 431-440), which line-by-line audited and unit/pixel-tested `Model`'s own
runtime API against FNA (see `plans/plan_graphics.md` rows 431-439). This doc covers the one piece those
tasks didn't: how a `Model` actually gets **loaded from content**, and where that loader falls
short of both FNA's real behavior and CNA's own runtime API surface.

## `Model`'s runtime API: fully audited, FNA-faithful

Tasks 431-439 (see `plans/plan_graphics.md` for full detail) confirmed `Model`/`ModelMesh`/
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

None of the above is about *loading* a model from content, though — that's a separate system,
covered below, and it does **not** yet take advantage of the Task 439 fix.

## CNA's Model content format is NOT FNA's `.xnb` binary format

FNA's real `ModelReader` (`src/Content/ContentReaders/ModelReader.cs`) deserializes a compiled
binary `.xnb` produced by the XNA/MonoGame content pipeline (`ContentReader.ReadMatrix()`,
`ReadSharedResource<T>()`, etc.) — the same binary format every other FNA content type uses.

CNA has **no binary `.xnb` model reader at all**. `ModelTypeReader` instead defines an entirely
CNA-original format:

- A `.model.json` descriptor (hand-editable JSON, not the compiled binary XNA format)
- listing `"bones"` and `"meshes"` arrays, where each mesh entry references
- separate raw binary sidecar files for its vertex data (`"vertices"`) and 16-bit index data
  (`"indices"`), plus an inline `"vertexStride"` used to guess which of 4 known
  `VertexPositionColor`/`VertexPositionColorTexture`/`VertexPositionTexture`/
  `VertexPositionNormalTexture` layouts to interpret the bytes as (`ContentManager.cpp:519-532`).

This is a deliberate, CNAEXT departure — a real `.xnb` binary reader would require reimplementing
XNA's full content-pipeline compiler and its shared-resource graph serialization, well beyond this
project's scope. **No existing FNA-produced `.xnb` model asset can be loaded by CNA.** Games must
author (or generate) the `.model.json` + sidecar format directly.

## Content-pipeline `ModelTypeReader` gaps versus FNA's real `ModelReader`

| FNA `ModelReader` behavior | CNA `ModelTypeReader` behavior | Gap |
|---|---|---|
| Reads N bones with names/transforms, then a full parent/child bone hierarchy (`ReadBoneReference`, `AddChild`) | Always synthesizes exactly **one** `ModelBone` (index 0). The JSON `"bones"` array is only ever consulted for its **first element's `"name"`** field — no other bones are created, no hierarchy, no `AddChild` calls at all | **Real gap.** Any content-authored multi-bone hierarchy (needed to exercise the Task 439 per-mesh-ParentBone / Task 435-437 `Copy*`-transform machinery through real loaded content) is unrepresentable via this loader today |
| `mesh.ParentBone = bones[parentBoneIndex]` per mesh, arbitrary bone | Every mesh is constructed via the OLD 3-arg `Model`/`ModelMesh` path — `ParentBone` is never assigned, so it's always `nullptr` (defaults to bone 0 at draw time, Task 431's finding) | **Real gap**, and notably **not yet fixed even though the underlying API now can** (Task 439's new 4-arg `Model` constructor exists but `ModelTypeReader` doesn't call it) |
| `model.Root = bones[rootBoneIndex]`, arbitrary bone | Always defaults to `bones[0]` (the only bone that ever exists) | The runtime constructor API now supports an arbitrary `rootBoneIndex` (Task 916, fixed), but `ModelTypeReader` doesn't call it — moot regardless, since the loader only ever creates one bone |
| Reads a real per-mesh `BoundingSphere` from content | `ModelMesh::boundingSphere_` is **never set** — stays default-constructed for every loaded mesh | **Real gap.** Any game code relying on `Model.Meshes[i].BoundingSphere` for culling/hit-testing will silently get a degenerate (zero-radius) sphere for every content-loaded model |
| Reads a `Tag` per model and per mesh (`reader.ReadObject<object>()`) | Neither `Model::Tag` nor any `ModelMesh::Tag` is ever set by the loader | **Real gap**, low-severity (an optional, game-defined field) |
| `VertexBuffer`/`IndexBuffer`/`Effect` are `ReadSharedResource<T>` — the SAME buffer/effect object can be shared across multiple meshes/parts if the source content says so (resource deduplication) | Every mesh gets its own freshly-allocated `VertexBuffer`/`IndexBuffer`; every part with a non-`"BasicEffect"` name gets its own `Effect` via a fresh `cm.Load<...>()` call | Intentional simplification, not a correctness bug — just means CNA's loader can't reproduce a source asset that intentionally shares one mesh's buffers across several `ModelMeshPart`s |
| Supports `existingInstance` (re-populating an already-loaded `Model` in place, used by the content pipeline's hot-reload/rebuild path) | Not supported — every `Load<Model>()` call always constructs a brand new `Model` and set of GPU resources | Acceptable: FNA's own hot-reload path is `internal`/pipeline-tooling-only, never called from ordinary game code either |

**Zero test coverage**: confirmed via repo-wide search that no test or example anywhere exercises
`ModelTypeReader` (`ContentManager::Load<Model>(...)`) at all — only the unrelated
`SkinnedModelEXT`/Avatar binary-descriptor pipeline (a completely separate CNAEXT system, see
below) has any content-loading test coverage. None of Tasks 431-439's extensive `Model` unit/pixel
tests load a `Model` through `ContentManager` — they all hand-build `Model`/`ModelMesh`/
`ModelBone` instances directly in C++, which is exactly what let Task 439's own dead-`ParentBone`
gap go unnoticed for as long as it did. This is flagged as an open follow-up, not fixed here (pure
documentation task; fixing the bone-hierarchy/`ParentBone`/`BoundingSphere`/`Tag` gaps above and
adding real test coverage for the JSON loader are natural next steps but out of this task's scope).

## Not the same system: `SkinnedModelEXT` / Avatar real-rendering

`SkinnedModelEXT` (`.skeleton.bin`/`.clip.bin`, loaded via `SkinnedModelTypeReader`) is a
**deliberately separate**, `CNAEXT`, GPU-vertex-skinned mesh+skeleton+animation container — not
built on `Model`/`ModelBone`/`ModelMesh` at all, since those encode XNA's *rigid* per-mesh
parent-bone-transform hierarchy (a wrong fit for per-vertex GPU skinning). It exists to support the
Avatar real-rendering extension (`AvatarRenderer::EnableRealRenderingEXT`) and has its own binary
format, its own content-pipeline reader, and its own extensive test coverage
(`ContentManagerSkinnedModelTests.cpp`) — see `docs/avatar-real-rendering-ext.md` for full detail.
Everything in this document is about the **plain, XNA-shaped** `Model`/`ModelTypeReader` path only.

## Summary

| Area | Status |
|---|---|
| `Model`/`ModelMesh`/`ModelMeshPart`/`ModelBone` runtime API | ✅ Fully audited and tested against FNA (Tasks 431-439); 1 documented safe deviation; Task 916's `rootBoneIndex` gap fixed 2026-07-09, zero known open gaps in the runtime API |
| `Model` binary `.xnb` loading | ❌ Not implemented — CNA uses an original `.model.json` + binary-sidecar format instead, not wire-compatible with any real XNA/FNA content asset |
| Multi-bone hierarchy via content pipeline | ❌ Not implemented — loader always synthesizes exactly 1 bone, even though the runtime API fully supports arbitrary hierarchies |
| Per-mesh `ParentBone` via content pipeline | ❌ Not implemented — loader never assigns it, even though Task 439 added the runtime API needed to do so |
| Per-mesh `BoundingSphere` via content pipeline | ❌ Not implemented — always left at its zero-radius default |
| `Tag` (model/mesh) via content pipeline | ❌ Not implemented |
| Resource sharing/dedup across meshes | ❌ Not implemented (always allocates fresh per mesh) — low severity |
| `existingInstance` hot-reload | ❌ Not implemented — acceptable, FNA's own use of this is pipeline-tooling-only |
| Test coverage of the JSON model loader itself | ❌ None — zero tests or examples exercise `ModelTypeReader` |
| Skinned/animated content (Avatar) | ✅ Separate system, fully covered — see `docs/avatar-real-rendering-ext.md` |
