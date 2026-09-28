# `SkinnedModelEXT` (CNAEXT)

`Microsoft::Xna::Framework::Graphics::SkinnedModelEXT` is a CNA extension: a GPU-skinnable mesh,
skeleton and animation-clip container for games that bring their own skinned characters. It is not
part of XNA 4.0 and is unrelated to the XNA avatar API, which renders CNA's own avatar catalog
without any extension call (see `avatars.md`). It uses the always-compiled `CNAEXT` marker and
`*EXT` naming, not the separate `CNA_CNAEXT` engine layer (`misc/CNAEXT.md`).

It is deliberately not built on `Model`/`ModelBone`/`ModelMesh`, which encode XNA's rigid
per-mesh parent-bone hierarchy rather than per-vertex skinning.

## Types

- **`VertexPositionNormalTextureSkinned`** — position, normal, one texture coordinate, and four
  bone indices and weights (52 bytes); `VertexBuffer::SetData` accepts it like the other typed
  vertices.
- **`SkinnedModelEXT`** holds:
  - `BoneCount`, `ParentBoneIndices`, `BindPoseLocal`, `InverseBindPoseGlobal` — its own skeleton,
    bones in topological order (`parent[i] < i`);
  - `Parts` — named renderable parts, each a `ModelMeshPart` with owned buffers and an optional
    texture; `AddPartEXT`, `AttachPartEXT` (moves every part of another model with the same bone
    layout onto this one, replacing parts with the same name) and `RemovePartEXT` (frees the
    part's GPU resources);
  - `Clips` — named `AnimationClipEXT`s (duration and per-bone `BoneTrackEXT` keyframe tracks);
  - `ComputeBoneTransformsEXT(clipName, position, loop, outWorldBones)` — samples each track
    (linear translation and scale, `Quaternion::Slerp` rotation), composes local transforms down
    the hierarchy, and multiplies each by `InverseBindPoseGlobal`, producing matrices ready for
    `SkinnedEffect::SetBoneTransforms` (at most `SkinnedEffect::MaxBones`, 72).

## Content format

`ContentManager::Load<std::shared_ptr<SkinnedModelEXT>>` reads a `.skinnedmodel.json` manifest
with the project's flat JSON reader:

- `*.skinnedmodel.json`: `{"skeleton": "...", "parts": [{name, vertices, indices, vertexStride,
  texture}], "animations": [{name, clip}]}` (`vertexStride` defaults to 52).
- `*.skeleton.bin`: `int32 boneCount`, `int32 parentIndices[boneCount]`,
  `float bindPoseLocal[boneCount*16]`, `float inverseBindPoseGlobal[boneCount*16]` (row-major).
- `*.clip.bin`: `double durationSeconds`, `int32 trackCount`, then per track
  `int32 boneIndex, int32 keyCount`, then per key `double time, float tx,ty,tz,
  float qx,qy,qz,qw, float sx,sy,sz`.

Every referenced path is resolved relative to the manifest's own directory and must stay inside
the content root, so a bundle is self-contained and relocatable. Truncated files and header counts
inconsistent with a file's length are rejected with `ContentLoadException`
(`ContentManagerSkinnedModelTests.cpp`).

`tools/avatar_asset_pipeline/convert_avatar.py` converts a skinned glTF binary into this format.

## Renderer support

Drawing goes through `SkinnedEffect` and the ordinary `GraphicsDevice` path, so a model renders
wherever `SkinnedEffect` does. `EasyGL_SkinnedModel_AttachPart`
(`modules/graphics/examples/skinned_model_attach_part_integration_test.cpp`) draws an attached part
and checks the pixels; the renderers without 3D (SDL_Renderer) refuse the buffer and effect
creation with their existing errors.
