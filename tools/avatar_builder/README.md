# Procedural avatar asset generator (Phase 11)

Offline, one-time content-authoring pipeline for `AvatarRenderer`'s real-rendering
extension (see `docs/avatar-real-rendering-ext.md`). Not part of the C++ build; never
run by CNA at runtime. Each script runs headless via system-installed Blender:

```
blender --background --python tools/avatar_builder/generate_skeleton.py
```

## Why procedural, not MakeHuman/Mixamo

The prior session tried MakeHuman (GUI) and CharMorph/Blender (external repo) and hit
Claude Code permission-classifier stops around downloading/executing third-party
tooling and code (full account in `NEXT.md`). A fully procedural pipeline sidesteps that
class of problem: every script here is original code, run through the already-installed,
already-trusted local Blender — no third-party downloads, no external codebase to
execute. It also has a real engineering advantage over MakeHuman+Mixamo: the same
script that builds the skeleton (this one) also authors the animations
(`generate_animations.py`, Task 11.6) directly on these bone names, so there is no
bone-retargeting step at all.

## Status

- [x] Task 11.1 — `generate_skeleton.py`: builds the canonical skeleton below.
- [x] Task 11.2 — `generate_body.py`: procedural low-poly body, auto-weighted to the skeleton.
- [x] Task 11.3 — `generate_materials.py`: 5 flat-color placeholder materials, Skin assigned to the body.
- [x] Task 11.4 — `generate_morphs.py`: `Smile`/`Blink` shape keys on the body mesh.
- [x] Task 11.5 — `generate_hair.py` / `generate_clothes.py`: helmet-like hair cap, Shirt/Pants/Shoes shells.
- [x] Task 11.6 — `generate_animations.py`: `Stand0`/`Wave` Actions; confirmed real elbow/sleeve tearing under bend.
- [x] Task 11.7 — `export_gltf.py` / `generate_avatar.py`: exports male_avatar.glb/female_avatar.glb, reopens cleanly.
- [ ] Task 11.8 — `validate_gltf.py`

Full task detail: `plan_net.md` Phase 11 ("Procedural Avatar Asset Generator").

## Canonical skeleton (`generate_skeleton.py`)

A new, **CNA-original** ~19-bone biped armature named `CNAAvatarSkeleton`. This is
**not** the real Xbox 71-bone hierarchy used by the faithful XNA Avatar port
(`Microsoft::Xna::Framework::GamerServices::AvatarRenderer::ParentBones`, Phase 8 — that
stays untouched and unrelated) and **not** Mixamo/Rigify bone naming. Every other Phase
11 script (body weights, morphs, animations) must key its bone names off this exact
list — this file is the single source of truth.

Rest pose: standing, Z-up, sagittal plane at X=0, `+X` = the figure's own left (matching
the `.L`/`.R` suffixes), roughly 1.8 m tall. Coordinates are in meters.

