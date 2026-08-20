# Procedural avatar asset generator (Phase 11)

Offline, one-time content-authoring pipeline that generates the body/skeleton/animation
content behind `AvatarRenderer`'s real-rendering extension (see
`docs/avatar-real-rendering-ext.md`). Not part of the C++ build; never run by CNA at
runtime — its only output is `.glb` files consumed later by the existing content
pipeline (`tools/avatar_asset_pipeline/`, Phase 10).

## Usage

**Just want a working avatar?** Run the top-level driver, which calls every other
script in this directory in order and produces one `.glb`:

```
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender male --out assets/avatar/generated/male_avatar.glb
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender female --out assets/avatar/generated/female_avatar.glb
python3 tools/avatar_builder/validate_gltf.py assets/avatar/generated/male_avatar.glb
```

(`--` separates this script's own arguments from Blender's — everything after it goes
to `argparse`, not Blender's CLI parser.)

**Working on one stage only?** Every `generate_*.py` script other than
`generate_avatar.py` itself can also run standalone via `blender --background --python
tools/avatar_builder/generate_<stage>.py` — each one builds only its own prerequisites
(e.g. `generate_body.py` builds the skeleton first, then the body) and runs its own
`assert`-based self-check, printing `OK: ...` on success. Use this when iterating on a
single stage instead of re-running the whole pipeline. Each per-script section below has
its own standalone-run command under "Verify:".

**`validate_gltf.py` is the one script here that is *not* run through Blender** — it's
plain `python3` (via `pygltflib`), meant to run against an already-exported `.glb`
(`python3 tools/avatar_builder/validate_gltf.py path/to/avatar.glb`). Every other script
needs Blender's own Python (`bpy`) and must be invoked with `blender --background
--python ...`, not plain `python3`.

**Want a custom body shape?** (Task 11.13) `--gender` picks a `GENDER_PRESETS` starting
point; `--height-scale`/`--shoulder-width-scale`/`--head-scale` override any of its three
values individually:

```
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender female --head-scale 1.1 --out /tmp/female_bighead.glb
```

See "Parametric body variation" near the end of this file for how the three parameters
work and what they do (and don't) affect.

**Want different hair/clothing?** (Task 11.14) `--hair-style`/`--shirt-style`/
`--pants-style` pick a variant, baked into the same combined export:

```
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender male --hair-style Ponytail --shirt-style LongSleeve --pants-style Shorts --out /tmp/male_variant.glb
```

To export a single hair/clothing variant on its own, as a standalone attachable `.glb`
(not merged into a full avatar), use `generate_wardrobe.py` instead:

```
blender --background --python tools/avatar_builder/generate_wardrobe.py -- --piece hair --style Ponytail --out /tmp/hair_ponytail.glb
```

See "Wardrobe pieces" near the end of this file for what "attachable" does (and doesn't
yet) mean.

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
- [x] Task 11.8 — `validate_gltf.py`: plain-python3 pygltflib sanity check, fails loudly on gaps.
- [x] Task 11.9 — this file: usage instructions, design rationale, per-script status/placeholder notes.
- [x] Task 11.13 — parametric body variation (`height_scale`/`shoulder_width_scale`/`head_scale`), see below.
- [x] Task 11.14 — additional hair styles / clothing variants, plus `generate_wardrobe.py` for standalone attachable pieces, see below.
- [x] Task 11.15 — additional animation presets (`Stand1`/`Clap`/`Celebrate`), see below.

**Phase 11a ("one male + one female avatar that draws") is functionally complete as of
Task 11.9. Phase 11b (real C++ engine integration) is complete as of Task 11.12. Phase
11c (procedural variety) is complete as of Task 11.15** — see
`docs/avatar-real-rendering-ext.md` and `plans/plan_net.md` for full detail. Phase 11d
(Task 11.16, optional/future, not scheduled) is all that remains.

**Update (`plans/plan_net.md` Phase 7, decision 4b):** the body/clothing generation this file
describes below (`generate_body.py`/`generate_clothes.py`, joined via Blender's plain
`bpy.ops.object.join()`) is what actually produced the "monster" avatars — visible
self-intersecting mesh explosions at every joint, both statically and mid-animation, since
a datablock join never welds geometry at the seams. **`generate_avatar.py`/
`generate_wardrobe.py` now use `generate_body_meshcraft.py`/`generate_clothes_meshcraft.py`
in production** (aliased in as drop-in replacements: `import generate_body_meshcraft as
generate_body`), which build the same primitive shapes but merge them with real CSG
(constructive solid geometry) union via the sibling `mesh-craft` tool instead of a
datablock join — see "Mesh-craft CSG pipeline (Phase 7)" below for the full pipeline, and
`docs/avatar-real-rendering-ext.md`'s own "Phase 7" section for the rendering-side story.
The original `generate_body.py`/`generate_clothes.py` (documented in their original,
unmodified form in the rest of this file) remain standalone-runnable and are not deleted,
but are no longer what a normal `generate_avatar.py` run actually builds.

Full task detail: `plans/plan_net.md` Phase 11 ("Procedural Avatar Asset Generator") and Phase 7
("Avatar asset quality: stop the 'monster' avatars").

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

**Automatic weights are a starting point, not a finished result.** An early manual
`BLENDER_WORKBENCH` render (T-pose) plus an ad hoc pose-mode elbow/knee bend test — on
the body alone, no clothes, no real animation — showed reasonable proportions and no
mesh tearing at the bend. That was **not** the plan's required "no gross bending
artifacts" check, though: the real check needed the actual `Stand0`/`Wave` animations
and the clothed avatar, and was done later, once Task 11.6 existed (see the "Bend-artifact
check" subsection under Placeholder animations, below) — it found a real tear at the
elbow/sleeve that this early body-only test didn't catch. A manual weight-painting
correction pass is still needed and not yet done.

Verify: `blender --background --python tools/avatar_builder/generate_body.py` runs
without error and asserts a non-empty vertex group exists for every bone name in `BONES`.

## Mesh-craft CSG pipeline (Phase 7, `generate_body_meshcraft.py` / `generate_clothes_meshcraft.py`)

**This is what `generate_avatar.py`/`generate_wardrobe.py` actually build with now** — drop-in
replacements for `generate_body.build_body()`/`generate_clothes.build_clothes()`, aliased in
(`import generate_body_meshcraft as generate_body`) so the rest of the orchestration pipeline
needed no rewrite. The original modules above are unchanged and still standalone-runnable.

**Why:** `generate_body.py`'s cylinder-per-bone geometry was always correct in shape; the actual
bug was how the pieces were combined. `bpy.ops.object.join()` (the original approach) merges mesh
*datablocks* into one object without welding geometry at the seams — every limb visibly
self-intersected/exploded at every joint, both statically and mid-animation. That's the "monster"
avatar this phase exists to fix (decision 4b).

**How:** the same capsule/sphere primitives are written out as a `.mc3.xml` document (the sibling
[`mesh-craft`](../../../mesh-craft) tool's own format) with every primitive wrapped in one
`<union material="skin">` — mesh-craft's Manifold-backed CSG engine produces a genuine watertight
boolean merge, not a datablock join. mesh-craft's own `mc3togltf` CLI (resolved via the
`$MC3TOGLTF` env var or a conventional build path, `_locate_mc3togltf()`) exports the unioned
result to `.glb`, which `bpy.ops.import_scene.gltf` then imports, merges/renames to
`CNAAvatarBody`, and parents to the armature — `generate_body.fix_automatic_weights` still does
the actual skinning, called with a wider `blend_radius` (`avg_radius*1.6`, up from a flat `0.08`)
to match the now-merged geometry.

**Coordinate frame (verified empirically, not assumed):** mesh-craft uses a Y-up frame; Blender
(and this project's own skeleton) is Z-up. `_mc3_position()` applies the remap on every primitive:
`mc3.X → Blender.X`, `mc3.Y → Blender.Z`, `mc3.Z → Blender.Y`.

**CSG's documented limitations don't matter here:** per mesh-craft's own `MC3_FORMAT.md`, a
unioned mesh gets a placeholder `UV=(0,0)`, flat recomputed normals, and loses per-child
materials. Confirmed a non-issue *before* relying on it, not assumed: `CNAAvatarBody.png` (the
material `generate_materials.py` assigns) is a solid 4×4 white placeholder texture — real skin
color is a runtime tint (`AvatarAppearanceEXT::setSkinColorProperty`), not baked UVs, so losing
real UVs costs nothing visually.

**Other changes bundled with this fix:** `BONE_RADII` were thickened (~2× for arms, head grown
0.11→0.15) — plain-primitive geometry could get away with thinner radii since a visible seam gap
didn't matter as much; a real watertight merge needed thicker geometry to read as proportioned.
`generate_clothes_meshcraft.py` needed two of its own real bug fixes, found via screenshot
inspection after the body-only fix worked: (1) it initially still referenced the *old*, thinner
`generate_body.BONE_RADII` instead of the new module's; (2) even after that fix, the shirt/pants
were a barely-visible sliver because their `~0.02m` padding constant (see the garment table
above) was tuned against the old thin body — fixed with a `padding * 1.8` multiplier, scoped to
this new generator only (the original `generate_clothes.py`'s own constants are untouched).

**Honest result** (verified via direct screenshot comparison across both genders, 3 angles, and a
mid-animation pose): the core "monster" complaints — disproportionate stick-thin limbs, a
too-small head, severe self-intersecting mesh explosions at every joint — are genuinely fixed on
the body/skin itself. This **directly supersedes** the "confirmed elbow/sleeve tear" findings
documented below under "Placeholder animations"/"Bend-artifact check" and "Orchestration and
export" — those were measured against the old `generate_body.py`/`generate_clothes.py` output and
have not been re-measured against the mesh-craft pipeline's own geometry; treat those sections as
historical record of the original Phase 11 pipeline, not the current state. Smaller gaps remain
open on the new pipeline, honestly documented rather than glossed over: a residual shoe-area dark
artifact, a `Wave`-pose chest-band artifact, and `validate_gltf.py` still lacking NaN/Inf/
bone-index-bounds checks on generated content (see `plans/plan_net.md` Phase 7's own "Honest overall
assessment" for the authoritative, up-to-date list).

Verify: `blender --background --python tools/avatar_builder/generate_body_meshcraft.py` runs the
same way as `generate_body.py` (needs `$MC3TOGLTF` resolvable, or mesh-craft built at one of the
conventional paths `_locate_mc3togltf()` checks).

## Placeholder materials (`generate_materials.py`)

Five flat-color Principled BSDF materials, no texture maps: `CNAAvatarSkin`,
`CNAAvatarHair`, `CNAAvatarShirt`, `CNAAvatarPants`, `CNAAvatarShoes` (see
`MATERIAL_COLORS` for the exact RGBA values). `build_materials()` only *creates* all
five and `assign_body_material()` only assigns `Skin` to the body mesh — the other four
are assigned to their matching garment/hair mesh by `generate_clothes.py`/
`generate_hair.py` (Task 11.5) instead, once that geometry exists (this script alone,
run standalone, has nothing to assign them to yet, since it doesn't build clothes/hair
itself).

`build_materials()`/`assign_body_material(body_obj, materials)` are importable the same
way as the skeleton/body builders, for reuse by `generate_avatar.py` (Task 11.7).

These are explicitly placeholder flat colors, not final art — texture painting or
tinting variety is out of scope until a later iteration (`plans/plan_net.md` Phase 11c).

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

Three garment *slots*, each its own mesh object parented to the skeleton with automatic
weights (same technique as the body) and its matching material assigned. Since Task
11.14, each slot can be built from one of several named *styles* (`GARMENT_STYLES`); the
mesh object is always named after its slot (`CNAAvatarShirt`/`Pants`/`Shoes`), never its
style — the style only changes which bones the shell covers:

| Slot | Style (`DEFAULT_STYLES`) | Bones covered | Padding over body radius |
|---|---|---|---|
| `CNAAvatarShirt` | `TShirt` (default) | `Spine, Spine1, Shoulder.L/R, UpperArm.L/R` | +0.02 m |
| `CNAAvatarShirt` | `LongSleeve` | above + `LowerArm.L/R` (sleeve reaches the wrist) | +0.02 m |
| `CNAAvatarPants` | `Pants` (default) | `Hips, UpperLeg.L/R, LowerLeg.L/R` | +0.02 m |
| `CNAAvatarPants` | `Shorts` | `Hips, UpperLeg.L/R` only (bare lower leg) | +0.02 m |
| `CNAAvatarShoes` | `Shoes` (only style) | `Foot.L/R` | +0.015 m |

Each garment is built the same way as the body: one `generate_body.add_cylinder_segment`
+ `generate_body.add_joint_sphere` per covered bone (that bone's own `BONE_RADII` radius
plus the garment's padding), joined into a single mesh. These are offset shells over the
existing body shape, not fitted tailoring or cloth simulation — expect visible clipping
through the body at the sleeve/pant-leg/shoe seams. Known, accepted limitation of this
iteration, not a bug to chase down yet.

`build_clothes(armature_obj, materials, styles=None)` is importable the same way as the
other builders, for reuse by `generate_avatar.py` (Task 11.7) and `generate_wardrobe.py`
(Task 11.14); `styles={"Shirt": "LongSleeve"}`-style overrides pick a non-default style
per slot, defaulting to `DEFAULT_STYLES` (== this script's pre-Task-11.14 behavior)
otherwise.

Verify: `blender --background --python tools/avatar_builder/generate_clothes.py` runs
without error and asserts each garment has a vertex group for every bone it covers and
its correct material assigned (default styles only — the standalone run doesn't sweep
every style; see "Wardrobe pieces" below for how the non-default styles were verified).

## Placeholder hair (`generate_hair.py`)

`CNAAvatarHair`, selectable between two styles (`HAIRSTYLES`, Task 11.14):

- **`Cap`** (default) — a single open-bottomed hemisphere shell (built directly with
  `bmesh`, not `generate_body`'s cylinder/sphere helpers, since it needs half a sphere
  rather than a whole one) sized just outside the head.
- **`Ponytail`** — the same cap, plus a tapered cone "tail" hanging off the back of the
  head (built with `bmesh.ops.create_cone`, transformed to droop down and backward),
  joined into the same single mesh/object rather than a separate part.

Both are parented to the skeleton with automatic weights — since only the `Head` bone is
nearby, this ends up rigidly following the head in practice, without needing
bone-parenting. Assigned the `Hair` material.

This is still a literal, crude shape, not modeled hair strands or hair cards —
explicitly expected and accepted for this iteration (a later iteration can replace either
style with real hair geometry).

`build_hair(armature_obj, materials, style="Cap")` is importable the same way as the
other builders, for reuse by `generate_avatar.py` (Task 11.7) and `generate_wardrobe.py`
(Task 11.14).

Verify: `blender --background --python tools/avatar_builder/generate_hair.py` runs
without error and asserts the (default `Cap`) hair mesh is non-empty, has a vertex group
for `Head`, and has the `Hair` material assigned.

## Placeholder animations (`generate_animations.py`)

Five Blender Actions on `CNAAvatarSkeleton`, named to match `AvatarAnimationPreset`
exactly (see `AvatarAnimationPresetToClipNameEXT`). Task 11.6 authored the first two;
Task 11.15 added the other three, working toward covering more of
`AvatarAnimationPreset`'s 31 values (5/31 is still a small fraction — this remains
placeholder motion, not a claim of full coverage):

- **`Stand0`** (idle, 90 frames, loops — frame 1 and frame 90 are identical): a subtle
  `Hips` bob (+-0.01 m) and `Spine1` rock (+-2 deg).
- **`Stand1`** (a second idle, 100 frames, loops): deliberately shaped differently from
  `Stand0` so the two are never confusable — a slow side-to-side weight shift (`Hips`
  sways in X, not `Stand0`'s up/down Z bob) with a counter-rotating torso twist
  (`Spine1` rotates around its own local Y — its length axis, a genuine twist for this
  vertical bone — not `Stand0`'s local-X front/back rock).
- **`Wave`** (60 frames, plays once): `UpperArm.R` rotates to raise the arm, then
  `LowerArm.R` folds the elbow back and forth a few times before both return to rest.
- **`Clap`** (48 frames, plays once): both arms raise together to roughly chest height,
  then both forearms oscillate their fold angle four times in sync — a crude, symmetric
  approximation of clapping (arms coming up in front of the body and pulsing together),
  not a literal hands-meet-at-center gesture; this cylinder-and-bone rig has no IK to aim
  the hands at each other.
- **`Celebrate`** (60 frames, plays once): both arms raise together to the same angle
  Wave already uses for `UpperArm.R` (see the gotcha below for why a bigger, untested
  angle was tried first and rejected) and hold, while `Hips` bounces up twice — a
  triumphant "arms up" pose with a little pump to it.

All five are simple keyframed bone rotations — no motion capture, no external clip
source — authored directly against the Task 11.1 bone names, so there is no retargeting
step.

**A real gotcha, worth knowing before adding a third animation (Task 11.6):** the first
`Wave` attempt keyframed `LowerArm.R`'s rotation on its local Y axis, which is the
bone's own head-to-tail length axis — rotating a round cylinder around its own length
axis is an invisible twist. This produced a "working" action (nonzero frame range, no
errors) that did *nothing visible* when rendered. Caught only by actually rendering it
and comparing frames. Local X (or Z, depending on the bone's orientation/roll) is what
visibly bends a limb — verify any new bone-rotation animation by rendering it, not just
by checking that keyframes exist.

**A second, sneakier gotcha (Task 11.15), worth knowing before mirroring any single-arm
gesture onto both arms:** `Wave`'s `UpperArm.R`/`LowerArm.R` convention does not mirror
onto `UpperArm.L`/`LowerArm.L` with a single consistent rule, and testing bone-by-bone
in isolation gives a *wrong* answer for one of the two joints. `UpperArm.L/R`'s "raise
the arm" axis (local Z) needs an **opposite-signed** angle between `.L` and `.R` for the
same physical motion, confirmed whether tested alone or combined with a forearm fold.
`LowerArm.L/R`'s "fold the elbow" axis (local X) tests as opposite-signed too **if posed
alone** (matching upper arm at T-pose rest) — but once the matching upper arm is
*actually raised* (the real context this rig is ever posed in), both sides need the
**same** sign instead. Why: a child bone's local rotation composes with its parent's
*current* world transform, not its rest transform, so an isolated single-bone test
silently assumes the parent is still at rest — which stops being true the moment the
arm is actually raised, and the same local angle then swings a different way in world
space. `Celebrate`'s first attempt also used an untested, much bigger raise angle
(150°) on the theory that "bigger raise = more dramatic overhead pose" — rendered from a
camera angle that made it look plausible at first glance, but a clearer camera position
(elevated, angled down at the figure) revealed it didn't read as an overhead raise at
all. Reusing `Wave`'s own already-verified 80° raise magnitude for both arms fixed it.
**The general lesson, stated once so it doesn't need re-learning:** verify a new pose
empirically, in the *actual combined pose* a call site uses (both joints posed together,
a full render from more than one camera angle) — not bone-by-bone in isolation, and not
from a single camera angle that happens to look plausible. Don't assume any of this
generalizes to bones this script hasn't yet mirrored (`UpperLeg`/`LowerLeg`, etc.)
without the same empirical check. `_raise_upper_arm`/`_fold_lower_arm` in
`generate_animations.py` encapsulate the verified conventions so future animations reuse
them instead of re-deriving signs from scratch.

**Consistent with the known elbow/sleeve tear** (see "Bend-artifact check" below):
posing the clothed avatar through `Clap`'s peak fold shows the same forearm/hand
separating from the shirt sleeve that `Wave` already showed — the same automatic-weights
limitation, not a new or worse issue introduced by this task.

`build_animations(armature_obj)` is importable the same way as the other builders, for
reuse by `generate_avatar.py` (Task 11.7).

Verify: `blender --background --python tools/avatar_builder/generate_animations.py` runs
without error and asserts both actions exist with a nonzero frame range.

### Bend-artifact check (deferred from Task 11.2, done here)

**Historical: measured against the original `generate_body.py`/`generate_clothes.py` pipeline,
before Phase 7's mesh-craft CSG replacement (see "Mesh-craft CSG pipeline" above) — not
re-measured against the current production pipeline's own geometry.**

Posing the full clothed avatar through `Wave`'s peak elbow-fold frames and rendering a
close-up confirms a **real, visible tear** at the elbow/wrist: the forearm and hand
separate from the shirt sleeve (and slightly from each other) at both fold extremes
tested. This is the automatic-weights limitation Task 11.2 always expected, now
*confirmed* rather than assumed. **Still open, not fixed:** a manual weight-painting
correction pass at the elbows (and likely knees/shoulders too, untested at comparable
fold angles) is needed before this rig is presentable in motion. Out of scope for
Phase 11a/11b/11c's "functional, not polished" pipeline milestones (confirmed still
present under `Clap`'s peak fold too, Task 11.15) — revisit when polish work is
prioritized.

## Orchestration and export (`generate_avatar.py` + `export_gltf.py`)

`generate_avatar.py`'s `build_avatar(gender, height_scale=None, shoulder_width_scale=None,
head_scale=None)` clears the scene and calls every Task 11.1–11.6 `build_*()` function in
order (skeleton, body, materials, morphs, clothes, hair, animations), returning
`(armature_obj, objects)` where `objects` is the armature plus its mesh children
(`export_gltf.export_avatar()`'s expected input). Run headless:

```
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender male --out assets/avatar/generated/male_avatar.glb
blender --background --python tools/avatar_builder/generate_avatar.py -- --gender female --out assets/avatar/generated/female_avatar.glb
```

(Arguments go after Blender's own `--` so Blender doesn't try to parse them itself.)

**Female is a real, independently-shaped variant, not a uniform scale of the male rig**
(Task 11.13 — see "Parametric body variation" below for how). `--gender` selects a
`GENDER_PRESETS` entry as the starting point for three scale parameters
(`height_scale`/`shoulder_width_scale`/`head_scale`); `--height-scale`/
`--shoulder-width-scale`/`--head-scale` override any of them individually.

**Hair/clothing style is also selectable** (Task 11.14): `--hair-style`
(`generate_hair.HAIRSTYLES`) and `--shirt-style`/`--pants-style`
(`generate_clothes.GARMENT_STYLES["Shirt"|"Pants"]`) each default to this script's
pre-Task-11.14 style, so an unqualified `--gender male|female` run is unaffected.

`export_gltf.py`'s `export_avatar(output_path, objects)` selects exactly `objects` and
calls Blender's glTF2 exporter (`export_format="GLB"`, `use_selection=True`,
`export_animation_mode="ACTIONS"` so `Stand0`/`Wave` export as two separate glTF
animations rather than merged, plus `export_morph`/`export_skins`/`export_yup=True`).

**Verified beyond "runs without error":**
- Both `--gender male` and `--gender female` reopen cleanly in Blender
  (`bpy.ops.import_scene.gltf`) with the correct 6 objects, correct armature parenting,
  both actions, and the `Smile`/`Blink` shape keys all present. Since Task 11.13, the
  proportion differences are baked directly into bone *positions* at generation time —
  the exported skeleton node itself has no scale transform at all (`None`, i.e.
  identity), unlike the pre-11.13 female preset's `(0.93, 0.93, 0.93)` object-level scale.
- **Determinism:** running the male export twice produces byte-identical JSON and a
  binary buffer that differs in ~4.5% of float32 values, every one by exactly 1 ULP
  (`2^-23`) — Blender-internal floating-point rounding noise, not a real difference.
  Satisfies the plan's explicit "byte-identical **or near-identical**" bar.

**Confirmed, not-fixed findings** (same spirit as the elbow-tear check above — direct
inspection, not guessing). **Also historical, same caveat as the Bend-artifact check above:
measured against the original `generate_body.py`/`generate_clothes.py` pipeline, not
re-measured against Phase 7's mesh-craft CSG replacement:**
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

## Parametric body variation (Task 11.13)

Three independent scale parameters, threaded through every `build_*()` function that
needs them (`generate_skeleton.build_bones()`, then `generate_body.build_body()`,
`generate_clothes.build_clothes()`, `generate_hair.build_hair()`,
`generate_morphs.build_morphs()` — each accepts an optional `bones=` list and, where
relevant, `height_scale=`/`head_scale=`):

- **`height_scale`** — applied uniformly to every bone's (x, y, z) position in
  `generate_skeleton.build_bones()`, giving a proportionally taller/shorter body. Also
  scales every non-Head bone's flesh/garment radius to match (`generate_body.py`/
  `generate_clothes.py`), so a shorter body isn't left disproportionately thick.
- **`shoulder_width_scale`** — applied *additionally*, only to the X coordinate of the
  arm chain (`Shoulder`/`UpperArm`/`LowerArm`/`Hand`, both `.L`/`.R`), independently
  widening or narrowing the shoulders/arm span without touching height. Everything else
  (torso, legs, head) is unaffected.
- **`head_scale`** — scales only the Head bone's own flesh/hair-cap radius
  (`generate_body.py`/`generate_hair.py`/`generate_morphs.py`), independently of
  `height_scale` — e.g. a stylized disproportionately large or small "chibi" head at any
  body height. Doesn't touch skeleton bone positions at all.

`generate_avatar.py`'s `GENDER_PRESETS` gives `--gender male`/`female` a starting point
for all three (female: `height_scale=0.93, shoulder_width_scale=0.85, head_scale=0.97`
— a coarse, explicitly placeholder silhouette, not measured/researched proportions);
`--height-scale`/`--shoulder-width-scale`/`--head-scale` override any of the preset's
values individually, e.g. `--gender female --head-scale 1.1`.

This conceptually echoes `AvatarDescription`'s customization intent (height, build)
without attempting to reconstruct its real, undocumented byte format — these three
parameters are a **CNA-original**, independent parameterization, not a decode of any
real Xbox Avatar data.

Every other script's own standalone `blender --background --python
generate_<stage>.py` run is unaffected — all new parameters default to `bones=None`
(the canonical, unscaled `generate_skeleton.BONES`) and `height_scale=head_scale=1.0`,
so nothing changes unless `generate_avatar.py` (or a caller) explicitly asks for a
custom body.

Verify: rendered (not just built) four combinations — default male, default female
preset, an extreme `height_scale=1.0, shoulder_width_scale=0.6, head_scale=1.6` ("big
head, narrow shoulders"), and `height_scale=1.2, shoulder_width_scale=1.3, head_scale=1.0`
("tall and wide") — confirming each parameter visibly and independently changes the
right part of the body, not just that the script accepts new arguments. Also confirmed:
a custom-parameter export still validates cleanly (`validate_gltf.py`), still converts
cleanly (`convert_avatar.py --embedded-clips`), and its rest-pose bone transforms are
still bit-for-bit identity (the same exact-math check from Task 11.11), at non-unit
scale factors.

## Wardrobe pieces (`generate_wardrobe.py`, Task 11.14)

`generate_avatar.py` always bakes hair + all three clothing slots into one combined
avatar `.glb`. `generate_wardrobe.py` instead builds and exports exactly ONE hair style
or ONE clothing variant, on its own, as a standalone `.glb` — just that piece's mesh plus
a copy of the `CNAAvatarSkeleton` armature at rest pose (needed for its own vertex-group
skinning data), independent of any specific full-avatar export:

```
blender --background --python tools/avatar_builder/generate_wardrobe.py -- --piece hair --style Ponytail --out /tmp/hair_ponytail.glb
blender --background --python tools/avatar_builder/generate_wardrobe.py -- --piece shirt --style LongSleeve --out /tmp/shirt_longsleeve.glb
blender --background --python tools/avatar_builder/generate_wardrobe.py -- --piece pants --style Shorts --out /tmp/pants_shorts.glb
```

`--gender`/`--height-scale`/`--shoulder-width-scale`/`--head-scale` work the same as
`generate_avatar.py` (Task 11.13) — a wardrobe piece is built against the same
proportioned skeleton a matching body would use, so it fits.

**"Attachable" today means "convertible independently," not "loadable alongside a body
at runtime yet."** `tools/avatar_asset_pipeline/convert_avatar.py` (Phase 10, unmodified
by this task) already converts any GLB with one skin and one-or-more meshes into a
`.skinnedmodel.json` with one part per mesh — nothing in it assumes a "body" mesh must be
present. Verified directly: converting a standalone `hair_ponytail.glb` produces a clean
`avatar.skinnedmodel.json` (19 bones, 1 part: `CNAAvatarHair`) with no code changes.
`Microsoft::Xna::Framework::GamerServices::AvatarRenderer`/`Graphics::SkinnedModelEXT`,
however, currently load and draw exactly **one** model at a time (`realModel_`, a single
`shared_ptr`) — there is no engine-side API yet to load a second `SkinnedModelEXT` and
render it using a first model's already-computed bone transforms. Actually attaching a
separately-converted wardrobe piece onto a running avatar at draw time is real, future
engine work (closer in spirit to Task 11.16's deferred scope than to this task), not part
of Task 11.14 — this script's job is to prove the *content* is genuinely modular and
already flows through the existing, unmodified content pipeline, not to wire up runtime
attachment.

**A real nuance found while verifying this:** an independently-converted piece's own
`skeleton.bin` is not guaranteed byte-identical to a body's `skeleton.bin` built with the
same proportions — each file's exporter adds its own synthetic `neutral_bone` joint only
if *that* mesh happens to have zero-weight vertices (e.g. the `LongSleeve` shirt triggered
one in testing, the `Ponytail` hair did not), so two independently-converted pieces meant
for the same avatar can end up with different total bone counts. Both still agree on the
first 19 real bones (the canonical skeleton, same rest pose, same order), so this is not
a correctness bug — just a real detail any future runtime-attachment work will need to
account for (e.g. ignore each piece's own trailing synthetic joint and rely only on the
shared 19-bone prefix), noted here rather than glossed over.

**Verified beyond "the script runs":**
- Rendered `Ponytail` from the side: a clearly distinct tapered tail drooping down the
  back of the head, vs. `Cap`'s plain dome — not just a different vertex count.
- Rendered `LongSleeve` vs. `TShirt` and `Shorts` vs. `Pants` on a full-body avatar:
  the sleeve visibly extends to the wrist, and the pant leg visibly stops above the knee
  (bare lower leg exposed), each independently of the other slot's default.
- Every touched script's own standalone run (`generate_hair.py`, `generate_clothes.py`,
  `generate_avatar.py` with no style flags) still produces the exact same vertex counts
  as before Task 11.14 (25 hair, 348/290/116 Shirt/Pants/Shoes, 6-object export) —
  confirming full backward compatibility.
- Reopened each of the three example wardrobe-piece exports in a fresh Blender session:
  each is exactly one real mesh object (`CNAAvatar<Slot>`, plus the same cosmetic
  `Icosphere`/`neutral_bone` widget already documented under Task 11.7 when a piece has
  its own zero-weight vertices) parented to one 19-bone `CNAAvatarSkeleton` armature.
- Converted a standalone piece export with the real, unmodified
  `tools/avatar_asset_pipeline/convert_avatar.py` and confirmed a clean
  `avatar.skinnedmodel.json` + `skeleton.bin` + vertex/index buffers, with no converter
  changes required.

Verify: `blender --background --python tools/avatar_builder/generate_wardrobe.py --
--piece hair --style Ponytail --out /tmp/hair_ponytail.glb` produces a non-empty file;
`python3 tools/avatar_asset_pipeline/convert_avatar.py --body /tmp/hair_ponytail.glb --out /tmp/hair_ponytail_content`
converts it cleanly with the unmodified converter.

## Validating an export (`validate_gltf.py`)

Plain `python3` — no Blender needed for this one:

```
python3 tools/avatar_builder/validate_gltf.py /tmp/male_avatar.glb
```

Runs 4 checks via `pygltflib` and fails loudly (nonzero exit, a `FAIL: <specific
reason>` message) on the first one that doesn't hold, rather than silently accepting a
hollow/broken export:

1. At least one mesh has a non-empty `POSITION` accessor.
2. Some skin's joints cover all 19 canonical bone names from `generate_skeleton.BONES`
   (imported directly, not duplicated — see below). Extra joints (e.g. `neutral_bone`)
   are reported as informational, not treated as a failure.
3. All five animations are present: `Stand0`/`Wave` (Task 11.6), `Stand1`/`Clap`/`Celebrate` (Task 11.15).
4. Both `Smile` and `Blink` appear in some mesh's `extras["targetNames"]` (where
   Blender's glTF exporter records shape-key/morph-target names).

**A real prerequisite fix, not just this script:** `generate_skeleton.py` used to
unconditionally `import bpy` at module level, which meant even importing it (just to
read `BONES`) would immediately `sys.exit()` under plain `python3`. Moved that import
inside `build_skeleton()` (lazy) — `BONES`/`ARMATURE_NAME` are pure data with no bpy
dependency, so `validate_gltf.py` (and anything else needing the canonical bone list
outside Blender) can import them directly instead of duplicating the list. Every other
script in this pipeline still requires bpy at its own module level, unchanged — this
fix is scoped to `generate_skeleton.py` only, since it's the one module whose data
(not just its build function) other tools need.

Verified beyond "runs and prints OK": ran it against a nonexistent path, a garbage
(non-glTF) file, a copy of a real export with `Wave` stripped, and a copy with `Blink`
removed from `targetNames` — each produced a distinct, correct `FAIL:` message and exit
code 1. Both real `male_avatar.glb`/`female_avatar.glb` pass clean.
