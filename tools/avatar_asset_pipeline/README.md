# Avatar asset pipeline

Offline, one-time asset preparation for `AvatarRenderer`'s real-rendering extension (see
`docs/avatar-real-rendering-ext.md`). Not part of the C++ build; never run by CNA at runtime.

## Status

`convert_avatar.py` is written and structurally verified against a small hand-built synthetic
glTF fixture (bone hierarchy/topological ordering, name-based animation retargeting, and CNA's
exact `.skeleton.bin`/`.clip.bin`/`.skinnedmodel.json` binary layout all round-trip correctly).

**Now also run successfully against real generated content** (Task 11.10): CNA's own
procedural Blender pipeline (`tools/avatar_builder/`, Phase 11a) produces
`male_avatar.glb`/`female_avatar.glb` — real body+skeleton+animations, no MakeHuman/Mixamo
involved. Converting that output surfaced two real gaps, both fixed:

- `convert_body()`'s per-mesh part names came from the exported mesh **data-block** name,
  not the object name — `tools/avatar_builder/generate_body.py`/`generate_clothes.py`
  only renamed the object, leaving Blender's auto-generated `Cylinder`/`Cylinder.024`-style
  data-block names to leak into `avatar.skinnedmodel.json`'s part list. Fixed at the
  source (both scripts now rename `obj.data.name` too), not by special-casing names here.
- The original `--body`/`--clip` CLI assumed the MakeHuman/Mixamo workflow's file layout:
  one body file, separate per-clip files, each with exactly one animation
  (`convert_clip` hardcoded `gltf.animations[0]`). CNA's own pipeline bundles
  body+skeleton+**all** clips in one `.glb` instead. Added `--embedded-clips`: converts
  every animation already in `--body` itself, using each animation's own `.name` as the
  clip name (see `convert_embedded_clip()`/`_tracks_from_animation()` — the per-animation
  conversion logic is now shared between both code paths, not duplicated).

Verified: `python3 convert_avatar.py --body male_avatar.glb --out content/avatar/male
--embedded-clips` (against a real, freshly-generated `male_avatar.glb`) produces a
20-bone skeleton (19 real bones + Blender's synthetic `neutral_bone`, expected — see
`tools/avatar_builder/README.md`), 5 correctly-named parts, and both `Stand0.clip.bin`
(19 tracks, 3.75s)/`Wave.clip.bin` (19 tracks, 2.5s) — durations confirmed against
Blender's actual default scene fps (24), not assumed.

It has **not** been run against a real MakeHuman export or real Mixamo clips yet (the
`--body`/`--clip` path, as opposed to `--embedded-clips`) — that requires manual GUI
(MakeHuman) and browser (Mixamo.com, Adobe account) steps outside headless automation,
and per Phase 11's own decision (see `NEXT.md`) is no longer the planned path forward;
CNA's own procedural pipeline replaces it. Two body meshes (male + female) exist, per the
project's decision to match XNA's `AvatarBodyType` concept from the start rather than
shipping a unisex placeholder — see `tools/avatar_builder/generate_avatar.py`'s
`--gender` flag.

