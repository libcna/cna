# Avatar real-rendering extension (NOXNA/EXT)

## What this is

The real XNA 4.0 `Microsoft.Xna.Framework.GamerServices` Avatar API
(`AvatarRenderer`, `AvatarAnimation`, `AvatarDescription`, ...) is a byte-exact port of the
*real* Microsoft reference assembly's behavior — which never actually renders anything off-Xbox.
`AvatarRenderer.Draw()` is a no-op, `State` always reports `Unavailable`, and
`AvatarDescription.CreateRandom()` returns an all-zero invalid description. This is not a CNA
limitation: the real Xbox Avatar body mesh, texture, and animation data was always streamed at
runtime from Xbox LIVE servers that have been offline for over a decade — the reference assembly
itself never bundled any of that content.

This extension adds a **CNA-original, opt-in** rendering path that actually draws a real,
GPU-skinned 3D mesh. It is explicitly **not** a reproduction of Microsoft's proprietary Xbox
Avatar art style — that data is permanently inaccessible. The faithful XNA behavior described
above remains the unconditional default; nothing here changes it unless a game explicitly calls
`AvatarRenderer::EnableRealRenderingEXT(...)`.

## Naming note: two unrelated "NOXNA" things

`NOXNA.md` documents a **separate**, larger, independently-planned `CNA_NOXNA` CMake option
gating a whole `CNA::Graphics` modern-engine layer (PBR/HDR/bloom/shadows/instancing/glTF). This
extension does **not** use that system. It uses the small, always-compiled `NOXNA` marker macro
and `*EXT` method-suffix convention already used throughout `GamerServices`
(`Guide::ShowAchievementsEXT`, `GamerPresence::SetPresenceModeStringEXT`, `ShaderEffect`). The two
share a name by coincidence; do not conflate them.

## Architecture

### New types (`Microsoft::Xna::Framework::Graphics`, all `NOXNA`)

- **`VertexPositionNormalTextureSkinned`** — a GPU-skinned vertex: position, normal, one texture
  coordinate, up to 4 bone blend weights/indices. Matches the 52-byte layout already proven by
  `examples/skinned_effect_integration_test.cpp`'s `SkinnedGpuVertex`. `VertexBuffer::SetData`
  overloads exist for it, matching the other 4 typed vertex kinds.
