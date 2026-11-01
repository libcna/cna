# `SkinnedModelEXT` (CNAEXT)

`Microsoft::Xna::Framework::Graphics::SkinnedModelEXT` is CNA's move-only mesh, skeleton and
animation container for games that bring their own skinned characters. It is not part of XNA 4.0
and is independent of the XNA avatar API and its catalog ([avatars](avatars.md)). It uses the
always-compiled `CNAEXT` marker and `*EXT` names, rather than the optional
[`CNA_CNAEXT` engine layer](cnaext-engine-layer.md).

It owns parts and their resources independently of `Model`. CNA's `Model` also supports imported
skinning through `SkinningData` and per-skin mesh mappings; that is a separate content/draw path
([model content](model-content-pipeline-support.md), [glTF conventions](gltf-conventions.md)).
Loading a glTF `Model` does not produce a `SkinnedModelEXT`.

## Skeletons, parts and sampling

- `VertexPositionNormalTextureSkinned` is a declared 52-byte record: position at byte 0, normal
  at 12, UV at 24, four float weights at 32 and four byte joint indices at 48.
- `BoneCount`, `ParentBoneIndices`, `BindPoseLocal` and `InverseBindPoseGlobal` describe one
  skeleton. Parents precede children; negative parent indices are treated as roots.
- `Parts` holds names, borrowed `ModelMeshPart*`/`Texture2D*` descriptors and `DiffuseColor`.
  The model owns the underlying parts, vertex/index buffers and optional textures.
  `AddPartEXT` transfers ownership and appends; it does not replace duplicate names.
  `RemovePartEXT` removes every matching name and its owned resources. Do not erase/reorder the
  public `Parts` vector directly: removal relies on its alignment with the private resource arrays.
- `AttachPartEXT(std::move(other))` replaces existing same-named parts and transfers the incoming
  parts/resources, leaving `other.Parts` empty. It retains this model's skeleton and clips.
  **Only equal bone counts are checked**; matching joint order, bind poses and graphics-device
  ownership remain the caller's responsibility. There is no skeleton retargeting.
- `Clips` maps names to `AnimationClipEXT`: duration, per-bone `BoneTrackEXT` keyframes and an
  explicit `TargetSpace`. This container expects `JointPalette` indices. `SceneNode` clips belong
  to the separate rigid-`Model` path; `ComputeBoneTransformsEXT` does not validate that tag.

`ComputeBoneTransformsEXT(clipName, position, loop, outWorldBones)` uses linear translation and
scale interpolation and `Quaternion::Slerp` for rotation. Keys must be in ascending time order;
the sampler does not sort them. Empty/out-of-range tracks are ignored and untracked bones retain
their local bind pose. Positive-duration clips wrap in ticks (including negative positions) when
looping, otherwise clamp to the clip; nonpositive durations sample time zero.

With row-vector matrices, local transforms are `Scale * Rotation * Translation`, a child's global
transform is `local * parentGlobal`, and the output is `InverseBindPoseGlobal * global`.
Unknown clip names, inconsistent skeleton-array sizes and forward/self parent references throw
`System::ArgumentException`. The routine sizes its output to `BoneCount`; it does not impose
`SkinnedEffect::MaxBones`. The subsequent `SkinnedEffect::SetBoneTransforms` requires 1–72 matrices.

## Loose content format

`ContentManager::Load<std::shared_ptr<SkinnedModelEXT>>` selects the
`.skinnedmodel.json` reader in `ContentModelLoaders.cpp`. A representative manifest is:

```json
{
  "skeleton": "skeleton.bin",
  "parts": [{
    "name": "body",
    "vertices": "body.verts.bin",
    "indices": "body.idx.bin",
    "vertexStride": 52,
    "texture": "body.png",
    "diffuseColor": [1, 1, 1, 1]
  }],
  "animations": [{"name": "Idle", "clip": "idle.clip.bin"}]
}
```

The reader requires the 52-byte skinned declaration for a populated part. Indices are unsigned
16-bit triangle-list indices. Missing vertex/index paths or a nonpositive stride skip the entry;
a different positive stride, nonintegral buffer record count or out-of-range index is refused.
The caller applies `DiffuseColor.xyz` and `.w` to `SkinnedEffect`'s diffuse color and alpha
alongside `Texture`; the descriptor itself does not draw or assign material state.

The converter writes little-endian sidecars:

| File | Layout |
|---|---|
| `*.skeleton.bin` | `int32 boneCount`, `int32 parentIndices[boneCount]`, then `float bindPoseLocal[boneCount*16]` and `float inverseBindPoseGlobal[boneCount*16]`, row-major |
| `*.clip.bin` | `double durationSeconds`, `int32 trackCount`; per track: `int32 boneIndex, keyCount`; per key: `double time`, translation `float[3]`, quaternion `float[4]`, scale `float[3]` |

The legacy skeleton/vertex reader copies native scalar representations; this path has not been
qualified on big-endian hosts. Animation references can also name a standalone
[`.cnj` AnimationClip](cnj-format.md), loaded through `ContentManager` caching, rather than a raw
clip sidecar. Raw clips default to `JointPalette`.

Every embedded reference is nonempty and relative to the manifest's directory. An in-root
manifest confines references to its content root; an explicitly loaded absolute external manifest
confines them to its own bundle directory. Existing symlink components are checked too. This
preserves relocation without treating every absolute top-level asset as forbidden
([path contract](filesystem-path-model.md)).

Bounds-checked binary reads reject truncation with `ContentLoadException`; the skeleton reader
also bounds the bone count to 0–100,000. This is not complete resource budgeting: the shared raw-clip
reader reserves vectors from nonnegative track/key counts before proving that the file contains
them, and neither the manifest nor the sampler validates every key order, joint/weight or
same-count skeleton relationship. No claim of arbitrary-input allocation safety is made.

## Conversion and drawing

`tools/avatar_asset_pipeline/convert_avatar.py` is an offline Python tool requiring pygltflib and
Pillow, independent of the runtime build. It selects the first skin, remaps its joints and inverse
bind matrices into topological order, writes four-weight vertices and 16-bit triangle lists, and
extracts the base-color factor/texture (white texture when absent). It is not the general CNA glTF
importer. Triangle winding is reversed from glTF's counter-clockwise order for the default XNA
`CullCounterClockwise` draw; CNA's ordinary glTF `Model` importer keeps the source winding.

Drawing uses a caller-owned `SkinnedEffect`, device state and per-part buffer bindings.
The model container does not independently qualify a renderer: the applicable boundary is
[`SkinnedEffect` support](skinnedeffect-support.md). The synthetic
`EasyGL_SkinnedModel_AttachPart` probe draws two independently created one-bone parts after
attachment and reads their red/blue pixels; it does not test arbitrary converted characters.

## Verification

The 2026-11-01 audit checked the container, content reader, path resolver, shared clip reader,
vertex declaration and converter. `CnaGraphicsTests`, `CnaContentTests` and the attachment probe
build. Privately on OPENGLES3, all 46 selected sampling/ownership/vertex tests and 14 manifest tests
pass, together with the two-part pixel probe. The Python converter regression command could not
start because the active Python environment lacks pygltflib; its implementation was checked by
source inspection. These results do not establish big-endian, every-renderer or arbitrary-asset
qualification.
