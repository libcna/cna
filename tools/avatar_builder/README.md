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
- [ ] Task 11.5 — `generate_hair.py` / `generate_clothes.py`
- [ ] Task 11.6 — `generate_animations.py`
- [ ] Task 11.7 — `export_gltf.py` / `generate_avatar.py`
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
