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
  skeleton. Sampling requires a nonnegative count, complete arrays and finite matrices.
  Parents precede children; negative parent indices are treated as roots.
- `Parts` holds names, borrowed `ModelMeshPart*`/`Texture2D*` descriptors and `DiffuseColor`.
  The model owns the underlying parts, vertex/index buffers and optional textures.
  `AddPartEXT` transfers ownership and appends; it does not replace duplicate names.
  `RemovePartEXT` removes every matching name and its owned resources. Do not erase/reorder the
  public `Parts` vector directly: removal relies on its alignment with the private resource arrays.
- `AttachPartEXT(std::move(other))` replaces existing same-named parts and transfers the incoming
  parts/resources, leaving `other.Parts` empty. It retains this model's skeleton and clips.
  Before replacement, it checks complete, finite, topologically ordered skeleton arrays,
  equal bone counts, identical parent/local-bind/inverse-bind arrays and one graphics device for
  the present resources. Self-attachment and inconsistent owned-part arrays are refused too.
  Validation refusal preserves both models' parts. Empty incoming parts are a no-op after
  self/count checks. Joint names are not stored: equivalent numeric skeletons alone cannot prove
  semantic joint identity, so pieces still need a common source rig. There is no retargeting.
- `Clips` maps names to `AnimationClipEXT`: duration, per-bone `BoneTrackEXT` keyframes and an
  explicit `TargetSpace`. This container expects `JointPalette` indices. `SceneNode` clips belong
  to the separate rigid-`Model` path; `ComputeBoneTransformsEXT` rejects them and unknown tags.

`ComputeBoneTransformsEXT(clipName, position, loop, outWorldBones)` uses linear translation and
scale interpolation and `Quaternion::Slerp` for rotation. Before sampling, it requires
nondecreasing key times and finite transforms; equal times remain legal. It does not sort keys.
After validation, empty/out-of-range tracks are ignored and untracked bones retain
their local bind pose. Positive-duration clips wrap in ticks (including negative positions) when
looping, otherwise clamp to the clip; nonpositive durations sample time zero.

`AnimationPlayer` and rigid `ApplyClipToBonesEXT` use the same validated interpolation, with
`JointPalette` and `SceneNode` respectively. Ordered tick distances use unsigned arithmetic so
even an interval from `TimeSpan::MinValue` to `MaxValue` does not overflow. Mutable borrowed clips
are checked again when sampled; validation therefore scans their keys each time.

With row-vector matrices, local transforms are `Scale * Rotation * Translation`, a child's global
transform is `local * parentGlobal`, and the output is `InverseBindPoseGlobal * global`.
Unknown clip names, invalid target spaces, descending key times, nonfinite transforms,
inconsistent skeleton-array sizes and forward/self parent references throw
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
also bounds the bone count to 0–100,000. The shared raw-clip reader checks file size before
allocating its owned input and proves every track/key byte range before reserving output vectors.
It uses the default CNB sanity limits: at most 512 MiB of input, 16,777,216 tracks and 16,777,216
aggregate keys. It also requires representable times, nondecreasing key order, finite transforms
and exact consumption without trailing bytes. Runtime and model-CNB compilation use this reader.

These are bounded input/count checks, not a total process/GPU memory budget: source bytes and
decoded arrays coexist, and concurrent loads remain application-owned. Vertex joint indices,
weights and semantic same-rig identity are not exhaustively validated. Big-endian skeleton/vertex
execution remains unqualified.

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

The 2026-11-02 `CNA-FIX-L13` regressions first reproduce eight failing sampling/attachment/raw-read
cases against the preceding code. The corrected 46 sampling/ownership tests pass on each of
OPENGLES3, SOFTWARE and SDLGPU, and all 454 selected CNB/clip/model-loading tests pass privately.
The strict-C skinned-model/player smokes cover indexed-skeleton mismatch, retained resource counts,
finite bind matrices and wrong-space refusal; the two-part pixel probe and project gates pass.
No native Windows, big-endian, arbitrary-character or every-renderer qualification is claimed.