**Now also proven end-to-end through the real C++ engine** (Task 11.11,
`examples/demo_avatar/`): the converted content loads via `ContentManager` and renders,
animated, in a real window (see `docs/avatar-real-rendering-ext.md`'s "Real content
integration" section for full detail). Getting there found and fixed two more real bugs
in `convert_body()` itself, on top of the two above:

- `build_node_hierarchy()`'s topological bone reordering (needed for
  `ComputeBoneTransformsEXT`'s `parent[i] < i` assumption) left `inverseBindMatrices`
  and every vertex's `JOINTS_0` indices in glTF's original `skin.joints` order — both
  needed remapping to the new order (`joint_index_remap`), or bones were skinned with
  the wrong bind pose/vertex weights.
- `bind_pose_local`'s conversion from glTF's column-major matrix convention to CNA's
  row-major one was backwards: they're actually byte-identical for the same transform
  (transposing the matrix and swapping major order are inverse operations that cancel
  out), so the correct fix was to stop transposing, not to transpose consistently.
  `bind_pose_local` is now derived directly from `inverse_bind_global` via matrix
  inversion (correct by construction — see `_invert4x4`/`_mat_mul_rowmajor`) rather than
  independently from each joint's own TRS, removing an entire class of independent-
  derivation-disagrees-with-itself risk.

A third bug turned out to be in the C++ engine itself, not this pipeline:
`ContentManager.cpp`'s `SkinnedModelTypeReader` had a path-resolution bug (paths
resolved against the content root instead of the manifest's own directory) and a real
evaluation-order bug reading keyframe data (relying on unspecified C++ argument
evaluation order across multiple side-effecting reads) — see
`docs/avatar-real-rendering-ext.md` for detail; nothing to change here for either.

## Manual steps (to be performed by a human, not automatable here)

1. Install MakeHuman (https://www.makehumancommunity.org/).
2. In MakeHuman, build a body (male, then separately female), and under the **Pose/Animate**
   tab select the **"Mixamo"** skeleton preset before exporting. Export each as FBX.
3. On Mixamo.com (requires a free Adobe account), search for and download each clip listed in
   the mapping table below as FBX, **"without skin"** (Mixamo can retarget any of its clips onto
   a previously-uploaded custom character — upload the MakeHuman FBX there first so the
   downloaded clips are already retargeted onto a matching skeleton).
4. Convert every FBX to glTF2 with the assimp CLI:
   ```
   assimp export male_body.fbx male_body.glb -f gltf2
   assimp export female_body.fbx female_body.glb -f gltf2
   assimp export Wave.fbx Wave.glb -f gltf2
   # ... one per clip
   ```
5. Run the converter (requires `pip install pygltflib`):
   ```
   python3 convert_avatar.py --body male_body.glb --out content/avatar/male \
       --clip Wave.glb Wave --clip Clap.glb Clap --clip Idle.glb Stand0 ...
   python3 convert_avatar.py --body female_body.glb --out content/avatar/female \
       --clip Wave.glb Wave --clip Clap.glb Clap ...
   ```
   Because both bodies use the identical MakeHuman "Mixamo" skeleton preset, the same
   downloaded/converted clip `.glb` files can be fed to both `--out` directories — no separate
   Mixamo download is needed per body. **Verify this assumption** once both bodies exist (compare
   bone names/order between `male/skeleton.bin` and `female/skeleton.bin`); if MakeHuman's male
   and female base meshes turn out to have divergent rest-pose bone orientations despite the
   shared preset, only the divergent clips need per-body reconversion, not all of them.

## Clip name -> Mixamo source mapping

Clip names **must** match `AvatarAnimationPreset` enumerator names exactly (see
`AvatarAnimationPresetToClipNameEXT` in
`include/Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp`) — this is
the lookup key `AvatarRenderer::DrawRealEXT` uses against a loaded `SkinnedModelEXT`'s `Clips`.

| CNA preset name | Mixamo source clip | Match quality |
|---|---|---|
| Stand0 | "Idle" | Reasonable variant |
| Stand1 | "Breathing Idle" | Reasonable variant |
| Stand2 | "Standing Idle Variation 01" | Reasonable variant |
| Stand3 | "Standing Idle Variation 02" | Reasonable variant |
| Stand4 | "Bored" | Reasonable variant |
| Stand5 | "Neutral Idle" | Reasonable variant |
| Stand6 | "Happy Idle" | Reasonable variant |
| Stand7 | "Relaxed Idle" | Reasonable variant |
| Clap | "Clapping" | Exact |
| Wave | "Waving" / "Standing Wave" | Exact |
| Celebrate | "Cheering" / "Victory" | Close |
| FemaleIdleLookAround | "Looking Around" | Exact (shared with Male variant) |
| FemaleIdleShiftWeight | "Weight Shift" | Exact (shared with Male variant) |
| FemaleIdleCheckNails | *(no exact Mixamo match)* | **Documented substitute needed** — pick a generic vanity/hand-inspection idle; do not invent motion data by hand |
| FemaleIdleFixShoe | *(no exact Mixamo match)* | **Documented substitute needed** — pick a bend/crouch gesture |
| FemaleAngry | "Angry" | Exact (shared with Male variant) |
| FemaleConfused | "Confused" | Exact (shared with Male variant) |
| FemaleLaugh | "Laughing" | Exact (shared with Male variant) |
| FemaleCry | "Crying" | Exact (shared with Male variant) |
| FemaleShocked | "Shocked" | Exact |
| FemaleYawn | "Yawn" | Exact (shared with Male variant) |
| MaleIdleLookAround | "Looking Around" | Exact (shared with Female variant) |
| MaleIdleStretch | "Stretching" | Exact |
| MaleIdleShiftWeight | "Weight Shift" | Exact (shared with Female variant) |
| MaleIdleCheckHand | "Checking Watch" | Close substitute |
| MaleAngry | "Angry" | Exact (shared with Female variant) |
| MaleConfused | "Confused" | Exact (shared with Female variant) |
| MaleLaugh | "Laughing" | Exact (shared with Female variant) |
| MaleCry | "Crying" | Exact (shared with Female variant) |
| MaleSurprised | "Surprised" | Exact |
| MaleYawn | "Yawn" | Exact (shared with Female variant) |

Roughly 16-20 distinct Mixamo clips cover all 31 presets once shared clips are reused across
their Female/Male pairs. Where a documented substitute is used instead of an exact name match,
call `AvatarAnimation::SetRealClipNameEXT(...)` at the point of construction (or after) to point
that preset's playback at whichever clip name was actually produced, if it differs from the
preset's own name.

## Why the pipeline stops here for now

MakeHuman GUI interaction and Mixamo's browser-based, account-gated download flow cannot be
driven from this headless environment. The script above is ready to run as soon as a human
completes steps 1-4; nothing about `convert_avatar.py` itself is blocked.