| Bone | Parent | Head | Tail | Connected |
|---|---|---|---|---|
| `Hips` | *(root)* | (0.00, 0.00, 1.00) | (0.00, 0.00, 1.10) | no |
| `Spine` | `Hips` | (0.00, 0.00, 1.10) | (0.00, 0.00, 1.25) | yes |
| `Spine1` | `Spine` | (0.00, 0.00, 1.25) | (0.00, 0.00, 1.40) | yes |
| `Neck` | `Spine1` | (0.00, 0.00, 1.40) | (0.00, 0.00, 1.50) | yes |
| `Head` | `Neck` | (0.00, 0.00, 1.50) | (0.00, 0.00, 1.70) | yes |
| `Shoulder.L` | `Spine1` | (0.00, 0.00, 1.40) | (0.18, 0.00, 1.40) | yes |
| `Shoulder.R` | `Spine1` | (0.00, 0.00, 1.40) | (-0.18, 0.00, 1.40) | yes |
| `UpperArm.L` | `Shoulder.L` | (0.18, 0.00, 1.40) | (0.50, 0.00, 1.40) | yes |
| `UpperArm.R` | `Shoulder.R` | (-0.18, 0.00, 1.40) | (-0.50, 0.00, 1.40) | yes |
| `LowerArm.L` | `UpperArm.L` | (0.50, 0.00, 1.40) | (0.75, 0.00, 1.40) | yes |
| `LowerArm.R` | `UpperArm.R` | (-0.50, 0.00, 1.40) | (-0.75, 0.00, 1.40) | yes |
| `Hand.L` | `LowerArm.L` | (0.75, 0.00, 1.40) | (0.85, 0.00, 1.40) | yes |
| `Hand.R` | `LowerArm.R` | (-0.75, 0.00, 1.40) | (-0.85, 0.00, 1.40) | yes |
| `UpperLeg.L` | `Hips` | (0.10, 0.00, 1.00) | (0.10, 0.00, 0.55) | no |
| `UpperLeg.R` | `Hips` | (-0.10, 0.00, 1.00) | (-0.10, 0.00, 0.55) | no |
| `LowerLeg.L` | `UpperLeg.L` | (0.10, 0.00, 0.55) | (0.10, 0.00, 0.10) | yes |
| `LowerLeg.R` | `UpperLeg.R` | (-0.10, 0.00, 0.55) | (-0.10, 0.00, 0.10) | yes |
| `Foot.L` | `LowerLeg.L` | (0.10, 0.00, 0.10) | (0.10, 0.15, 0.02) | yes |
| `Foot.R` | `LowerLeg.R` | (-0.10, 0.00, 0.10) | (-0.10, 0.15, 0.02) | yes |

19 bones total. `UpperLeg.L/R` are deliberately *not* connected to `Hips` (their head
sits at the hip joint, not at `Hips`' tail) — everything else in a given limb/spine
chain is connected, since each child's head coincides exactly with its parent's tail.

`generate_skeleton.py` exposes `build_skeleton()` (creates/returns the armature object,
safe to call repeatedly in the same Blender session) and a `BONES` list other scripts
can import for the name/parent/position data, so `generate_avatar.py` (Task 11.7) can
orchestrate this and the other generator scripts in one Blender process without
re-deriving the bone list.

Verify: `blender --background --python tools/avatar_builder/generate_skeleton.py` runs
without error and asserts every bone name/parent matches this table.

## Procedural body (`generate_body.py`)

Builds one primitive "flesh" shape per bone in the table above — a cylinder along the
bone's own head→tail axis, plus a small joint sphere at its head end (the `Head` bone
gets a single sphere instead, centered on its head/tail midpoint) — then joins every
part into a single `CNAAvatarBody` mesh and parents it to the skeleton with
`bpy.ops.object.parent_set(type='ARMATURE_AUTO')`. Building geometry straight from the
bone list guarantees every deforming bone has nearby mesh, so automatic weights produce
a non-empty vertex group for all 19 bones without hand-authoring anything.

`build_body(armature_obj)` is importable the same way as `generate_skeleton.build_skeleton()`,
for reuse by `generate_avatar.py` (Task 11.7).

**Automatic weights are a starting point, not a finished result.** A manual
`BLENDER_WORKBENCH` render (T-pose) plus an ad hoc pose-mode elbow/knee bend test showed
reasonable proportions and no mesh tearing at the bend, but that is **not** the plan's
required "no gross bending artifacts" check — that check needs Task 11.6's real test
animations (`Stand0`/`Wave`) and is deferred until they exist. Expect a manual
weight-painting correction pass to still be needed at elbows/knees/shoulders once those
animations play.

Verify: `blender --background --python tools/avatar_builder/generate_body.py` runs
without error and asserts a non-empty vertex group exists for every bone name in `BONES`.

## Placeholder materials (`generate_materials.py`)

