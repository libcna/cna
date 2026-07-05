#!/usr/bin/env python3
"""Builds CNA's procedural avatar body — a stylized, low-poly humanoid mesh generated
directly from the Task 11.1 skeleton's own bone positions (see generate_skeleton.py /
README.md), then parented to that skeleton with Blender's automatic (heat-map) vertex
weights.

One primitive-derived "flesh" shape is created per bone (a cylinder along the bone's
head->tail axis, plus a small joint sphere at non-Head bones' head end; the Head bone
gets a single sphere instead of a cylinder), then all parts are joined into a single
mesh object. Building geometry directly from the bone list guarantees every deforming
bone has nearby mesh to receive automatic weights, and keeps body shape and skeleton
in lock-step without any manual authoring step.

Automatic weights are a starting point, not a finished result: expect visible bending
artifacts at elbows/knees/shoulders until a manual weight-painting correction pass is
done — do not treat a clean run of this script alone as proof the rig looks right in
motion. That visual check needs Task 11.6's test animations and is deferred until they
exist (see README.md's Status section).

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_body.py

`generate_avatar.py` (Task 11.7) will import build_body() from this module and call it
in the same Blender process as generate_skeleton.build_skeleton(), rather than
re-deriving the body shape.
"""

import sys
from pathlib import Path

try:
    import bpy
    import mathutils
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_body.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)

BODY_NAME = "CNAAvatarBody"

# Approximate limb radius (meters) per bone, used for both the cylinder "flesh"
# segment along the bone and the joint sphere at its head end. Purely a visual
# starting point for a stylized low-poly body, not anatomically precise.
BONE_RADII = {
    "Hips": 0.14, "Spine": 0.13, "Spine1": 0.13, "Neck": 0.05, "Head": 0.11,
    "Shoulder.L": 0.05, "Shoulder.R": 0.05,
    "UpperArm.L": 0.06, "UpperArm.R": 0.06,
    "LowerArm.L": 0.05, "LowerArm.R": 0.05,
    "Hand.L": 0.04, "Hand.R": 0.04,
    "UpperLeg.L": 0.09, "UpperLeg.R": 0.09,
    "LowerLeg.L": 0.07, "LowerLeg.R": 0.07,
    "Foot.L": 0.05, "Foot.R": 0.05,
}


def add_cylinder_segment(name, head, tail, radius):
    """Adds a cylinder running from world point `head` to `tail` with the given radius,
    oriented along that axis. Public so generate_hair.py/generate_clothes.py (Task 11.5)
    can build their own bone-aligned primitives with the same technique as the body."""
    head_v = mathutils.Vector(head)
    tail_v = mathutils.Vector(tail)
    direction = tail_v - head_v
    bpy.ops.mesh.primitive_cylinder_add(
        radius=radius, depth=direction.length, location=(head_v + tail_v) / 2, vertices=8,
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = mathutils.Vector((0.0, 0.0, 1.0)).rotation_difference(direction)
    return obj


def add_joint_sphere(name, location, radius):
    """Adds a low-poly UV sphere at `location`. Public for the same reason as
    add_cylinder_segment() above."""
    bpy.ops.mesh.primitive_uv_sphere_add(
        radius=radius, location=location, segments=8, ring_count=6,
    )
    obj = bpy.context.object
    obj.name = name
    return obj


def build_body(armature_obj, bones=None, height_scale=1.0, head_scale=1.0):
    """Builds the procedural low-poly body mesh, joins every part into a single mesh
    object, and parents it to `armature_obj` with automatic (heat-map) vertex weights.
    Returns the body mesh object. Safe to call repeatedly in the same Blender session
    (e.g. from generate_avatar.py) — removes any pre-existing body object first.

    `bones` optionally overrides the canonical `generate_skeleton.BONES` table (e.g.
    with `generate_skeleton.build_bones(...)`'s output, Task 11.13) — must be the SAME
    bone list passed to `generate_skeleton.build_skeleton()`, so the body's geometry
    lines up with the actual armature. `height_scale` scales every non-Head bone's
    flesh radius to match (Task 11.13's height parameter, applied here since bone
    *positions* already carry it via `bones`, but radius does not scale automatically).
    `head_scale` scales the Head bone's own radius independently of height (Task 11.13's
    head-size parameter — deliberately decoupled from height_scale, e.g. for a
    disproportionately large/small "chibi"-style head)."""
    if bones is None:
        bones = generate_skeleton.BONES

    existing = bpy.data.objects.get(BODY_NAME)
    if existing is not None:
        bpy.data.meshes.remove(existing.data, do_unlink=True)

    parts = []
    for name, _parent, head, tail, _connected in bones:
        radius = BONE_RADII[name] * (head_scale if name == "Head" else height_scale)
        if name == "Head":
            center = tuple((mathutils.Vector(head) + mathutils.Vector(tail)) / 2)
            parts.append(add_joint_sphere(f"{name}_flesh", center, radius))
        else:
            parts.append(add_cylinder_segment(f"{name}_flesh", head, tail, radius))
            parts.append(add_joint_sphere(f"{name}_joint", head, radius))

    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    body_obj = bpy.context.object
    body_obj.name = BODY_NAME
    body_obj.data.name = BODY_NAME

    bpy.ops.object.select_all(action="DESELECT")
    body_obj.select_set(True)
    armature_obj.select_set(True)
    bpy.context.view_layer.objects.active = armature_obj
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")

    return body_obj


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = build_body(armature_obj)

    bone_names = {name for name, *_rest in generate_skeleton.BONES}
    group_names = {g.name for g in body_obj.vertex_groups}
    missing = bone_names - group_names

    print(f"Built body '{body_obj.name}' with {len(body_obj.data.vertices)} vertices, "
          f"{len(body_obj.vertex_groups)} vertex groups.")
    if missing:
        print(f"WARNING: no vertex group for bones: {sorted(missing)}")
    assert not missing, f"automatic weights produced no vertex group for: {sorted(missing)}"
    print("OK: every skeleton bone has a corresponding vertex group on the body mesh.")
