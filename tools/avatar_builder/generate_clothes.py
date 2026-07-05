#!/usr/bin/env python3
"""Builds CNA's placeholder clothes — `Shirt`, `Pants`, and `Shoes` — as offset shells
layered over the Task 11.2 body mesh, each its own mesh object parented to the skeleton
with automatic weights, same technique as the body itself (Task 11.2) and using its
primitive helpers (`generate_body.add_cylinder_segment`/`add_joint_sphere`) directly.

Each garment *slot* (Shirt/Pants/Shoes) can be built from one of several named *styles*
(Task 11.14, see `GARMENT_STYLES` below) — each style covers a different fixed subset of
bones with a cylinder+joint-sphere "shell" per bone, at that bone's own
`generate_body.BONE_RADII` radius plus a small outward padding — a slightly bigger
version of the body geometry underneath it, not real cloth simulation or fitted
tailoring. Explicitly expected to look crude at this stage (offset shells clipping
through the body at the seams) — a known, accepted limitation of this iteration, not a
bug to chase down yet (see `generate_hair.py`'s docstring for the same caveat about
hair). The exported mesh object is always named after its *slot*
(`CNAAvatarShirt`/`Pants`/`Shoes`), never its style — a slot is a fixed content-pipeline
part name; the style only changes which bones its shell covers.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_clothes.py

`generate_avatar.py` (Task 11.7) will import build_clothes() from this module and call
it in the same Blender process as the skeleton/body/material builders, rather than
re-deriving the garments. `generate_wardrobe.py` (Task 11.14) can also export any one
garment style on its own, as a standalone attachable `.glb`.
"""

import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_clothes.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)
import generate_body  # noqa: E402
import generate_materials  # noqa: E402

# Garment slot -> {style name: (bones it covers, outward padding in meters added to
# that bone's generate_body.BONE_RADII radius, material part name from
# generate_materials.py)}. Task 11.14: each slot's DEFAULT_STYLES entry below reproduces
# the exact bone list this dict used before Task 11.14 (when it was a flat
# name -> (bones, padding, material) dict with no style concept) — build_clothes()'s
# default output (styles=None) is therefore unaffected by this refactor. The other
# styles are real, additional silhouette variants, not just relabeling.
GARMENT_STYLES = {
    "Shirt": {
        "TShirt": (
            ["Spine", "Spine1", "Shoulder.L", "Shoulder.R", "UpperArm.L", "UpperArm.R"],
            0.02,
            "Shirt",
        ),
        "LongSleeve": (
            ["Spine", "Spine1", "Shoulder.L", "Shoulder.R", "UpperArm.L", "UpperArm.R",
             "LowerArm.L", "LowerArm.R"],
            0.02,
            "Shirt",
        ),
    },
    "Pants": {
        "Pants": (
            ["Hips", "UpperLeg.L", "UpperLeg.R", "LowerLeg.L", "LowerLeg.R"],
            0.02,
            "Pants",
        ),
        "Shorts": (
            ["Hips", "UpperLeg.L", "UpperLeg.R"],
            0.02,
            "Pants",
        ),
    },
    "Shoes": {
        "Shoes": (
            ["Foot.L", "Foot.R"],
            0.015,
            "Shoes",
        ),
    },
}

# Slot -> style name used when build_clothes() isn't told otherwise — exactly what
# GARMENTS (pre-Task-11.14) built for every slot.
DEFAULT_STYLES = {"Shirt": "TShirt", "Pants": "Pants", "Shoes": "Shoes"}

NAME_PREFIX = "CNAAvatar"


def _build_garment(garment_name, bone_names, padding, bones_by_name, height_scale):
    parts = []
    for bone_name in bone_names:
        head, tail = bones_by_name[bone_name]
        radius = generate_body.BONE_RADII[bone_name] * height_scale + padding
        parts.append(generate_body.add_cylinder_segment(f"{garment_name}_{bone_name}_shell", head, tail, radius))
        parts.append(generate_body.add_joint_sphere(f"{garment_name}_{bone_name}_joint", head, radius))

    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    obj = bpy.context.object
    obj.name = f"{NAME_PREFIX}{garment_name}"
    obj.data.name = f"{NAME_PREFIX}{garment_name}"
    return obj


def build_clothes(armature_obj, materials, bones=None, height_scale=1.0, styles=None):
    """Builds Shirt/Pants/Shoes mesh objects, each parented to `armature_obj` with
    automatic (heat-map) vertex weights and assigned its matching material from
    `materials` (as returned by generate_materials.build_materials()). Returns a
    {garment_slot: mesh_object} dict. Safe to call repeatedly in the same Blender
    session — removes any pre-existing garment objects of the same name first.

    `bones` optionally overrides the canonical `generate_skeleton.BONES` table (Task
    11.13) — must be the same bone list passed to `generate_skeleton.build_skeleton()`.
    `height_scale` scales each garment's underlying body radius to match (the fixed
    padding on top is left unscaled, same absolute clearance regardless of body size).
    `styles` optionally overrides DEFAULT_STYLES's choice of variant for one or more
    slots (Task 11.14), e.g. `styles={"Shirt": "LongSleeve"}` — slots not mentioned keep
    their DEFAULT_STYLES entry."""
    if bones is None:
        bones = generate_skeleton.BONES
    bones_by_name = {name: (head, tail) for name, _parent, head, tail, _connected in bones}
    resolved_styles = {**DEFAULT_STYLES, **(styles or {})}

    garment_objs = {}
    for garment_slot, style_name in resolved_styles.items():
        bone_names, padding, material_part = GARMENT_STYLES[garment_slot][style_name]

        existing = bpy.data.objects.get(f"{NAME_PREFIX}{garment_slot}")
        if existing is not None:
            bpy.data.meshes.remove(existing.data, do_unlink=True)

        obj = _build_garment(garment_slot, bone_names, padding, bones_by_name, height_scale)

        obj.data.materials.clear()
        obj.data.materials.append(materials[material_part])

        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        armature_obj.select_set(True)
        bpy.context.view_layer.objects.active = armature_obj
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")

        garment_objs[garment_slot] = obj

    return garment_objs


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = generate_body.build_body(armature_obj)
    materials = generate_materials.build_materials()
    generate_materials.assign_body_material(body_obj, materials)
    garments = build_clothes(armature_obj, materials)

    for slot, obj in garments.items():
        bone_names, _padding, material_part = GARMENT_STYLES[slot][DEFAULT_STYLES[slot]]
        covered_bones = set(bone_names)
        group_names = {g.name for g in obj.vertex_groups}
        missing = covered_bones - group_names
        print(f"Built '{obj.name}' with {len(obj.data.vertices)} vertices, "
              f"{len(obj.vertex_groups)} vertex groups, material "
              f"{obj.data.materials[0].name if obj.data.materials else '(none)'}.")
        if missing:
            print(f"WARNING: no vertex group for bones: {sorted(missing)}")
        assert not missing, f"{slot}: automatic weights missing groups for {sorted(missing)}"
        assert obj.data.materials and obj.data.materials[0].name == materials[material_part].name

    print(f"OK: {', '.join(garments)} exist, each vertex-grouped to its covered bones "
          f"with the correct material assigned.")