Five flat-color Principled BSDF materials, no texture maps: `CNAAvatarSkin`,
`CNAAvatarHair`, `CNAAvatarShirt`, `CNAAvatarPants`, `CNAAvatarShoes` (see
`MATERIAL_COLORS` for the exact RGBA values). Only `CNAAvatarSkin` is assigned anywhere
right now — as the sole material slot on the Task 11.2 body mesh, via
`assign_body_material()`. `Hair`/`Shirt`/`Pants`/`Shoes` exist in `bpy.data.materials`
for `generate_hair.py`/`generate_clothes.py` (Task 11.5) to assign once that geometry
exists; there is nothing to assign them to yet.

`build_materials()`/`assign_body_material(body_obj, materials)` are importable the same
way as the skeleton/body builders, for reuse by `generate_avatar.py` (Task 11.7).

These are explicitly placeholder flat colors, not final art — texture painting or
tinting variety is out of scope until a later iteration (`plan_net.md` Phase 11c).

Verify: `blender --background --python tools/avatar_builder/generate_materials.py` runs
without error and asserts all 5 materials exist with `Skin` as the body mesh's sole
material slot.

## Placeholder facial morphs (`generate_morphs.py`)

Two shape keys on `CNAAvatarBody`: `Smile` and `Blink` (plus the implicit `Basis`).

The head (Task 11.2) is a single low-poly UV sphere with no separate eye/mouth
geometry, so vertex selection can't target "the mouth" or "an eyelid" directly — instead
it picks vertices by the sphere's own fixed latitude rings (a `segments=8`/`ring_count=6`
UV sphere always has vertices at `z/radius` in `{0, +-0.5, +-0.866, +-1.0}`, regardless
of the actual radius) that face forward (`+Y`, matching the skeleton's own forward
convention — see the canonical-skeleton section above):

- **`Smile`** selects the front-facing vertices of the ring one latitude step below the
  equator (`z/radius ~= -0.5`) and lifts the ones further from center-line (`|x|` larger)
  more than the ones near it, approximating a corners-up smile shape.
- **`Blink`** selects the front-facing vertices of the ring one latitude step above the
  equator (`z/radius ~= +0.5`) and pulls them down/inward, approximating closing eyelids.

This is an explicitly crude, placeholder approximation of facial motion on an
unmodeled head — not real facial geometry. A manual render at `value=1.0` for both keys
shows a visibly different (dented/bulged) head shape, confirming the deformation is
real, but it will not look like an actual face. Treat this the same way as Task 11.2's
auto-weights: good enough to prove the mechanism (shape keys exist, drive real vertex
motion, will export via glTF), not good enough to be final content.

### Adding more morphs later

Once a real modeled head (with actual eyelid/mouth topology) replaces the Task 11.2
placeholder sphere, do **not** try to adapt this ring-selection approach — write new
shape keys keyed to the new mesh's actual vertex groups/named vertex selections instead.
`build_morphs(body_obj)` follows the same `_add_shape_key(body_obj, name, indices,
displacement_fn)` pattern for any future shape key: pick a vertex index list and a
per-vertex displacement function, and it handles creating/replacing the shape key block.

`build_morphs(body_obj)` is importable the same way as the other builders, for reuse by
`generate_avatar.py` (Task 11.7).

Verify: `blender --background --python tools/avatar_builder/generate_morphs.py` runs
without error and asserts both shape keys exist and each displaces at least one vertex
by more than a trivial amount.

## Placeholder clothes (`generate_clothes.py`)

Three garments, each its own mesh object parented to the skeleton with automatic
weights (same technique as the body) and its matching material assigned:

| Garment | Bones covered | Padding over body radius |
|---|---|---|
| `CNAAvatarShirt` | `Spine, Spine1, Shoulder.L/R, UpperArm.L/R` | +0.02 m |
| `CNAAvatarPants` | `Hips, UpperLeg.L/R, LowerLeg.L/R` | +0.02 m |
| `CNAAvatarShoes` | `Foot.L/R` | +0.015 m |