- **`SkinnedModelEXT`** — a real, GPU-skinnable mesh + skeleton + animation-clip container.
  Deliberately **not** built on `Model`/`ModelBone`/`ModelMesh`, which encode a *rigid*
  per-mesh parent-bone-transform hierarchy (real XNA's multi-part model animation), the wrong
  shape for per-vertex GPU skinning. Holds:
  - `BoneCount`, `ParentBoneIndices`, `BindPoseLocal`, `InverseBindPoseGlobal` — its own,
    independent skeleton (typically ~50-65 bones for a Mixamo-style rig).
  - `Parts` — named renderable parts (`"body"`, `"hair"`, ...), each a thin wrapper around a
    `ModelMeshPart` + owned `VertexBuffer`/`IndexBuffer`/`Texture2D`.
  - `Clips` — named `AnimationClipEXT`s (duration + per-bone `BoneTrackEXT` keyframe tracks).
  - `ComputeBoneTransformsEXT(clipName, position, loop, outWorldBones)` — samples each track
    (Lerp translation/scale, `Quaternion::Slerp` rotation), composes local TRS, walks the bone
    hierarchy (bones are stored in topological/breadth-first order) to build world transforms,
    then multiplies each by `InverseBindPoseGlobal` to produce final skinning matrices ready for
    `SkinnedEffect::SetBoneTransforms`.

### Bone-index mapping: fully decoupled

`AvatarRenderer::ParentBones` / `IAvatarAnimation::BoneTransforms` (the real Xbox 71-bone arrays)
are **untouched** by this extension — `AvatarRenderer::Draw(vector<Matrix>&, AvatarExpression)`
hard-throws `ArgumentException` unless given exactly 71 entries, so reusing that path for a
differently-sized rig was never an option. `SkinnedModelEXT` has its own, completely independent
bone count/hierarchy. `SkinnedEffect::MaxBones = 72` comfortably fits a typical Mixamo biped rig.

### Content pipeline

New `.skinnedmodel.json` + `.skeleton.bin` + `.clip.bin` schema, loaded via a new
`SkinnedModelTypeReader` registered in `ContentManager::RegisterBuiltinLoaders()`. Follows the
existing hand-rolled-JSON-parser convention (no new JSON library dependency) used by
`ModelTypeReader`/`SpriteFontTypeReader` — small flat manifest referencing binary blobs.

- `*.skinnedmodel.json`: `{"skeleton": "...", "parts": [{name, vertices, indices, vertexStride,
  texture}], "animations": [{name, clip}]}`.
- `*.skeleton.bin`: `int32 boneCount`, `int32 parentIndices[boneCount]`,
  `float bindPoseLocal[boneCount*16]`, `float inverseBindPoseGlobal[boneCount*16]` (row-major).
- `*.clip.bin`: `double durationSeconds`, `int32 trackCount`, then per track:
  `int32 boneIndex, int32 keyCount`, then per key: `double time, float tx,ty,tz,
  float qx,qy,qz,qw, float sx,sy,sz`.
- Clip names are the literal `AvatarAnimationPreset` enumerator names (`"Wave"`, `"Clap"`,
  `"FemaleIdleCheckNails"`, ...) — see `AvatarAnimationPresetToClipNameEXT`.
- Every path the manifest references (`skeleton`, a part's `vertices`/`indices`/`texture`, a
  clip's `clip`) is resolved relative to **the manifest file's own directory**, not the content
  root — so a bundle like `Content/avatar/male/` is self-contained and relocatable without
  rewriting any of its internal paths (fixed in Task 11.11; originally resolved against the
  content root, which broke the moment a manifest lived in a subdirectory).

### GamerServices API surface

- `AvatarRenderer::EnableRealRenderingEXT(device, model)` — opts in; builds an internal
  `SkinnedEffect`. `IsRealRenderingEnabledEXT()` reports whether this was called.
  `SetAppearanceEXT(appearance)` sets the skin/hair tint (see below).
  `DrawRealEXT(clipName, position, loop)` samples the clip, feeds bone matrices to the
  `SkinnedEffect`, and draws each part via the standard `GraphicsDevice` path — meaning it
  inherits whatever a given backend already does for 3D draws (see Backend support below).
  Throws `InvalidOperationException` if rendering wasn't enabled, `ObjectDisposedException`
  after `Dispose()`.
- `AvatarAnimation::SetRealClipNameEXT` / `GetRealClipNameEXT` — the clip name to drive real
  rendering for this preset; defaults to `AvatarAnimationPresetToClipNameEXT(preset)` at
  construction, overridable for documented best-effort substitutions (see
  `tools/avatar_asset_pipeline/README.md`).
- `AvatarAppearanceEXT` — a **CNA-invented** skin/hair tint struct. This is *not* a
  reverse-engineering of the real, proprietary, undocumented 1021-byte `AvatarDescription`
  format (never public, never reconstructible from the reference assembly alone). No clothing
  customization is provided in this phase.
- `AvatarBodyTypeToContentNameEXT(bodyType)` (Task 11.12) — maps `AvatarBodyType::Male`/
  `Female` to the ContentManager asset name (`"avatar/male/avatar"` / `"avatar/female/avatar"`)
  for the matching procedurally-generated body. The **only** place this mapping is made; a
  caller picks the `AvatarBodyType` by whatever means it already has (a game's own
  player-selection UI, a CLI flag like `examples/demo_avatar`'s `--gender`, etc.) — it is
  **not** derived from `AvatarDescription::getBodyTypeProperty()`, which faithfully never
  carries real body-type data (always lazily defaults to `Female`, matching the real XNA
  implementation's undocumented, never-populated description format).

### Backend support

| Backend | Status |
|---|---|
| EasyGL | Real, GPU-skinning proven end-to-end (`examples/avatar_real_render_integration_test.cpp`, pixel-readback, passing) |
| Vulkan | Real skinned pipeline exists (descriptor sets, per-frame bone UBO, dedicated pipeline); not yet smoke-tested for this feature |
| Bgfx | Real bone-uniform wiring exists; not yet smoke-tested for this feature |
| SDL_Renderer | 2D-only; any 3D resource creation (e.g. the `VertexBuffer`/`SkinnedEffect` this extension needs) already throws the pre-existing, tested `"SDL_Renderer does not support 3D: ..."` error — no new guard code was needed |

## Real content integration (Task 11.11)

`examples/demo_avatar/` is the first real, non-synthetic-fixture proof: a real windowed
demo that loads `Content/avatar/male/avatar.skinnedmodel.json` (produced by
`tools/avatar_builder/generate_avatar.py` + `tools/avatar_asset_pipeline/convert_avatar.py`,
Phase 11a/Tasks 11.1–11.10 — no MakeHuman/Mixamo involved) via
`ContentManager::Load<std::shared_ptr<SkinnedModelEXT>>`, calls
`AvatarRenderer::EnableRealRenderingEXT`/`SetAppearanceEXT`, and calls `DrawRealEXT("Stand0", ...)`
/ `DrawRealEXT("Wave", ...)` (Space toggles between them) every frame. Confirmed working on a
real X11/OpenGL window: a complete, correctly-proportioned, animated humanoid renders and both
clips visibly play.

Getting there surfaced three real, previously-undetected bugs — none in this file's own
architecture, all in code paths the Phase 10 synthetic fixture never exercised (it used identity
View/Projection, a single hand-built bone, and a hand-constructed `Quaternion::Identity`, so none
of these could show up until real camera matrices, a real multi-bone skeleton, and real
file-sourced quaternions were involved):

1. **`SkinnedModelTypeReader` path resolution** (`ContentManager.cpp`) resolved every manifest-
   referenced path against the content root instead of the manifest's own directory — see
   "Content pipeline" above. Content in a subdirectory (e.g. `Content/avatar/male/`) failed to
   load at all until fixed.
2. **A real evaluation-order bug**, also in `SkinnedModelTypeReader::Read()`: keyframe
   `Translation`/`Rotation`/`Scale` were each constructed directly from multiple chained
   `clipReader.Read<float>()` calls as constructor arguments — C++ does not guarantee left-to-right
   evaluation order for a function call's arguments, so the compiler was free to (and did) evaluate
   those side-effecting reads in a different order than intended, scrambling which bytes landed in
   which component. Fixed by reading each float into its own named local first (strictly sequential
   statements) before constructing the `Vector3`/`Quaternion`.
3. **`convert_avatar.py`'s bone-hierarchy reordering** (`build_node_hierarchy`, for
   `SkinnedModelEXT::ComputeBoneTransformsEXT`'s `parent[i] < i` requirement) reorders bones into
   topological order, but `inverseBindMatrices` and every vertex's `JOINTS_0` indices are given in
   glTF's own `skin.joints` order — both needed remapping to the new order, or bones were skinned
   using the wrong bind pose/vertex weights entirely. `bind_pose_local` is now also derived
   directly from `inverse_bind_global` via matrix inversion (correct by construction) rather than
   independently from each joint node's own TRS, which is simpler to keep consistent going forward.

Each bug was caught by actually rendering — first a forced-identity-bones diagnostic (isolating
camera/mesh/shader from bone math), then dumping `ComputeBoneTransformsEXT`'s own output at exact
rest pose (which must reduce to identity for every bone, by definition) and a hex dump of the raw
clip bytes — not by static code review. See `tools/avatar_asset_pipeline/convert_avatar.py` and
`ContentManager.cpp`'s `SkinnedModelTypeReader::Read()` for the fixes themselves.

**Task 11.12, done:** both bodies are now wired into the demo — `examples/demo_avatar --gender
male|female` (default `male`) selects which `AvatarBodyType` to pass to `AvatarDemo`, which maps
it to a ContentManager asset name via `AvatarBodyTypeToContentNameEXT` (see "GamerServices API
surface" above) and loads accordingly. Confirmed with a real screenshot of `--gender female`:
renders the distinct, correctly-scaled female body (0.93× overall, per
`tools/avatar_builder/generate_avatar.py`'s coarse female-scale placeholder), not just "the
mapping function returns a different string."

The confirmed elbow/sleeve tear and zero-weight vertices (`tools/avatar_builder/README.md`) are
unrelated content-quality gaps, not rendering bugs, and remain unfixed.

## What this explicitly is not

- Not a reproduction of the real Xbox Avatar body mesh, textures, or animation clips — those were
  never in the reference assembly and cannot be recovered from it.
- Not a change to any faithful XNA-spec behavior — every existing `AvatarRenderer`/`AvatarAnimation`
  test continues to pass unmodified.
- Not a clothing/accessory system — skin and hair tint only, in this phase.
- Not a MakeHuman/Mixamo-based pipeline — Phase 11a's own procedural Blender pipeline
  (`tools/avatar_builder/`) replaced that plan entirely; `tools/avatar_asset_pipeline/`'s
  `--body`/`--clip` CLI still supports the original MakeHuman/Mixamo file layout for reference,
  but `--embedded-clips` (the path Task 11.11 actually uses) is what's exercised now.