Each garment is built the same way as the body: one `generate_body.add_cylinder_segment`
+ `generate_body.add_joint_sphere` per covered bone (that bone's own `BONE_RADII` radius
plus the garment's padding), joined into a single mesh. These are offset shells over the
existing body shape, not fitted tailoring or cloth simulation — expect visible clipping
through the body at the sleeve/pant-leg/shoe seams. Known, accepted limitation of this
first pass, not a bug to chase down yet.

`build_clothes(armature_obj, materials)` is importable the same way as the other
builders, for reuse by `generate_avatar.py` (Task 11.7).

Verify: `blender --background --python tools/avatar_builder/generate_clothes.py` runs
without error and asserts each garment has a vertex group for every bone it covers and
its correct material assigned.

## Placeholder hair (`generate_hair.py`)

`CNAAvatarHair`: a single open-bottomed hemisphere shell (built directly with `bmesh`,
not `generate_body`'s cylinder/sphere helpers, since it needs half a sphere rather than a
whole one) sized just outside the head, parented to the skeleton with automatic weights
— since only the `Head` bone is nearby, this ends up rigidly following the head in
practice, without needing bone-parenting. Assigned the `Hair` material.

This is a literal helmet shape, not modeled hair strands or hair cards — explicitly
expected and accepted for this first pass (a later iteration, `plan_net.md` Task 11.14,
can replace it with real hair geometry).

`build_hair(armature_obj, materials)` is importable the same way as the other builders,
for reuse by `generate_avatar.py` (Task 11.7).

Verify: `blender --background --python tools/avatar_builder/generate_hair.py` runs
without error and asserts the hair mesh is non-empty, has a vertex group for `Head`, and
has the `Hair` material assigned.

## Placeholder animations (`generate_animations.py`)

Two Blender Actions on `CNAAvatarSkeleton`, named to match `AvatarAnimationPreset`
exactly (see `AvatarAnimationPresetToClipNameEXT`):

- **`Stand0`** (idle, 90 frames, loops — frame 1 and frame 90 are identical): a subtle
  `Hips` bob (+-0.01 m) and `Spine1` rock (+-2 deg).
- **`Wave`** (60 frames, plays once): `UpperArm.R` rotates to raise the arm, then
  `LowerArm.R` folds the elbow back and forth a few times before both return to rest.

Both are simple keyframed bone rotations — no motion capture, no external clip source —
authored directly against the Task 11.1 bone names, so there is no retargeting step.

**A real gotcha, worth knowing before adding a third animation:** the first `Wave`
attempt keyframed `LowerArm.R`'s rotation on its local Y axis, which is the bone's own
head-to-tail length axis — rotating a round cylinder around its own length axis is an
invisible twist. This produced a "working" action (nonzero frame range, no errors) that
did *nothing visible* when rendered. Caught only by actually rendering it and comparing
frames. Local X (or Z, depending on the bone's orientation/roll) is what visibly bends a
limb — verify any new bone-rotation animation by rendering it, not just by checking that
keyframes exist.

`build_animations(armature_obj)` is importable the same way as the other builders, for
reuse by `generate_avatar.py` (Task 11.7).

Verify: `blender --background --python tools/avatar_builder/generate_animations.py` runs
without error and asserts both actions exist with a nonzero frame range.

### Bend-artifact check (deferred from Task 11.2, done here)

Posing the full clothed avatar through `Wave`'s peak elbow-fold frames and rendering a
close-up confirms a **real, visible tear** at the elbow/wrist: the forearm and hand
separate from the shirt sleeve (and slightly from each other) at both fold extremes
tested. This is the automatic-weights limitation Task 11.2 always expected, now
*confirmed* rather than assumed. **Still open, not fixed:** a manual weight-painting
correction pass at the elbows (and likely knees/shoulders too, untested at comparable
fold angles) is needed before this rig is presentable in motion. Out of scope for Tasks
11.1–11.8's "functional, not polished" pipeline milestone — revisit when polish work is
prioritized (`plan_net.md` Phase 11c).

## Orchestration and export (`generate_avatar.py` + `export_gltf.py`)

`generate_avatar.py`'s `build_avatar(gender)` clears the scene and calls every Task
11.1–11.6 `build_*()` function in order (skeleton, body, materials, morphs, clothes,
hair, animations), returning `(armature_obj, objects)` where `objects` is the armature
plus its mesh children (`export_gltf.export_avatar()`'s expected input). Run headless:

```
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender male --out assets/avatar/generated/male_avatar.glb
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender female --out assets/avatar/generated/female_avatar.glb
```

(Arguments go after Blender's own `--` so Blender doesn't try to parse them itself.)

**Female is the identical male rig, scaled.** `--gender female` applies a single overall
`armature_obj.scale = (0.93, 0.93, 0.93)` after building — a coarse placeholder, *not*
real proportion differentiation (shoulder/hip width ratio, head size, etc.). Real
parametric variation is deliberately deferred to `plan_net.md` Task 11.13 rather than
attempted here; don't read the current female output as more than "the same body,
smaller."

`export_gltf.py`'s `export_avatar(output_path, objects)` selects exactly `objects` and
calls Blender's glTF2 exporter (`export_format="GLB"`, `use_selection=True`,
`export_animation_mode="ACTIONS"` so `Stand0`/`Wave` export as two separate glTF
animations rather than merged, plus `export_morph`/`export_skins`/`export_yup=True`).

**Verified beyond "runs without error":**
- Both `--gender male` and `--gender female` reopen cleanly in Blender
  (`bpy.ops.import_scene.gltf`) with the correct 6 objects, correct armature parenting,
  both actions, and the `Smile`/`Blink` shape keys all present. Female's skeleton node
  scale round-trips as exactly `(0.93, 0.93, 0.93)`.
- **Determinism:** running the male export twice produces byte-identical JSON and a
  binary buffer that differs in ~4.5% of float32 values, every one by exactly 1 ULP
  (`2^-23`) — Blender-internal floating-point rounding noise, not a real difference.
  Satisfies the plan's explicit "byte-identical **or near-identical**" bar.

**Confirmed, not-fixed findings** (same spirit as the elbow-tear check above — direct
inspection, not guessing):
- The exporter warns `Mesh Cylinder is not valid` — the body mesh's underlying
  data-block still carries its `primitive_cylinder_add`-era name; cosmetic naming
  leftover, unrelated to the warning's actual cause.
- 24 vertices on `CNAAvatarShirt` have more than 4 bone influences (glTF's hard limit is
  4; the exporter trims/renormalizes them) — an expected consequence of automatic
  weights on overlapping garment geometry, confirmed by direct per-vertex inspection.
- 32 of `CNAAvatarBody`'s 1086 vertices have **zero** total bone weight (also confirmed
  by direct inspection). Blender's exporter covers this by adding a synthetic
  `neutral_bone` joint to the skin. Reopening in Blender additionally creates a cosmetic
  `Icosphere` bone-shape widget to visualize that bone (it has no natural head/tail) —
  that widget is **not** in the exported file itself (absent from its own node/mesh
  list), purely an artifact of Blender's own importer UI.

None of the above breaks the file or blocks Task 11.7's own bar (deterministic-enough,
reopens cleanly) — they're additional real gaps worth closing in the same future
weight-painting pass as the elbow tear, not before.

`build_avatar(gender)`/`export_avatar(output_path, objects)` are the final pieces
`generate_avatar.py`'s own `__main__` block calls; nothing else needs to import them.

Verify: `blender --background --python tools/avatar_builder/generate_avatar.py -- --gender male --out /tmp/male_avatar.glb`
produces a non-empty file that reopens cleanly in Blender.
